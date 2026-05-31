/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU TriCore System Control Unit (SCU)
 *
 * Copyright (c) 2017 David Brenken <david.brenken@efs-auto.de>
 * Copyright (c) 2024 Infineon Technologies AG
 */

#ifndef HW_TRICORE_SCU_H
#define HW_TRICORE_SCU_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_TRICORE_SCU "tricore_scu"
OBJECT_DECLARE_SIMPLE_TYPE(TriCoreSCUState, TRICORE_SCU)

/* OSCCON bit positions (iLLD IfxScu_bf.h) */
#define MASK_OSCCON_PLLLV               0x00000002  /* bit 1 */
#define MASK_OSCCON_PLLHV               0x00000100  /* bit 8 */

/* PLLCON0 fields */
#define MASK_PLLCON0_VCOBYP             0x00000001
#define MASK_PLLCON0_SETFINDIS          0x00000010
#define MASK_PLLCON0_CLRFINDIS          0x00000020
#define MASK_PLLCON0_NDIV               0x0000FE00
#define MASK_PLLCON0_PDIV               0x0F000000

/* PLLCON1 fields */
#define MASK_PLLCON1_K1DIV              0x007F0000
#define MASK_PLLCON1_K2DIV              0x0000003F

/* PLLSTAT fields */
#define MASK_PLLSTAT_VCOBYST            0x00000001
#define MASK_PLLSTAT_VCOLOCK            0x00000004
#define MASK_PLLSTAT_FINDIS             0x00000008

/* CCUCON fields */
#define MASK_CCUCON0_SRIDIV             0x00000F00
#define MASK_CCUCON0_SPBDIV             0x000F0000
#define MASK_CCUCON0_UP                 0x40000000
#define MASK_CCUCON1_STMDIV             0x00000F00
#define MASK_CCUCON1_INSEL              0x30000000
#define MASK_CCUCON1_INSEL_BACKUP       0x00000000
#define MASK_CCUCON1_INSEL_OSC0         0x10000000
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

/* Clock frequencies */
#define SCU_FBACKUP                     100000000
#define SCU_XTAL1                       20000000

#define SCU_REG_SIZE                    0x1000
#define SCU_NUM_REGS                    (SCU_REG_SIZE / 4)

struct TriCoreSCUState {
    /* private */
    SysBusDevice parent_obj;

    /* public */
    MemoryRegion iomem;

    /* CCU registers */
    uint32_t osccon;
    uint32_t pllstat;
    uint32_t pllcon[3];
    uint32_t plleraystat;
    uint32_t plleraycon[2];
    uint32_t ccucon[6];
    uint32_t fdr;
    uint32_t extcon;

    /* Watchdog stub registers */
    uint32_t wdtscon0;
    uint32_t wdtscon1;
    uint32_t wdtcpu0con0;

    /* Backing store for remaining offsets */
    uint32_t regs[SCU_NUM_REGS];
};

#endif
