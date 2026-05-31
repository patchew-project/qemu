/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU TriCore System Timer Module (STM)
 *
 * Copyright (c) 2024 Infineon Technologies AG
 */

#ifndef HW_TRICORE_STM_H
#define HW_TRICORE_STM_H

#include "hw/core/clock.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"
#include "qemu/timer.h"

#define TYPE_TRICORE_STM "tricore_stm"
OBJECT_DECLARE_SIMPLE_TYPE(TriCoreSTMState, TRICORE_STM)

#define MASK_ICR_CMP0EN     0x01
#define MASK_ICR_CMP0IR     0x02
#define MASK_ICR_CMP1EN     0x10
#define MASK_ICR_CMP1IR     0x20

#define MASK_ISCR_CMP0IRR   0x1
#define MASK_ISCR_CMP0IRS   0x2
#define MASK_ISCR_CMP1IRR   0x4
#define MASK_ISCR_CMP1IRS   0x8

#define MASK_CMCON_MSIZE0   0x1F
#define MASK_CMCON_MSTART0  0x1F00
#define MASK_CMCON_MSIZE1   0x1F0000
#define MASK_CMCON_MSTART1  0x1F000000

#define STM_R_MAX           (0x100 / 4)

/* Reset values */
#define RESET_TRICORE_STM_CLC       0x0
#define RESET_TRICORE_STM_ID        0x0000C000
#define RESET_TRICORE_STM_TIM0      0x0
#define RESET_TRICORE_STM_TIM1      0x0
#define RESET_TRICORE_STM_TIM2      0x0
#define RESET_TRICORE_STM_TIM3      0x0
#define RESET_TRICORE_STM_TIM4      0x0
#define RESET_TRICORE_STM_TIM5      0x0
#define RESET_TRICORE_STM_TIM6      0x0
#define RESET_TRICORE_STM_CAP       0x0
#define RESET_TRICORE_STM_CMP0      0x0
#define RESET_TRICORE_STM_CMP1      0x0
#define RESET_TRICORE_STM_CMCON     0x0
#define RESET_TRICORE_STM_ICR       0x0
#define RESET_TRICORE_STM_ISCR      0x0
#define RESET_TRICORE_STM_TIM0SV    0x0
#define RESET_TRICORE_STM_CAPSV     0x0
#define RESET_TRICORE_STM_OCS       0x0
#define RESET_TRICORE_STM_KRSTCLR   0x0
#define RESET_TRICORE_STM_KRST1     0x0
#define RESET_TRICORE_STM_KRST0     0x0
#define RESET_TRICORE_STM_ACCEN1    0x0
#define RESET_TRICORE_STM_ACCEN0    0xFFFFFFFF

struct TriCoreSTMState {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion iomem;
    QEMUTimer *timer;
    Clock *fstm;
    qemu_irq irq;

    uint32_t regs[STM_R_MAX];
    uint64_t tim_counter;
    int64_t tim_base_ns;
};

#endif /* HW_TRICORE_STM_H */
