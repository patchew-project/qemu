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
#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_xive.h"
#include "hw/ppc/xive.h"
#include "hw/ppc/xive_regs.h"

void spapr_xive_pic_print_info(sPAPRXive *xive, Monitor *mon)
{
    sPAPRXiveClass *sxc = SPAPR_XIVE_GET_CLASS(xive);
    int i;

    if (sxc->synchronize_state) {
        sxc->synchronize_state(xive);
    }

    xive_source_pic_print_info(&xive->source, mon);

    monitor_printf(mon, "IVE Table\n");
    for (i = 0; i < xive->nr_irqs; i++) {
        XiveIVE *ive = &xive->ivt[i];
        uint32_t eq_idx;

        if (!(ive->w & IVE_VALID)) {
            continue;
        }

        eq_idx = GETFIELD(IVE_EQ_INDEX, ive->w);

        monitor_printf(mon, "  %6x %s eqidx:%03d ", i,
                       ive->w & IVE_MASKED ? "M" : " ", eq_idx);

        if (!(ive->w & IVE_MASKED)) {
            XiveEQ *eq;

            eq = xive_fabric_get_eq(XIVE_FABRIC(xive), eq_idx);
            if (eq && (eq->w0 & EQ_W0_VALID)) {
                xive_eq_pic_print_info(eq, mon);
                monitor_printf(mon, " data:%08x",
                               (int) GETFIELD(IVE_EQ_DATA, ive->w));
            } else {
                monitor_printf(mon, "no eq ?!");
            }
        }
        monitor_printf(mon, "\n");
    }
}

static void spapr_xive_reset(DeviceState *dev)
{
    sPAPRXive *xive = SPAPR_XIVE(dev);
    int i;

    /* Xive Source reset is done through SysBus, it should put all
     * IRQs to OFF (!P|Q) */

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
    sPAPRXive *xive = SPAPR_XIVE(obj);

    object_initialize(&xive->source, sizeof(xive->source), TYPE_XIVE_SOURCE);
    object_property_add_child(obj, "source", OBJECT(&xive->source), NULL);
}

void spapr_xive_common_realize(sPAPRXive *xive, int esb_shift, Error **errp)
{
    XiveSource *xsrc = &xive->source;
    Error *local_err = NULL;

    if (!xive->nr_irqs) {
        error_setg(errp, "Number of interrupt needs to be greater 0");
        return;
    }

    /* The XIVE interrupt controller has an internal source for IPIs
     * and generic IPIs, the PSIHB has one and also the PHBs. For
     * simplicity, we use a unique XIVE source object for *all*
     * interrupts on sPAPR. The ESBs pages are mapped at the address
     * of chip 0 of a real system.
     */
    object_property_set_int(OBJECT(xsrc), XIVE_VC_BASE, "bar",
                            &error_fatal);
    object_property_set_int(OBJECT(xsrc), xive->nr_irqs, "nr-irqs",
                            &error_fatal);
    object_property_set_int(OBJECT(xsrc), esb_shift, "shift",
                            &error_fatal);
    object_property_add_const_link(OBJECT(xsrc), "xive", OBJECT(xive),
                                   &error_fatal);
    object_property_set_bool(OBJECT(xsrc), true, "realized", &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    qdev_set_parent_bus(DEVICE(xsrc), sysbus_get_default());

    /* Allocate the Interrupt Virtualization Table */
    xive->ivt = g_new0(XiveIVE, xive->nr_irqs);

    /* The Thread Interrupt Management Area has the same address for
     * each chip. On sPAPR, we only need to expose the User and OS
     * level views of the TIMA.
     */
    xive->tm_base = XIVE_TM_BASE;
}

static void spapr_xive_realize(DeviceState *dev, Error **errp)
{
    sPAPRXive *xive = SPAPR_XIVE(dev);
    Error *local_err = NULL;

    spapr_xive_common_realize(xive, XIVE_ESB_64K_2PAGE, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    memory_region_init_io(&xive->tm_mmio_user, OBJECT(xive),
                          &xive_tm_user_ops, xive, "xive.tima.user",
                          1ull << TM_SHIFT);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &xive->tm_mmio_user);

    memory_region_init_io(&xive->tm_mmio_os, OBJECT(xive),
                          &xive_tm_os_ops, xive, "xive.tima.os",
                          1ull << TM_SHIFT);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &xive->tm_mmio_os);
}

static XiveIVE *spapr_xive_get_ive(XiveFabric *xf, uint32_t lisn)
{
    sPAPRXive *xive = SPAPR_XIVE(xf);

    return lisn < xive->nr_irqs ? &xive->ivt[lisn] : NULL;
}

static XiveNVT *spapr_xive_get_nvt(XiveFabric *xf, uint32_t server)
{
    PowerPCCPU *cpu = spapr_find_cpu(server);

    return cpu ? XIVE_NVT(cpu->intc) : NULL;
}

static XiveEQ *spapr_xive_get_eq(XiveFabric *xf, uint32_t eq_idx)
{
    XiveNVT *nvt = xive_fabric_get_nvt(xf, SPAPR_XIVE_EQ_SERVER(eq_idx));

    return xive_nvt_eq_get(nvt, SPAPR_XIVE_EQ_PRIO(eq_idx));
}

static int vmstate_spapr_xive_pre_save(void *opaque)
{
    sPAPRXive *xive = opaque;
    sPAPRXiveClass *sxc = SPAPR_XIVE_GET_CLASS(xive);

    if (sxc->pre_save) {
        sxc->pre_save(xive);
    }

    return 0;
}

static int vmstate_spapr_xive_post_load(void *opaque, int version_id)
{
    sPAPRXive *xive = opaque;
    sPAPRXiveClass *sxc = SPAPR_XIVE_GET_CLASS(xive);

    if (sxc->post_load) {
        sxc->post_load(xive, version_id);
    }

    return 0;
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
    .pre_save = vmstate_spapr_xive_pre_save,
    .post_load = vmstate_spapr_xive_post_load,
    .priority = MIG_PRI_XIVE_IVE,
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
    xfc->get_nvt = spapr_xive_get_nvt;
    xfc->get_eq = spapr_xive_get_eq;
}

static const TypeInfo spapr_xive_info = {
    .name = TYPE_SPAPR_XIVE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_init = spapr_xive_init,
    .instance_size = sizeof(sPAPRXive),
    .class_init = spapr_xive_class_init,
    .class_size = sizeof(sPAPRXiveClass),
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
    XiveSource *xsrc = &xive->source;

    if (!ive) {
        return false;
    }

    ive->w |= IVE_VALID;
    xive_source_irq_set(xsrc, lisn - xsrc->offset, lsi);
    return true;
}

bool spapr_xive_irq_disable(sPAPRXive *xive, uint32_t lisn)
{
    XiveIVE *ive = spapr_xive_get_ive(XIVE_FABRIC(xive), lisn);
    XiveSource *xsrc = &xive->source;

    if (!ive) {
        return false;
    }

    ive->w &= ~IVE_VALID;
    xive_source_irq_set(xsrc, lisn - xsrc->offset, false);
    return true;
}

void spapr_xive_mmio_map(sPAPRXive *xive)
{
    XiveSource *xsrc = &xive->source;

    /* ESBs */
    sysbus_mmio_map(SYS_BUS_DEVICE(xsrc), 0, xsrc->esb_base);

    /* Thread Management Interrupt Area: User and OS views */
    sysbus_mmio_map(SYS_BUS_DEVICE(xive), 0, xive->tm_base);
    sysbus_mmio_map(SYS_BUS_DEVICE(xive), 1, xive->tm_base + (1 << TM_SHIFT));
}

void spapr_xive_mmio_unmap(sPAPRXive *xive)
{
    XiveSource *xsrc = &xive->source;

    /* ESBs */
    sysbus_mmio_unmap(SYS_BUS_DEVICE(xsrc), 0);

    /* Thread Management Interrupt Area: User and OS views */
    sysbus_mmio_unmap(SYS_BUS_DEVICE(xive), 0);
    sysbus_mmio_unmap(SYS_BUS_DEVICE(xive), 1);
}

qemu_irq spapr_xive_qirq(sPAPRXive *xive, int lisn)
{
    XiveIVE *ive = spapr_xive_get_ive(XIVE_FABRIC(xive), lisn);
    XiveSource *xsrc = &xive->source;

    if (!ive || !(ive->w & IVE_VALID)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid LISN %d\n", lisn);
        return NULL;
    }

    return xive_source_qirq(xsrc, lisn - xsrc->offset);
}
