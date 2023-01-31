/*
 * QEMU lowRISC Ibex IRQ wrapper
 *
 * Copyright (c) 2022-2023 Rivos, Inc.
 *
 * SPDX-License-Identifier: MIT
 *
 */

#ifndef HW_RISCV_IBEX_IRQ_H
#define HW_RISCV_IBEX_IRQ_H

#include "qemu/osdep.h"
#include "qom/object.h"
#include "hw/irq.h"
#include "hw/qdev-core.h"
#include "hw/sysbus.h"


/** Simple IRQ wrapper to limit propagation of no-change calls */
typedef struct {
    qemu_irq irq;
    int level;
} IbexIRQ;

static inline bool ibex_irq_set(IbexIRQ *ibex_irq, int level)
{
    if (level != ibex_irq->level) {
        ibex_irq->level = level;
        qemu_set_irq(ibex_irq->irq, level);
        return true;
    }

    return false;
}

static inline bool ibex_irq_raise(IbexIRQ *irq)
{
    return ibex_irq_set(irq, 1);
}

static inline bool ibex_irq_lower(IbexIRQ *irq)
{
    return ibex_irq_set(irq, 0);
}

static inline void ibex_qdev_init_irq(Object *obj, IbexIRQ *irq,
                                      const char *name)
{
    irq->level = 0;
    qdev_init_gpio_out_named(DEVICE(obj), &irq->irq, name, 1);
}

static inline void ibex_qdev_init_irqs(Object *obj, IbexIRQ *irqs,
                                       const char *name, unsigned count)
{
    for (unsigned ix = 0; ix < count; ix++) {
        irqs[ix].level = 0;
        qdev_init_gpio_out_named(DEVICE(obj), &irqs[ix].irq, name, 1);
    }
}

static inline void ibex_sysbus_init_irq(Object *obj, IbexIRQ *irq)
{
    irq->level = 0;
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &irq->irq);
}

#endif /* HW_RISCV_IBEX_IRQ_H */
