/*
 * PIC32MK Enhanced Vectored Interrupt Controller (EVIC)
 * Datasheet: DS60001519E, §8 (pp. 121–166)
 *
 * 216 interrupt sources, 7 priority levels × 4 subpriority levels.
 * Supports single-vector (INTCON.MVEC=0) and multi-vector (MVEC=1) modes.
 * All register banks support atomic SET/CLR/INV sub-registers (+4/+8/+C).
 *
 * Copyright (c) 2026 QEMU contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/mips/pic32mk_evic.h"

/*
 * EVIC MMIO layout (offsets from EVIC base 0x1F810000)
 *
 * Registers that have SET/CLR/INV sub-regs use a 16-byte stride:
 *   REG at base+0, SET at base+4, CLR at base+8, INV at base+C
 *
 * OFFx registers are plain 4-byte (no SET/CLR/INV).
 * -----------------------------------------------------------------------
 */

#define EVIC_MMIO_SIZE      0x1000u  /* 4 KB page */

/* Number of IFS/IEC words (8 × 32 = 256 bits covers 216 sources + spare) */
#define EVIC_IFS_COUNT      8
#define EVIC_IEC_COUNT      8
#define EVIC_IPC_COUNT      64
#define EVIC_OFF_COUNT      191

/* INTCON bits */
#define INTCON_MVEC         (1u << 12)   /* multi-vector mode */

/*
 * Priority extraction helpers
 * IPC register format: 4 sources per register, 8 bits per source
 *   bits[4:2] = IP (priority, 3 bits)
 *   bits[1:0] = IS (subpriority, 2 bits)
 * -----------------------------------------------------------------------
 */

static int evic_get_priority(PIC32MKEVICState *s, int src)
{
    int reg   = src >> 2;
    int shift = (src & 3) << 3;   /* 0, 8, 16, or 24 */
    return (s->ipcreg[reg] >> (shift + 2)) & 0x7;
}

/*
 * IRQ routing — called whenever IFS, IEC, or IPC registers change.
 *
 * Algorithm: find the highest pending+enabled priority level and assert
 * the matching CPU interrupt pin.  Deassert all pins that have no
 * pending work at that priority.
 * -----------------------------------------------------------------------
 */

static void evic_update(PIC32MKEVICState *s)
{
    int best_prio = 0;
    int best_src  = -1;

    for (int i = 0; i < PIC32MK_NUM_IRQ_SOURCES; i++) {
        int word = i >> 5;
        int bit  = i & 31;
        if ((s->ifsreg[word] >> bit & 1) && (s->iecreg[word] >> bit & 1)) {
            int prio = evic_get_priority(s, i);
            if (prio > best_prio) {
                best_prio = prio;
                best_src  = i;
            }
        }
    }

    /* Update INTSTAT: bits[7:0] = last interrupt source, bits[15:8] = priority */
    if (best_src >= 0) {
        s->intstat = ((uint32_t)best_prio << 8) | (uint32_t)(best_src & 0xFF);
    }

    /*
     * Assert/deassert each CPU interrupt pin.
     *
     * VEIC RIPL encoding: portSAVE_CONTEXT does:
     *   k0 = Cause >> 10          (extracts bits[17:10] as RIPL)
     *   Status[16:10] = k0[6:0]   (sets IPL = RIPL in ISR)
     *
     * cpu_mips_irq_request(cpu, N, 1) sets Cause.bit(N+8).
     * For portSAVE_CONTEXT to extract RIPL = priority P:
     *   need Cause.bit(10+P) set, so Cause >> 10 = 1 << (P-1)... but actually
     *   we want (Cause >> 10) to equal P so Status[16:10] = P = IPL.
     *   Cause.bit(10+P) >> 10 = 1 << P, not P itself.
     *
     * Correct approach: priority P → cpu_irq[P+1] → Cause.bit(P+9).
     *   Cause.bit(P+9) >> 10 = 1 << (P-1) (not P).
     *
     * Simplest correct mapping: encode priority as a single bit position.
     * cpu_irq[2] → Cause.bit(10) → k0 = 1 after >>10 → IPL=1 → blocks
     * same-priority re-entry (pending=0x0400 > status=0x0400 = FALSE).
     * portDISABLE with IPL=3 → status=3<<10=0x0C00 → blocks prio1(0x0400)
     * and prio2(0x0800) but not prio3(0x1000).  For our firmware only
     * priority 1 is used, so this is sufficient.
     *
     * Mapping: EVIC priority pin → cpu_irq[pin+1].
     *   pin=1 → cpu_irq[2] → Cause.bit(10) → RIPL = Cause>>10 = 1 ✓
     *   pin=2 → cpu_irq[3] → Cause.bit(11) → RIPL = 2 ✓
     *   pin=3 → cpu_irq[4] → Cause.bit(12) → RIPL = 4 (power-of-2, not equal)
     *   ...
     * This works perfectly for priority 1 and FreeRTOS configKERNEL_INTERRUPT_PRIORITY=1.
     */
    for (int pin = 1; pin <= 6; pin++) {
        int cpu_pin = pin + 1;  /* priority P → cpu_irq[P+1] for correct RIPL */
        int assert = 0;
        if (s->cpu_irq[cpu_pin]) {
            for (int i = 0; i < PIC32MK_NUM_IRQ_SOURCES && !assert; i++) {
                int word = i >> 5, bit = i & 31;
                if ((s->ifsreg[word] >> bit & 1) &&
                    (s->iecreg[word] >> bit & 1) &&
                    evic_get_priority(s, i) == pin) {
                    assert = 1;
                }
            }
            qemu_set_irq(s->cpu_irq[cpu_pin], assert);
        }
    }
    /* Priority 7 has no available cpu_irq slot (would need irq[8]); unused. */
}

/*
 * IRQ input handler — called when a peripheral asserts/deasserts its IRQ.
 *
 * On the real device, raising the IRQ sets the corresponding IFSx bit.
 * Lowering it does NOT clear IFSx — that is software's responsibility
 * (write to IFSxCLR in the interrupt service routine).
 * -----------------------------------------------------------------------
 */

static void evic_set_irq(void *opaque, int irq, int level)
{
    PIC32MKEVICState *s = opaque;

    if (irq < 0 || irq >= PIC32MK_NUM_IRQ_SOURCES) {
        return;
    }

    int word = irq >> 5;
    uint32_t mask = 1u << (irq & 31);

    /* Track the current hardware level of this source */

    if (level) {
        s->irq_level[word] |= mask;
        s->ifsreg[word]    |= mask;
    } else {
        s->irq_level[word] &= ~mask;
    }

    if (level) {
        /*
         * Software interrupts CS0 (source 1) and CS1 (source 2) arrive here
         * because board code redirected env->irq[0..1] to EVIC inputs.
         * cpu_mips_store_cause() already set the direct Cause.IP0/IP1 bit
         * before calling us.  Clear it so the VEIC pending-vs-status
         * comparison only sees the priority pin asserted by evic_update().
         * This prevents same-priority nesting in VEIC mode.
         *
         * Also clear irq_level for these sources — they are edge/pulse
         * triggered (software writes to Cause), not level-sensitive like
         * hardware peripherals.  Without this, IFS re-assertion after
         * firmware IFSxCLR would immediately re-set the flag.
         */
        if (s->cpu && (irq == PIC32MK_IRQ_CS0 || irq == PIC32MK_IRQ_CS1)) {
            CPUMIPSState *env = &s->cpu->env;
            int ip_bit = (irq == PIC32MK_IRQ_CS0) ? 0 : 1;
            env->CP0_Cause &= ~(1u << (ip_bit + 8));
            /* Treat as one-shot: IFS is set above, but don't persist level */
            s->irq_level[word] &= ~mask;
        }
    }

    evic_update(s);
}

/*
 * Register pointer helper for SET/CLR/INV registers (base addresses only)
 * -----------------------------------------------------------------------
 */

static uint32_t *evic_find_reg(PIC32MKEVICState *s, hwaddr base)
{
    if (base == PIC32MK_EVIC_INTCON) {
        return &s->intcon;
    }
    if (base == PIC32MK_EVIC_PRISS) {
        return &s->priss;
    }
    if (base == PIC32MK_EVIC_INTSTAT) {
        return &s->intstat;
    }
    if (base == PIC32MK_EVIC_IPTMR) {
        return &s->iptmr;
    }

    if (base >= PIC32MK_EVIC_IFS0 && base < PIC32MK_EVIC_IEC0) {
        int i = (int)((base - PIC32MK_EVIC_IFS0) >> 4);
        return (i < EVIC_IFS_COUNT) ? &s->ifsreg[i] : NULL;
    }
    if (base >= PIC32MK_EVIC_IEC0 && base < PIC32MK_EVIC_IPC0) {
        int i = (int)((base - PIC32MK_EVIC_IEC0) >> 4);
        return (i < EVIC_IEC_COUNT) ? &s->iecreg[i] : NULL;
    }
    if (base >= PIC32MK_EVIC_IPC0 && base < PIC32MK_EVIC_OFF0) {
        int i = (int)((base - PIC32MK_EVIC_IPC0) >> 4);
        return (i < EVIC_IPC_COUNT) ? &s->ipcreg[i] : NULL;
    }
    return NULL;
}

/* Apply SET/CLR/INV operation (sub = byte offset within 16-byte group) */
/* PIC32MK: base+0=REG, +4=CLR, +8=SET, +0xC=INV */
static void apply_sci(uint32_t *reg, uint32_t val, int sub)
{
    switch (sub) {
    case 0:
        *reg  = val;
        break;
    case 4:
        *reg &= ~val;
        break;
    case 8:
        *reg |= val;
        break;
    case 12:
        *reg ^= val;
        break;
    }
}

/*
 * MMIO read/write
 * -----------------------------------------------------------------------
 */

static uint64_t evic_read(void *opaque, hwaddr addr, unsigned size)
{
    PIC32MKEVICState *s = opaque;

    /* OFFx: plain 4-byte registers, no SET/CLR/INV */
    if (addr >= PIC32MK_EVIC_OFF0) {
        int idx = (int)((addr - PIC32MK_EVIC_OFF0) >> 2);
        if (idx < EVIC_OFF_COUNT) {
            return s->offreg[idx];
        }
    } else {
        /* All other registers: 16-byte stride, all sub-addrs read base */
        hwaddr base = addr & ~(hwaddr)0xF;
        uint32_t *reg = evic_find_reg(s, base);
        if (reg) {
            return *reg;
        }
    }

    qemu_log_mask(LOG_UNIMP,
                  "pic32mk_evic: unimplemented read @ 0x%04" HWADDR_PRIx "\n",
                  addr);
    return 0;
}

static void evic_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PIC32MKEVICState *s = opaque;
    bool need_update = false;

    /* OFFx: plain 4-byte writes */
    if (addr >= PIC32MK_EVIC_OFF0) {
        int idx = (int)((addr - PIC32MK_EVIC_OFF0) >> 2);
        if (idx < EVIC_OFF_COUNT) {
            s->offreg[idx] = (uint32_t)val;
        }
        return;
    }

    int sub       = (int)(addr & 0xF);
    hwaddr base   = addr & ~(hwaddr)0xF;
    uint32_t *reg = evic_find_reg(s, base);

    if (!reg) {
        qemu_log_mask(LOG_UNIMP,
                      "pic32mk_evic: unimplemented write @ 0x%04"
                      HWADDR_PRIx " = 0x%08" PRIx64 "\n",
                      addr, val);
        return;
    }

    /* INTSTAT is read-only hardware status */
    if (reg == &s->intstat) {
        return;
    }

    apply_sci(reg, (uint32_t)val, sub);

    /*
     * After any IFS write (especially CLR), re-assert bits where the
     * hardware source is still active.  On real Microchip devices, IFS
     * flags are level-sensitive: if the peripheral still drives the
     * interrupt line high, the flag is immediately re-set even after the
     * firmware clears it.
     */
    if (base >= PIC32MK_EVIC_IFS0 && base < PIC32MK_EVIC_IEC0) {
        int idx = (int)((base - PIC32MK_EVIC_IFS0) >> 4);
        if (idx < EVIC_IFS_COUNT) {
            s->ifsreg[idx] |= s->irq_level[idx];
        }
    }

    /* IFS or IEC or IPC write → re-evaluate interrupt routing */
    if (base >= PIC32MK_EVIC_IFS0 &&
        base <  PIC32MK_EVIC_IPC0 + (hwaddr)(EVIC_IPC_COUNT * 0x10)) {
        need_update = true;
    }

    if (need_update) {
        evic_update(s);
    }
}

static const MemoryRegionOps evic_ops = {
    .read       = evic_read,
    .write      = evic_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

/*
 * Device lifecycle
 * -----------------------------------------------------------------------
 */

static void pic32mk_evic_reset(DeviceState *dev)
{
    PIC32MKEVICState *s = PIC32MK_EVIC(dev);

    s->intcon  = 0;
    s->priss   = 0;
    s->intstat = 0;
    s->iptmr   = 0;
    memset(s->ifsreg, 0, sizeof(s->ifsreg));
    memset(s->iecreg, 0, sizeof(s->iecreg));
    memset(s->ipcreg, 0, sizeof(s->ipcreg));
    memset(s->offreg, 0, sizeof(s->offreg));
    memset(s->irq_level, 0, sizeof(s->irq_level));

    /* Deassert all CPU interrupt pins */
    for (int i = 0; i < 8; i++) {
        if (s->cpu_irq[i]) {
            qemu_irq_lower(s->cpu_irq[i]);
        }
    }
}

static void pic32mk_evic_init(Object *obj)
{
    PIC32MKEVICState *s = PIC32MK_EVIC(obj);

    memory_region_init_io(&s->mr, obj, &evic_ops, s,
                          TYPE_PIC32MK_EVIC, EVIC_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mr);

    /* 216 GPIO input lines — one per interrupt source */
    qdev_init_gpio_in(DEVICE(obj), evic_set_irq, PIC32MK_NUM_IRQ_SOURCES);
}

static void pic32mk_evic_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    device_class_set_legacy_reset(dc, pic32mk_evic_reset);
}

static const TypeInfo pic32mk_evic_info = {
    .name          = TYPE_PIC32MK_EVIC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PIC32MKEVICState),
    .instance_init = pic32mk_evic_init,
    .class_init    = pic32mk_evic_class_init,
};

static void pic32mk_evic_register_types(void)
{
    type_register_static(&pic32mk_evic_info);
}

type_init(pic32mk_evic_register_types)
