/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU TriCore Interrupt Router (IR)
 *
 * Copyright (c) 2017 David Brenken <david.brenken@efs-auto.de>
 * Copyright (c) 2026 Parthiban Nallathambi <parthiban@linumiz.com>
 */

#ifndef HW_TRICORE_IR_H
#define HW_TRICORE_IR_H

#include "hw/core/sysbus.h"
#include "hw/core/registerfields.h"
#include "qom/object.h"

#define TYPE_TRICORE_IR "tricore_ir"
OBJECT_DECLARE_SIMPLE_TYPE(TriCoreIRState, TRICORE_IR)

/* SRC register fields common to all TriCore variants */
FIELD(SRC, SRPN, 0, 8)
FIELD(SRC, SRR, 24, 1)
FIELD(SRC, CLRR, 25, 1)
FIELD(SRC, SETR, 26, 1)
FIELD(SRC, IOV, 27, 1)
FIELD(SRC, IOVCLR, 28, 1)

/* TC3x SRC bit layout */
FIELD(SRC_TC3X, SRE, 10, 1)
FIELD(SRC_TC3X, TOS, 11, 3)

/* LWSR register fields */
FIELD(LWSR, PN, 0, 8)
FIELD(LWSR, VALID, 12, 1)
FIELD(LWSR, ID, 16, 9)
FIELD(LWSR, STAT, 31, 1)

/* LASR register fields */
FIELD(LASR, PN, 0, 8)
FIELD(LASR, ID, 16, 11)
FIELD(LASR, ENTER, 31, 1)

struct TriCoreIRState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion src_region;
    MemoryRegion int_region;

    uint32_t *src_regs;
    uint32_t lwsr[8];
    uint32_t lasr;

    qemu_irq *isp_irqs;

    uint8_t num_isps;
    uint16_t num_irqs;
};

void tricore_ir_irq_acknowledge(TriCoreIRState *s, uint16_t irq, uint8_t vm);

#endif /* HW_TRICORE_IR_H */
