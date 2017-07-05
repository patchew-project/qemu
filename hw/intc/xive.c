/*
 * QEMU PowerPC XIVE model
 *
 * Copyright (c) 2017, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "target/ppc/cpu.h"
#include "sysemu/cpus.h"
#include "sysemu/dma.h"
#include "monitor/monitor.h"
#include "hw/ppc/xive.h"

#include "xive-internal.h"

/*
 * Main XIVE object
 */

/* Let's provision some HW IRQ numbers. We could use a XIVE property
 * also but it does not seem necessary for the moment.
 */
#define MAX_HW_IRQS_ENTRIES (8 * 1024)


void xive_reset(void *dev)
{
    XIVE *x = XIVE(dev);
    int i;

    /* SBEs are initialized to 0b01 which corresponds to "ints off" */
    memset(x->sbe, 0x55, x->int_count / 4);

    /* Clear and mask all valid IVEs */
    for (i = x->int_base; i < x->int_max; i++) {
        XiveIVE *ive = &x->ivt[i];
        if (ive->w & IVE_VALID) {
            ive->w = IVE_VALID | IVE_MASKED;
        }
    }

    /* clear all EQs */
    memset(x->eqdt, 0, x->nr_targets * XIVE_EQ_PRIORITY_COUNT * sizeof(XiveEQ));
}

static void xive_init(Object *obj)
{
    ;
}

static void xive_realize(DeviceState *dev, Error **errp)
{
    XIVE *x = XIVE(dev);

    if (!x->nr_targets) {
        error_setg(errp, "Number of interrupt targets needs to be greater 0");
        return;
    }

    /* Initialize IRQ number allocator. Let's use a base number if we
     * need to introduce a notion of blocks one day.
     */
    x->int_base = 0;
    x->int_count = x->nr_targets + MAX_HW_IRQS_ENTRIES;
    x->int_max = x->int_base + x->int_count;
    x->int_hw_bot = x->int_max;
    x->int_ipi_top = x->int_base;

    /* Reserve some numbers as OPAL does ? */
    if (x->int_ipi_top < 0x10) {
        x->int_ipi_top = 0x10;
    }

    /* Allocate SBEs (State Bit Entry). 2 bits, so 4 entries per byte */
    x->sbe = g_malloc0(x->int_count / 4);

    /* Allocate the IVT (Interrupt Virtualization Table) */
    x->ivt = g_malloc0(x->int_count * sizeof(XiveIVE));

    /* Allocate the EQDT (Event Queue Descriptor Table), 8 priorities
     * for each thread in the system */
    x->eqdt = g_malloc0(x->nr_targets * XIVE_EQ_PRIORITY_COUNT *
                        sizeof(XiveEQ));

    qemu_register_reset(xive_reset, dev);
}

static Property xive_properties[] = {
    DEFINE_PROP_UINT32("nr-targets", XIVE, nr_targets, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void xive_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = xive_realize;
    dc->props = xive_properties;
    dc->desc = "XIVE";
}

static const TypeInfo xive_info = {
    .name = TYPE_XIVE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_init = xive_init,
    .instance_size = sizeof(XIVE),
    .class_init = xive_class_init,
};

static void xive_register_types(void)
{
    type_register_static(&xive_info);
}

type_init(xive_register_types)

XiveIVE *xive_get_ive(XIVE *x, uint32_t lisn)
{
    uint32_t idx = lisn;

    if (idx < x->int_base || idx >= x->int_max) {
        return NULL;
    }

    return &x->ivt[idx];
}

XiveEQ *xive_get_eq(XIVE *x, uint32_t idx)
{
    if (idx >= x->nr_targets * XIVE_EQ_PRIORITY_COUNT) {
        return NULL;
    }

    return &x->eqdt[idx];
}

/* TODO: improve EQ indexing. This is very simple and relies on the
 * fact that target (CPU) numbers start at 0 and are contiguous. It
 * should be OK for sPAPR.
 */
bool xive_eq_for_target(XIVE *x, uint32_t target, uint8_t priority,
                        uint32_t *out_eq_idx)
{
    if (priority > XIVE_PRIORITY_MAX || target >= x->nr_targets) {
        return false;
    }

    if (out_eq_idx) {
        *out_eq_idx = target + priority;
    }

    return true;
}
