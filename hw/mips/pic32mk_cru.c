/*
 * Microchip PIC32MK — Clock Reference Unit (CRU)
 * Datasheet: DS60001519E §9
 *
 * Models the CRU register block at 0xBF801200–0xBF80139F:
 *   OSCCON, OSCTUN, SPLLCON, UPLLCON, RCON, RSWRST, RNMICON, PWRCON,
 *   REFO1–4 CON/TRIM, PB1–7 DIV, CLKSTAT.
 *
 * All oscillator ready bits are returned set immediately — no PLL lock
 * timing is simulated.  Register values are stored but no dynamic clock
 * frequency propagation is performed; peripherals use the fixed
 * PIC32MK_CPU_HZ constant.
 *
 * RCON (§7) is also handled here since it falls within the CRU address
 * range (offset 0x40 = physical 0xBF801240).
 *
 * Copyright (c) 2026 QEMU contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/core/sysbus.h"
#include "hw/mips/pic32mk.h"
#include "system/runstate.h"

#define TYPE_PIC32MK_CRU "pic32mk-cru"
OBJECT_DECLARE_SIMPLE_TYPE(PIC32MKCRUState, PIC32MK_CRU)

typedef struct PIC32MKCRUState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    /* Oscillator / PLL */
    uint32_t osccon;
    uint32_t osctun;
    uint32_t spllcon;
    uint32_t upllcon;

    /* Reset control (RCON / RSWRST / RNMICON / PWRCON) */
    uint32_t rcon;
    uint32_t rswrst;
    uint32_t rnmicon;
    uint32_t pwrcon;

    /* Reference clock outputs 1–4 */
    uint32_t refo_con[PIC32MK_CRU_NREFO];
    uint32_t refo_trim[PIC32MK_CRU_NREFO];

    /* Peripheral bus clock dividers 1–7 */
    uint32_t pbdiv[PIC32MK_CRU_NPB];
} PIC32MKCRUState;

/*
 * SET/CLR/INV helper — returns the new register value without applying it,
 * so the caller can enforce read-only masks.
 * sub == 0x0: direct write, 0x4: SET, 0x8: CLR, 0xC: INV
 * -----------------------------------------------------------------------
 */
static uint32_t apply_sci(uint32_t old, uint32_t val, unsigned sub)
{
    switch (sub) {
    case 0x0:
        return val;
    case 0x4:
        return old | val;
    case 0x8:
        return old & ~val;
    case 0xC:
        return old ^ val;
    default:
        return old;
        /* unreachable */;
    }
}

/*
 * Read handler
 * -----------------------------------------------------------------------
 */
static uint64_t pic32mk_cru_read(void *opaque, hwaddr addr, unsigned size)
{
    PIC32MKCRUState *s = PIC32MK_CRU(opaque);
    hwaddr base = addr & ~0xFu;     /* register base (strip SET/CLR/INV) */

    switch (base) {
    case PIC32MK_CRU_OSCCON:
        return s->osccon;
    case PIC32MK_CRU_OSCTUN:
        return s->osctun;
    case PIC32MK_CRU_SPLLCON:
        return s->spllcon;
    case PIC32MK_CRU_UPLLCON:
        return s->upllcon;

    case PIC32MK_CRU_RCON:
        return s->rcon;
    case PIC32MK_CRU_RSWRST:
        return s->rswrst;
    case PIC32MK_CRU_RNMICON:
        return s->rnmicon;
    case PIC32MK_CRU_PWRCON:
        return s->pwrcon;

    /* Reference clock outputs 1–4: CON then TRIM, 0x20 stride */
    case PIC32MK_CRU_REFO1CON:
        return s->refo_con[0];
    case PIC32MK_CRU_REFO1TRIM:
        return s->refo_trim[0];
    case PIC32MK_CRU_REFO2CON:
        return s->refo_con[1];
    case PIC32MK_CRU_REFO2TRIM:
        return s->refo_trim[1];
    case PIC32MK_CRU_REFO3CON:
        return s->refo_con[2];
    case PIC32MK_CRU_REFO3TRIM:
        return s->refo_trim[2];
    case PIC32MK_CRU_REFO4CON:
        return s->refo_con[3];
    case PIC32MK_CRU_REFO4TRIM:
        return s->refo_trim[3];

    /* Peripheral bus dividers — 0x10 stride starting at 0x100 */
    case PIC32MK_CRU_PB1DIV:
        return s->pbdiv[0] | PIC32MK_PBDIV_PBDIVRDY;
    case PIC32MK_CRU_PB2DIV:
        return s->pbdiv[1] | PIC32MK_PBDIV_PBDIVRDY;
    case PIC32MK_CRU_PB3DIV:
        return s->pbdiv[2] | PIC32MK_PBDIV_PBDIVRDY;
    case PIC32MK_CRU_PB4DIV:
        return s->pbdiv[3] | PIC32MK_PBDIV_PBDIVRDY;
    case PIC32MK_CRU_PB5DIV:
        return s->pbdiv[4] | PIC32MK_PBDIV_PBDIVRDY;
    case PIC32MK_CRU_PB6DIV:
        return s->pbdiv[5] | PIC32MK_PBDIV_PBDIVRDY;
    case PIC32MK_CRU_PB7DIV:
        return s->pbdiv[6] | PIC32MK_PBDIV_PBDIVRDY;

    /* CLKSTAT — always report all clocks ready */
    case PIC32MK_CRU_CLKSTAT:
        return PIC32MK_CLKSTAT_ALL_RDY;

    default:
        qemu_log_mask(LOG_UNIMP,
                      "pic32mk-cru: unimplemented read @ 0x%03" HWADDR_PRIx "\n",
                      addr);
        return 0;
    }
}

/*
 * Write handler
 * -----------------------------------------------------------------------
 */
static void pic32mk_cru_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned size)
{
    PIC32MKCRUState *s = PIC32MK_CRU(opaque);
    hwaddr base = addr & ~0xFu;
    unsigned sub = addr & 0xCu;
    uint32_t v32 = (uint32_t)val;

    switch (base) {
    case PIC32MK_CRU_OSCCON:
        s->osccon = apply_sci(s->osccon, v32, sub);
        /*
         * Mirror NOSC → COSC immediately and set SLOCK.
         * Real hardware would do an oscillator switch sequence; in emulation
         * the switch completes instantly.
         */
        if (s->osccon & PIC32MK_OSCCON_OSWEN) {
            uint32_t nosc = (s->osccon & PIC32MK_OSCCON_NOSC_MASK)
                            >> PIC32MK_OSCCON_NOSC_SHIFT;
            s->osccon &= ~(PIC32MK_OSCCON_COSC_MASK | PIC32MK_OSCCON_OSWEN);
            s->osccon |= nosc << PIC32MK_OSCCON_COSC_SHIFT;
        }
        break;

    case PIC32MK_CRU_OSCTUN:
        s->osctun = apply_sci(s->osctun, v32, sub);
        break;

    case PIC32MK_CRU_SPLLCON:
        s->spllcon = apply_sci(s->spllcon, v32, sub);
        break;

    case PIC32MK_CRU_UPLLCON:
        s->upllcon = apply_sci(s->upllcon, v32, sub);
        break;

    case PIC32MK_CRU_RCON:
        s->rcon = apply_sci(s->rcon, v32, sub);
        break;

    case PIC32MK_CRU_RSWRST:
        s->rswrst = apply_sci(s->rswrst, v32, sub);
        if (s->rswrst & 1u) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "pic32mk-cru: software reset triggered (RSWRST)\n");
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        }
        break;

    case PIC32MK_CRU_RNMICON:
        s->rnmicon = apply_sci(s->rnmicon, v32, sub);
        break;

    case PIC32MK_CRU_PWRCON:
        s->pwrcon = apply_sci(s->pwrcon, v32, sub);
        break;

    /* Reference clock outputs 1–4 */
    case PIC32MK_CRU_REFO1CON:
        s->refo_con[0] = apply_sci(s->refo_con[0], v32, sub);
        if (s->refo_con[0] & PIC32MK_REFOCON_ON) {
            s->refo_con[0] |= PIC32MK_REFOCON_ACTIVE;
        } else {
            s->refo_con[0] &= ~PIC32MK_REFOCON_ACTIVE;
        }
        break;
    case PIC32MK_CRU_REFO1TRIM:
        s->refo_trim[0] = apply_sci(s->refo_trim[0], v32, sub);
        break;

    case PIC32MK_CRU_REFO2CON:
        s->refo_con[1] = apply_sci(s->refo_con[1], v32, sub);
        if (s->refo_con[1] & PIC32MK_REFOCON_ON) {
            s->refo_con[1] |= PIC32MK_REFOCON_ACTIVE;
        } else {
            s->refo_con[1] &= ~PIC32MK_REFOCON_ACTIVE;
        }
        break;
    case PIC32MK_CRU_REFO2TRIM:
        s->refo_trim[1] = apply_sci(s->refo_trim[1], v32, sub);
        break;

    case PIC32MK_CRU_REFO3CON:
        s->refo_con[2] = apply_sci(s->refo_con[2], v32, sub);
        if (s->refo_con[2] & PIC32MK_REFOCON_ON) {
            s->refo_con[2] |= PIC32MK_REFOCON_ACTIVE;
        } else {
            s->refo_con[2] &= ~PIC32MK_REFOCON_ACTIVE;
        }
        break;
    case PIC32MK_CRU_REFO3TRIM:
        s->refo_trim[2] = apply_sci(s->refo_trim[2], v32, sub);
        break;

    case PIC32MK_CRU_REFO4CON:
        s->refo_con[3] = apply_sci(s->refo_con[3], v32, sub);
        if (s->refo_con[3] & PIC32MK_REFOCON_ON) {
            s->refo_con[3] |= PIC32MK_REFOCON_ACTIVE;
        } else {
            s->refo_con[3] &= ~PIC32MK_REFOCON_ACTIVE;
        }
        break;
    case PIC32MK_CRU_REFO4TRIM:
        s->refo_trim[3] = apply_sci(s->refo_trim[3], v32, sub);
        break;

    /* Peripheral bus dividers (PBDIVRDY is read-only, masked out) */
    case PIC32MK_CRU_PB1DIV:
        s->pbdiv[0] = apply_sci(s->pbdiv[0], v32, sub) & ~PIC32MK_PBDIV_PBDIVRDY;
        break;
    case PIC32MK_CRU_PB2DIV:
        s->pbdiv[1] = apply_sci(s->pbdiv[1], v32, sub) & ~PIC32MK_PBDIV_PBDIVRDY;
        break;
    case PIC32MK_CRU_PB3DIV:
        s->pbdiv[2] = apply_sci(s->pbdiv[2], v32, sub) & ~PIC32MK_PBDIV_PBDIVRDY;
        break;
    case PIC32MK_CRU_PB4DIV:
        s->pbdiv[3] = apply_sci(s->pbdiv[3], v32, sub) & ~PIC32MK_PBDIV_PBDIVRDY;
        break;
    case PIC32MK_CRU_PB5DIV:
        s->pbdiv[4] = apply_sci(s->pbdiv[4], v32, sub) & ~PIC32MK_PBDIV_PBDIVRDY;
        break;
    case PIC32MK_CRU_PB6DIV:
        s->pbdiv[5] = apply_sci(s->pbdiv[5], v32, sub) & ~PIC32MK_PBDIV_PBDIVRDY;
        break;
    case PIC32MK_CRU_PB7DIV:
        s->pbdiv[6] = apply_sci(s->pbdiv[6], v32, sub) & ~PIC32MK_PBDIV_PBDIVRDY;
        break;

    case PIC32MK_CRU_CLKSTAT:
        /* Read-only register */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pic32mk-cru: write to read-only CLKSTAT ignored\n");
        break;

    default:
        qemu_log_mask(LOG_UNIMP,
                      "pic32mk-cru: unimplemented write @ 0x%03" HWADDR_PRIx
                      " = 0x%08x\n", addr, v32);
        break;
    }
}

static const MemoryRegionOps pic32mk_cru_ops = {
    .read       = pic32mk_cru_read,
    .write      = pic32mk_cru_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

/*
 * Realize / Reset
 * -----------------------------------------------------------------------
 */

static void pic32mk_cru_realize(DeviceState *dev, Error **errp)
{
    PIC32MKCRUState *s = PIC32MK_CRU(dev);
    memory_region_init_io(&s->mmio, OBJECT(s), &pic32mk_cru_ops, s,
                          "pic32mk-cru", PIC32MK_CRU_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
}

static void pic32mk_cru_reset_hold(Object *obj, ResetType type)
{
    PIC32MKCRUState *s = PIC32MK_CRU(obj);

    /* OSCCON: COSC = 001 (SPLL), clock locked */
    s->osccon = (1u << PIC32MK_OSCCON_COSC_SHIFT);
    s->osctun = 0;
    s->spllcon = 0;
    s->upllcon = 0;

    /* RCON: Power-on Reset + Brown-out Reset flags */
    s->rcon = PIC32MK_RCON_POR | PIC32MK_RCON_BOR;
    s->rswrst  = 0;
    s->rnmicon = 0;
    s->pwrcon  = 0;

    for (int i = 0; i < PIC32MK_CRU_NREFO; i++) {
        s->refo_con[i]  = 0;
        s->refo_trim[i] = 0;
    }

    /* PB1 is always on; PB2–7 default ON */
    for (int i = 0; i < PIC32MK_CRU_NPB; i++) {
        s->pbdiv[i] = PIC32MK_PBDIV_ON;
    }
}

/*
 * QOM boilerplate
 * -----------------------------------------------------------------------
 */

static void pic32mk_cru_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    dc->realize = pic32mk_cru_realize;
    dc->desc    = "PIC32MK Clock Reference Unit";
    rc->phases.hold = pic32mk_cru_reset_hold;
}

static const TypeInfo pic32mk_cru_info = {
    .name          = TYPE_PIC32MK_CRU,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PIC32MKCRUState),
    .class_init    = pic32mk_cru_class_init,
};

static void pic32mk_cru_register_types(void)
{
    type_register_static(&pic32mk_cru_info);
}

type_init(pic32mk_cru_register_types)
