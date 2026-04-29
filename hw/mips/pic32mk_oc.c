/*
 * PIC32MK Output Compare × 16 (OC1–OC16)
 * Datasheet: DS60001519E, §19
 *
 * Diagnostic-only emulation: when firmware enables an OC instance
 * (OCxCON.ON goes 0→1), the emulator emits a 12-byte binary event
 * to an optional chardev ("oc-events") for GUI waveform rendering,
 * and also writes a human-readable fallback to qemu_log().
 *
 * Event message format (16 bytes, little-endian):
 *   [0]     index   — OC instance number (1–16)
 *   [1]     enabled — 1 = enabled, 0 = disabled
 *   [2]     ocm     — OCM mode bits [2:0]
 *   [3]     flags   — bit0=OC32, bit1=OCTSEL
 *   [4..7]  ocr     — OCxR  (primary compare), uint32 LE
 *   [8..11] ocrs    — OCxRS (secondary compare), uint32 LE
 *   [12..15] pr     — timer PRx period register, uint32 LE (0 on disable)
 *
 * Consumer duty/pulse-width formula by mode:
 *   PWM (ocm=6,7):              pulse_pct = ocrs / pr * 100
 *   Single/continuous (ocm=4,5): pulse_pct = (ocrs - ocr) / pr * 100
 *
 * Register layout within each 0x200-byte block
 * (registers have SET/CLR/INV sub-regs at +4/+8/+C):
 *   OCxCON  +0x00  Control: ON, SIDL, OC32, OCFLT, OCTSEL, OCM
 *   OCxR    +0x10  Primary compare value
 *   OCxRS   +0x20  Secondary compare value
 *
 * Copyright (c) 2026 QEMU contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-properties.h"
#include "hw/mips/pic32mk.h"
#include "chardev/char.h"
#include "system/address-spaces.h"

/* OCM mode decode table (bits 2:0 of OCxCON) */
static const char *ocm_mode_names[] = {
    [0] = "Disabled",
    [1] = "High on match, then low",
    [2] = "Init low, force high",
    [3] = "Toggle on match",
    [4] = "Single pulse",
    [5] = "Continuous pulses",
    [6] = "PWM (no fault)",
    [7] = "PWM (fault enabled)",
};

/*
 * Timer PRx KSEG1 addresses for OCTSEL=0 (Timer2) and OCTSEL=1 (Timer3).
 * OC1-OC9 use the peripheral-1 timer block (SFR base 0xBF820000).
 * T2 base = 0xBF820200, PR2 = T2 base + 0x20 = 0xBF820220
 * T3 base = 0xBF820400, PR3 = T3 base + 0x20 = 0xBF820420
 * TODO: OC10-OC16 use peripheral-2 block timers (T4-T9) — extend table if needed.
 */
static const uint32_t oc_timer_pr_addr[2] = {
    0x1F820220u,   /* OCTSEL=0 → Timer2 PRx (physical: SFR_BASE + T2_OFFSET + 0x20) */
    0x1F820420u,   /* OCTSEL=1 → Timer3 PRx (physical: SFR_BASE + T3_OFFSET + 0x20) */
};

/*
 * Device state
 * -----------------------------------------------------------------------
 */

#define TYPE_PIC32MK_OC  "pic32mk-oc"
OBJECT_DECLARE_SIMPLE_TYPE(PIC32MKOCState, PIC32MK_OC)

struct PIC32MKOCState {
    SysBusDevice parent_obj;
    MemoryRegion mr;

    uint32_t con;   /* OCxCON */
    uint32_t r;     /* OCxR   */
    uint32_t rs;    /* OCxRS  */

    qemu_irq irq;
    uint8_t  index;    /* 1-16 */
    Chardev *chr_out;  /* optional chardev for waveform event streaming */
};

/*
 * MMIO helpers
 * -----------------------------------------------------------------------
 */

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

static uint32_t *oc_find_reg(PIC32MKOCState *s, hwaddr base)
{
    switch (base) {
    case PIC32MK_OCxCON:
        return &s->con;
    case PIC32MK_OCxR:
        return &s->r;
    case PIC32MK_OCxRS:
        return &s->rs;
    default:
        return NULL;
    }
}

/*
 * Timer PRx read — needed for duty-cycle in event messages
 * -----------------------------------------------------------------------
 */

static uint32_t oc_read_pr(PIC32MKOCState *s)
{
    int sel = !!(s->con & PIC32MK_OCCON_OCTSEL);
    MemTxResult res;
    return address_space_ldl_le(&address_space_memory,
                                oc_timer_pr_addr[sel],
                                MEMTXATTRS_UNSPECIFIED, &res);
}

/*
 * Event emission — qemu_log fallback + optional chardev binary stream
 * -----------------------------------------------------------------------
 */

static void oc_emit_event(PIC32MKOCState *s, bool enabled)
{
    uint32_t ocm   = (s->con & PIC32MK_OCCON_OCM_MASK) >> PIC32MK_OCCON_OCM_SHIFT;
    bool     oc32  = !!(s->con & PIC32MK_OCCON_OC32);
    bool     octsel = !!(s->con & PIC32MK_OCCON_OCTSEL);

    /* Human-readable fallback always goes to qemu_log */
    if (enabled) {
        qemu_log("pic32mk_oc: OC%u enabled — mode=%s, OC32=%u, OCTSEL=%u, "
                 "OCxR=0x%08X, OCxRS=0x%08X\n",
                 s->index, ocm_mode_names[ocm], oc32, octsel, s->r, s->rs);
    } else {
        qemu_log("pic32mk_oc: OC%u disabled\n", s->index);
    }

    if (!s->chr_out) {
        return;
    }

    uint32_t pr = enabled ? oc_read_pr(s) : 0;

    uint8_t  msg[16];
    uint32_t ocr_le  = cpu_to_le32(s->r);
    uint32_t ocrs_le = cpu_to_le32(s->rs);
    uint32_t pr_le   = cpu_to_le32(pr);

    msg[0] = s->index;
    msg[1] = enabled ? 1 : 0;
    msg[2] = (uint8_t)ocm;
    msg[3] = (uint8_t)(oc32 | (octsel << 1));
    memcpy(&msg[4],  &ocr_le,  4);
    memcpy(&msg[8],  &ocrs_le, 4);
    memcpy(&msg[12], &pr_le,   4);
    qemu_chr_write_all(s->chr_out, msg, sizeof(msg));
}

/* Public setter called by pic32mk.c after device realisation. */
void pic32mk_oc_set_chardev(DeviceState *dev, Chardev *chr)
{
    PIC32MK_OC(dev)->chr_out = chr;
}

/*
 * MMIO read/write
 * -----------------------------------------------------------------------
 */

static uint64_t oc_read(void *opaque, hwaddr addr, unsigned size)
{
    PIC32MKOCState *s = opaque;
    hwaddr base = addr & ~(hwaddr)0xF;
    uint32_t *reg = oc_find_reg(s, base);

    if (reg) {
        return *reg;
    }

    qemu_log_mask(LOG_UNIMP,
                  "pic32mk_oc: OC%u unimplemented read @ 0x%04" HWADDR_PRIx "\n",
                  s->index, addr);
    return 0;
}

static void oc_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PIC32MKOCState *s = opaque;
    int sub       = (int)(addr & 0xF);
    hwaddr base   = addr & ~(hwaddr)0xF;
    uint32_t *reg = oc_find_reg(s, base);

    if (!reg) {
        qemu_log_mask(LOG_UNIMP,
                      "pic32mk_oc: OC%u unimplemented write @ 0x%04"
                      HWADDR_PRIx " = 0x%08" PRIx64 "\n",
                      s->index, addr, val);
        return;
    }

    bool was_on = !!(s->con & PIC32MK_OCCON_ON);
    apply_sci(reg, (uint32_t)val, sub);

    /* Handle ON bit transitions for OCxCON writes */
    if (base == PIC32MK_OCxCON) {
        bool now_on = !!(s->con & PIC32MK_OCCON_ON);
        if (!was_on && now_on) {
            oc_emit_event(s, true);
        } else if (was_on && !now_on) {
            oc_emit_event(s, false);
        }
    }
}

static const MemoryRegionOps oc_ops = {
    .read       = oc_read,
    .write      = oc_write,
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

static void pic32mk_oc_reset(DeviceState *dev)
{
    PIC32MKOCState *s = PIC32MK_OC(dev);
    s->con = 0;
    s->r   = 0;
    s->rs  = 0;
}

static void pic32mk_oc_init(Object *obj)
{
    PIC32MKOCState *s = PIC32MK_OC(obj);

    memory_region_init_io(&s->mr, obj, &oc_ops, s,
                          TYPE_PIC32MK_OC, PIC32MK_OC_BLOCK_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mr);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static Property pic32mk_oc_properties[] = {
    DEFINE_PROP_UINT8("index", PIC32MKOCState, index, 1),
};

static void pic32mk_oc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    device_class_set_legacy_reset(dc, pic32mk_oc_reset);
    device_class_set_props(dc, pic32mk_oc_properties);
}

static const TypeInfo pic32mk_oc_info = {
    .name          = TYPE_PIC32MK_OC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PIC32MKOCState),
    .instance_init = pic32mk_oc_init,
    .class_init    = pic32mk_oc_class_init,
};

static void pic32mk_oc_register_types(void)
{
    type_register_static(&pic32mk_oc_info);
}

type_init(pic32mk_oc_register_types)
