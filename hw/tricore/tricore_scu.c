/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU TriCore System Control Unit (SCU)
 *
 * Copyright (c) 2017 David Brenken <david.brenken@efs-auto.de>
 * Copyright (c) 2024 Infineon Technologies AG
 */

#include "qemu/osdep.h"
#include "hw/tricore/tricore_scu.h"
#include "qemu/log.h"
#include "qemu/module.h"

/* Register offsets */
enum {
    A_OSCCON        = 0x10,
    A_PLLSTAT       = 0x14,
    A_PLLCON0       = 0x18,
    A_PLLCON1       = 0x1C,
    A_PLLCON2       = 0x20,
    A_PLLERAYSTAT   = 0x24,
    A_CCUCON0       = 0x30,
    A_CCUCON1       = 0x34,
    A_CCUCON2       = 0x40,
    A_CCUCON3       = 0x44,
    A_CCUCON4       = 0x48,
    A_CCUCON5       = 0x4C,
    A_WDTSCON0      = 0xF0,
    A_WDTCPU0CON0   = 0x100,
    A_CHIPID        = 0x140,
};

/* OSCCON bit positions (iLLD IfxScu_bf.h) */
#define MASK_OSCCON_PLLLV               0x00000002  /* bit 1 */
#define MASK_OSCCON_PLLHV               0x00000100  /* bit 8 */

/* PLLCON0 fields */
#define MASK_PLLCON0_SETFINDIS          0x00000010
#define MASK_PLLCON0_CLRFINDIS          0x00000020

/* PLLSTAT fields */
#define MASK_PLLSTAT_VCOLOCK            0x00000004
#define MASK_PLLSTAT_FINDIS             0x00000008

/* CCUCON fields */
#define MASK_CCUCON0_UP                 0x40000000
#define MASK_CCUCON1_UP                 0x40000000
#define MASK_CCUCON5_UP                 0x40000000

/* Reset values */
#define RESET_TRICORE_OSCCON            0x00000112
#define RESET_TRICORE_PLLSTAT           0x00000038
#define RESET_TRICORE_PLLCON0           0x0001C600
#define RESET_TRICORE_PLLCON1           0x0002020F
#define RESET_TRICORE_PLLCON2           0x00000000
#define RESET_TRICORE_PLLERAYSTAT       0x00000038
#define RESET_TRICORE_PLLERAYCON0       0x00012E00
#define RESET_TRICORE_PLLERAYCON1       0x000F020F
#define RESET_TRICORE_CCUCON0           0x01120148
#define RESET_TRICORE_CCUCON1           0x00002211
#define RESET_TRICORE_CCUCON2           0x00000002
#define RESET_TRICORE_CCUCON3           0x00000000
#define RESET_TRICORE_CCUCON4           0x00000000
#define RESET_TRICORE_CCUCON5           0x00000041
#define RESET_TRICORE_FDR               0x00000000
#define RESET_TRICORE_EXTCON            0x00000000
#define RESET_TRICORE_WDTSCON0          0xFFFC000E
#define RESET_TRICORE_WDTSCON1          0x00000000
#define RESET_TRICORE_WDTCPU0CON0      0xFFFC000E

static void tricore_scu_update_ccucon(TriCoreSCUState *s)
{
    if ((s->ccucon[0] & MASK_CCUCON0_UP) ||
        (s->ccucon[1] & MASK_CCUCON1_UP) ||
        (s->ccucon[5] & MASK_CCUCON5_UP)) {
        s->ccucon[0] &= ~(MASK_CCUCON0_UP | (1U << 31));
        s->ccucon[1] &= ~(MASK_CCUCON1_UP | (1U << 31));
        s->ccucon[5] &= ~(MASK_CCUCON5_UP | (1U << 31));
    }
}

static uint32_t tricore_scu_reg_read(TriCoreSCUState *s, hwaddr offset)
{
    switch (offset) {
    case A_OSCCON:
        return s->osccon;
    case A_PLLSTAT:
        /*
         * Report PLL as always locked: VCOLOCK=1, K1RDY=1, K2RDY=1.
         * Firmware polls these bits during clock initialisation.
         */
        return 0x00000077;
    case A_PLLCON0:
        return s->pllcon[0];
    case A_PLLCON1:
        return s->pllcon[1];
    case A_PLLCON2:
        return s->pllcon[2];
    case A_PLLERAYSTAT:
        return 0x00000077;
    case A_CCUCON0:
        return s->ccucon[0];
    case A_CCUCON1:
        return s->ccucon[1];
    case A_CCUCON2:
        return s->ccucon[2];
    case A_CCUCON3:
        return s->ccucon[3];
    case A_CCUCON4:
        return s->ccucon[4];
    case A_CCUCON5:
        return s->ccucon[5];
    case A_WDTSCON0:
        return s->wdtscon1;
    case A_WDTCPU0CON0:
        return s->wdtcpu0con0;
    case A_CHIPID:
        return 0x47477172 | (1U << 31);
    default:
        if (offset < SCU_REG_SIZE) {
            return s->regs[offset / 4];
        }
        qemu_log_mask(LOG_GUEST_ERROR,
                      "tricore_scu: read at bad offset 0x%" HWADDR_PRIx "\n",
                      offset);
        return 0;
    }
}

static uint64_t tricore_scu_read(void *opaque, hwaddr offset,
                                 unsigned size)
{
    TriCoreSCUState *s = TRICORE_SCU(opaque);

    return extract32(tricore_scu_reg_read(s, offset & ~3),
                     (offset & 3) * 8, size * 8);
}

static void tricore_scu_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned size)
{
    TriCoreSCUState *s = TRICORE_SCU(opaque);

    /* 8/16-bit writes: merge into the current 32-bit register value */
    if (size < 4) {
        value = deposit32(tricore_scu_reg_read(s, offset & ~3),
                          (offset & 3) * 8, size * 8, value);
        offset &= ~3;
    }

    switch (offset) {
    case A_OSCCON:
        s->osccon = (uint32_t)value;
        /*
         * Real silicon raises PLLHV and PLLLV once the external
         * oscillator is stable.  Mark stable immediately.
         */
        s->osccon |= MASK_OSCCON_PLLHV | MASK_OSCCON_PLLLV;
        break;
    case A_PLLCON0:
        s->pllcon[0] = (uint32_t)value;
        if (s->pllcon[0] & MASK_PLLCON0_SETFINDIS) {
            s->pllstat |= MASK_PLLSTAT_FINDIS;
        }
        if (s->pllcon[0] & MASK_PLLCON0_CLRFINDIS) {
            s->pllstat &= ~MASK_PLLSTAT_FINDIS;
        }
        s->pllstat |= MASK_PLLSTAT_VCOLOCK;
        break;
    case A_PLLCON1:
        s->pllcon[1] = (uint32_t)value;
        s->pllstat |= MASK_PLLSTAT_VCOLOCK;
        break;
    case A_PLLCON2:
        s->pllcon[2] = (uint32_t)value;
        s->pllstat |= MASK_PLLSTAT_VCOLOCK;
        break;
    case A_CCUCON0:
        s->ccucon[0] = (uint32_t)value;
        tricore_scu_update_ccucon(s);
        s->pllstat |= MASK_PLLSTAT_VCOLOCK;
        break;
    case A_CCUCON1:
        s->ccucon[1] = (uint32_t)value;
        tricore_scu_update_ccucon(s);
        s->pllstat |= MASK_PLLSTAT_VCOLOCK;
        break;
    case A_CCUCON2:
        s->ccucon[2] = (uint32_t)value & ~(1U << 31);
        s->pllstat |= MASK_PLLSTAT_VCOLOCK;
        break;
    case A_CCUCON3:
        s->ccucon[3] = (uint32_t)value & ~(1U << 31);
        s->pllstat |= MASK_PLLSTAT_VCOLOCK;
        break;
    case A_CCUCON4:
        s->ccucon[4] = (uint32_t)value & ~(1U << 31);
        s->pllstat |= MASK_PLLSTAT_VCOLOCK;
        break;
    case A_CCUCON5:
        s->ccucon[5] = (uint32_t)value;
        tricore_scu_update_ccucon(s);
        s->pllstat |= MASK_PLLSTAT_VCOLOCK;
        break;
    case A_WDTSCON0:
        s->wdtscon1 = (uint32_t)value;
        break;
    case A_WDTCPU0CON0:
        s->wdtcpu0con0 = (uint32_t)value;
        break;
    default:
        if (offset < SCU_REG_SIZE) {
            s->regs[offset / 4] = (uint32_t)value & ~(1U << 31);
            return;
        }
        qemu_log_mask(LOG_GUEST_ERROR,
                      "tricore_scu: write at bad offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps tricore_scu_ops = {
    .read = tricore_scu_read,
    .write = tricore_scu_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void tricore_scu_reset_hold(Object *obj, ResetType type)
{
    TriCoreSCUState *s = TRICORE_SCU(obj);

    s->ccucon[0] = RESET_TRICORE_CCUCON0;
    s->ccucon[1] = RESET_TRICORE_CCUCON1;
    s->ccucon[2] = RESET_TRICORE_CCUCON2;
    s->ccucon[3] = RESET_TRICORE_CCUCON3;
    s->ccucon[4] = RESET_TRICORE_CCUCON4;
    s->ccucon[5] = RESET_TRICORE_CCUCON5;
    s->fdr = RESET_TRICORE_FDR;
    s->extcon = RESET_TRICORE_EXTCON;

    s->osccon = RESET_TRICORE_OSCCON | MASK_OSCCON_PLLHV | MASK_OSCCON_PLLLV;

    s->pllcon[0] = RESET_TRICORE_PLLCON0;
    s->pllcon[1] = RESET_TRICORE_PLLCON1;
    s->pllcon[2] = RESET_TRICORE_PLLCON2;
    s->plleraycon[0] = RESET_TRICORE_PLLERAYCON0;
    s->plleraycon[1] = RESET_TRICORE_PLLERAYCON1;
    s->plleraystat = RESET_TRICORE_PLLERAYSTAT;
    s->pllstat = RESET_TRICORE_PLLSTAT | MASK_PLLSTAT_VCOLOCK;

    s->wdtcpu0con0 = RESET_TRICORE_WDTCPU0CON0;
    s->wdtscon0 = RESET_TRICORE_WDTSCON0;
    s->wdtscon1 = RESET_TRICORE_WDTSCON1;
}

static void tricore_scu_init(Object *obj)
{
    TriCoreSCUState *s = TRICORE_SCU(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &tricore_scu_ops, s,
                          TYPE_TRICORE_SCU, SCU_REG_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void tricore_scu_class_init(ObjectClass *klass, const void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = tricore_scu_reset_hold;
}

static const TypeInfo tricore_scu_info = {
    .name = TYPE_TRICORE_SCU,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TriCoreSCUState),
    .instance_init = tricore_scu_init,
    .class_init = tricore_scu_class_init,
};

static void tricore_scu_register_types(void)
{
    type_register_static(&tricore_scu_info);
}

type_init(tricore_scu_register_types)
