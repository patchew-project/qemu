/*
 * QEMU PowerPC sPAPR XIVE interrupt controller model
 *
 * Copyright (c) 2017-2018, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "target/ppc/cpu.h"
#include "sysemu/cpus.h"
#include "monitor/monitor.h"
#include "hw/ppc/spapr_xive.h"
#include "hw/ppc/xive_regs.h"

void spapr_xive_pic_print_info(sPAPRXive *xive, Monitor *mon)
{
    int i;

    monitor_printf(mon, "IVE Table\n");
    for (i = 0; i < xive->nr_irqs; i++) {
        XiveIVE *ive = &xive->ivt[i];

        if (!(ive->w & IVE_VALID)) {
            continue;
        }

        monitor_printf(mon, "  %4x %s %08x %08x\n", i,
                       ive->w & IVE_MASKED ? "M" : " ",
                       (int) GETFIELD(IVE_EQ_INDEX, ive->w),
                       (int) GETFIELD(IVE_EQ_DATA, ive->w));
    }
}

static void spapr_xive_reset(DeviceState *dev)
{
    sPAPRXive *xive = SPAPR_XIVE(dev);
    int i;

    /* Mask all valid IVEs in the IRQ number space. */
    for (i = 0; i < xive->nr_irqs; i++) {
        XiveIVE *ive = &xive->ivt[i];
        if (ive->w & IVE_VALID) {
            ive->w |= IVE_MASKED;
        }
    }
}

static void spapr_xive_init(Object *obj)
{

}

static void spapr_xive_realize(DeviceState *dev, Error **errp)
{
    sPAPRXive *xive = SPAPR_XIVE(dev);

    if (!xive->nr_irqs) {
        error_setg(errp, "Number of interrupt needs to be greater 0");
        return;
    }

    /* Allocate the Interrupt Virtualization Table */
    xive->ivt = g_new0(XiveIVE, xive->nr_irqs);
}

static XiveIVE *spapr_xive_get_ive(XiveFabric *xf, uint32_t lisn)
{
    sPAPRXive *xive = SPAPR_XIVE(xf);

    return lisn < xive->nr_irqs ? &xive->ivt[lisn] : NULL;
}

static const VMStateDescription vmstate_spapr_xive_ive = {
    .name = TYPE_SPAPR_XIVE "/ive",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField []) {
        VMSTATE_UINT64(w, XiveIVE),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_spapr_xive = {
    .name = TYPE_SPAPR_XIVE,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_EQUAL(nr_irqs, sPAPRXive, NULL),
        VMSTATE_STRUCT_VARRAY_POINTER_UINT32(ivt, sPAPRXive, nr_irqs,
                                     vmstate_spapr_xive_ive, XiveIVE),
        VMSTATE_END_OF_LIST()
    },
};

static Property spapr_xive_properties[] = {
    DEFINE_PROP_UINT32("nr-irqs", sPAPRXive, nr_irqs, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void spapr_xive_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    XiveFabricClass *xfc = XIVE_FABRIC_CLASS(klass);

    dc->realize = spapr_xive_realize;
    dc->reset = spapr_xive_reset;
    dc->props = spapr_xive_properties;
    dc->desc = "sPAPR XIVE interrupt controller";
    dc->vmsd = &vmstate_spapr_xive;

    xfc->get_ive = spapr_xive_get_ive;
}

static const TypeInfo spapr_xive_info = {
    .name = TYPE_SPAPR_XIVE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_init = spapr_xive_init,
    .instance_size = sizeof(sPAPRXive),
    .class_init = spapr_xive_class_init,
    .interfaces = (InterfaceInfo[]) {
            { TYPE_XIVE_FABRIC },
            { },
    },
};

static void spapr_xive_register_types(void)
{
    type_register_static(&spapr_xive_info);
}

type_init(spapr_xive_register_types)

bool spapr_xive_irq_enable(sPAPRXive *xive, uint32_t lisn, bool lsi)
{
    XiveIVE *ive = spapr_xive_get_ive(XIVE_FABRIC(xive), lisn);

    if (!ive) {
        return false;
    }

    ive->w |= IVE_VALID;
    return true;
}

bool spapr_xive_irq_disable(sPAPRXive *xive, uint32_t lisn)
{
    XiveIVE *ive = spapr_xive_get_ive(XIVE_FABRIC(xive), lisn);

    if (!ive) {
        return false;
    }

    ive->w &= ~IVE_VALID;
    return true;
}
