/*
 * Microchip PIC32MK — Configuration / PMD / SYSKEY registers
 * Datasheet: DS60001519E §6
 *
 * Models the CFG register block at 0xBF800000–0xBF80011F:
 *   CFGCON, SYSKEY, PMD1–PMD7, CFGCON2.
 *
 * SYSKEY implements the unlock state machine:
 *   Write 0x00000000 → LOCKED
 *   Write 0xAA996655 → FIRST_KEY
 *   Write 0x556699AA when FIRST_KEY → UNLOCKED
 *   Any wrong write → LOCKED
 *   SYSKEY always reads 0.
 *
 * PMD1–PMD7 writes are gated by CFGCON.PMDLOCK (bit 29).
 * When PMDLOCK=1, writes to PMDx are silently ignored.
 *
 * Copyright (c) 2026 QEMU contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/core/sysbus.h"
#include "hw/mips/pic32mk.h"

#define TYPE_PIC32MK_CFG "pic32mk-cfg"
OBJECT_DECLARE_SIMPLE_TYPE(PIC32MKCFGState, PIC32MK_CFG)

/* SYSKEY unlock state machine */
enum {
    SYSKEY_LOCKED = 0,
    SYSKEY_FIRST_KEY,
    SYSKEY_UNLOCKED,
};

typedef struct PIC32MKCFGState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    uint32_t cfgcon;
    uint32_t cfgcon2;
    uint32_t checon;                    /* Prefetch Cache Control */
    uint32_t pmd[PIC32MK_PMD_COUNT];    /* PMD1–PMD7 */
    uint8_t  syskey_state;              /* SYSKEY unlock FSM */
} PIC32MKCFGState;

/* SET/CLR/INV helper */
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
    }
}

/*
 * Read handler
 * -----------------------------------------------------------------------
 */
static uint64_t pic32mk_cfg_read(void *opaque, hwaddr addr, unsigned size)
{
    PIC32MKCFGState *s = PIC32MK_CFG(opaque);
    hwaddr base = addr & ~0xFu;

    switch (base) {
    case PIC32MK_CFGCON:
        return s->cfgcon;

    case PIC32MK_SYSKEY:
        /* SYSKEY always reads 0 */
        return 0;

    case PIC32MK_PMD1:
        return s->pmd[0];
    case PIC32MK_PMD2:
        return s->pmd[1];
    case PIC32MK_PMD3:
        return s->pmd[2];
    case PIC32MK_PMD4:
        return s->pmd[3];
    case PIC32MK_PMD5:
        return s->pmd[4];
    case PIC32MK_PMD6:
        return s->pmd[5];
    case PIC32MK_PMD7:
        return s->pmd[6];

    case PIC32MK_CFGCON2:
        return s->cfgcon2;

    case PIC32MK_CHECON:
        return s->checon;

    default:
        qemu_log_mask(LOG_UNIMP,
                      "pic32mk-cfg: unimplemented read @ 0x%03" HWADDR_PRIx "\n",
                      addr);
        return 0;
    }
}

/*
 * Write handler
 * -----------------------------------------------------------------------
 */
static void pic32mk_cfg_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned size)
{
    PIC32MKCFGState *s = PIC32MK_CFG(opaque);
    hwaddr base = addr & ~0xFu;
    unsigned sub = addr & 0xCu;
    uint32_t v32 = (uint32_t)val;

    /* Handle SYSKEY — no SET/CLR/INV, always direct write */
    if (base == PIC32MK_SYSKEY) {
        switch (s->syskey_state) {
        case SYSKEY_LOCKED:
            if (v32 == 0xAA996655u) {
                s->syskey_state = SYSKEY_FIRST_KEY;
            }
            break;
        case SYSKEY_FIRST_KEY:
            if (v32 == 0x556699AAu) {
                s->syskey_state = SYSKEY_UNLOCKED;
            } else {
                s->syskey_state = SYSKEY_LOCKED;
            }
            break;
        case SYSKEY_UNLOCKED:
            /* Any write re-locks (clearing 0x00000000 or random) */
            if (v32 != 0xAA996655u && v32 != 0x556699AAu) {
                s->syskey_state = SYSKEY_LOCKED;
            }
            break;
        }
        return;
    }

    switch (base) {
    case PIC32MK_CFGCON:
        s->cfgcon = apply_sci(s->cfgcon, v32, sub);
        break;

    case PIC32MK_CFGCON2:
        s->cfgcon2 = apply_sci(s->cfgcon2, v32, sub);
        break;

    case PIC32MK_CHECON:
        s->checon = apply_sci(s->checon, v32, sub);
        break;

    /* PMD1–PMD7: gated by PMDLOCK */
    case PIC32MK_PMD1:
    case PIC32MK_PMD2:
    case PIC32MK_PMD3:
    case PIC32MK_PMD4:
    case PIC32MK_PMD5:
    case PIC32MK_PMD6:
    case PIC32MK_PMD7:
        if (s->cfgcon & PIC32MK_CFGCON_PMDLOCK) {
            /* Writes rejected when PMDLOCK = 1 */
            return;
        }
        s->pmd[(base - PIC32MK_PMD1) / 0x10] =
            apply_sci(s->pmd[(base - PIC32MK_PMD1) / 0x10], v32, sub);
        break;

    default:
        qemu_log_mask(LOG_UNIMP,
                      "pic32mk-cfg: unimplemented write @ 0x%03" HWADDR_PRIx
                      " = 0x%08x\n", addr, v32);
        break;
    }
}

static const MemoryRegionOps pic32mk_cfg_ops = {
    .read       = pic32mk_cfg_read,
    .write      = pic32mk_cfg_write,
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

static void pic32mk_cfg_realize(DeviceState *dev, Error **errp)
{
    PIC32MKCFGState *s = PIC32MK_CFG(dev);
    memory_region_init_io(&s->mmio, OBJECT(s), &pic32mk_cfg_ops, s,
                          "pic32mk-cfg", PIC32MK_CFG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
}

static void pic32mk_cfg_reset_hold(Object *obj, ResetType type)
{
    PIC32MKCFGState *s = PIC32MK_CFG(obj);
    s->cfgcon  = 0;
    s->cfgcon2 = 0;
    s->checon  = 0;
    s->syskey_state = SYSKEY_LOCKED;
    for (int i = 0; i < PIC32MK_PMD_COUNT; i++) {
        s->pmd[i] = 0;
    }
}

/*
 * QOM boilerplate
 * -----------------------------------------------------------------------
 */

static void pic32mk_cfg_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    dc->realize = pic32mk_cfg_realize;
    dc->desc    = "PIC32MK Configuration / PMD / SYSKEY";
    rc->phases.hold = pic32mk_cfg_reset_hold;
}

static const TypeInfo pic32mk_cfg_info = {
    .name          = TYPE_PIC32MK_CFG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PIC32MKCFGState),
    .class_init    = pic32mk_cfg_class_init,
};

static void pic32mk_cfg_register_types(void)
{
    type_register_static(&pic32mk_cfg_info);
}

type_init(pic32mk_cfg_register_types)
