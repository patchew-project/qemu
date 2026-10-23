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

#define STM_R_MAX           (0x100 / 4)

struct TriCoreSTMState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    QEMUTimer *timer;
    Clock *fstm;
    qemu_irq irq;

    uint32_t regs[STM_R_MAX];
    uint64_t tim_counter;
    int64_t tim_base_ns;
};

#endif /* HW_TRICORE_STM_H */
