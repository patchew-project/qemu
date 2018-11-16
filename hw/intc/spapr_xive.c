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

/*
 * XIVE Virtualization Controller BAR and Thread Managment BAR that we
 * use for the ESB pages and the TIMA pages
 */
#define SPAPR_XIVE_VC_BASE   0x0006010000000000ull
#define SPAPR_XIVE_TM_BASE   0x0006030203180000ull

void spapr_xive_pic_print_info(sPAPRXive *xive, Monitor *mon)
{
    sPAPRXiveClass *sxc = SPAPR_XIVE_BASE_GET_CLASS(xive);
    int i;
    uint32_t offset = 0;

    if (sxc->synchronize_state) {
        sxc->synchronize_state(xive);
    }

    monitor_printf(mon, "XIVE Source %08x .. %08x\n", offset,
                   offset + xive->source.nr_irqs - 1);
    xive_source_pic_print_info(&xive->source, offset, mon);

    monitor_printf(mon, "XIVE EAT %08x .. %08x\n", 0, xive->nr_irqs - 1);
    for (i = 0; i < xive->nr_irqs; i++) {
        xive_eas_pic_print_info(&xive->eat[i], i, mon);
    }

    monitor_printf(mon, "XIVE ENDT %08x .. %08x\n", 0, xive->nr_ends - 1);
    for (i = 0; i < xive->nr_ends; i++) {
        xive_end_pic_print_info(&xive->endt[i], i, mon);
    }
}

/* Map the ESB pages and the TIMA pages */
static void spapr_xive_mmio_map(sPAPRXive *xive)
{
    sysbus_mmio_map(SYS_BUS_DEVICE(&xive->source), 0, xive->vc_base);
    sysbus_mmio_map(SYS_BUS_DEVICE(&xive->end_source), 0, xive->end_base);
    sysbus_mmio_map(SYS_BUS_DEVICE(xive), 0, xive->tm_base);
}

static void spapr_xive_base_reset(DeviceState *dev)
{
    sPAPRXive *xive = SPAPR_XIVE_BASE(dev);
    int i;

    /* Xive Source reset is done through SysBus, it should put all
     * IRQs to OFF (!P|Q) */

    /* Mask all valid EASs in the IRQ number space. */
    for (i = 0; i < xive->nr_irqs; i++) {
        XiveEAS *eas = &xive->eat[i];
        if (eas->w & EAS_VALID) {
            eas->w |= EAS_MASKED;
        }
    }

    for (i = 0; i < xive->nr_ends; i++) {
        xive_end_reset(&xive->endt[i]);
    }

    spapr_xive_mmio_map(xive);
}

static void spapr_xive_base_instance_init(Object *obj)
{
    sPAPRXive *xive = SPAPR_XIVE_BASE(obj);

    object_initialize(&xive->source, sizeof(xive->source), TYPE_XIVE_SOURCE);
    object_property_add_child(obj, "source", OBJECT(&xive->source), NULL);

    object_initialize(&xive->end_source, sizeof(xive->end_source),
                      TYPE_XIVE_END_SOURCE);
    object_property_add_child(obj, "end_source", OBJECT(&xive->end_source),
                              NULL);
}

static void spapr_xive_base_realize(DeviceState *dev, Error **errp)
{
    sPAPRXive *xive = SPAPR_XIVE_BASE(dev);
    XiveSource *xsrc = &xive->source;
    XiveENDSource *end_xsrc = &xive->end_source;
    Error *local_err = NULL;

    if (!xive->nr_irqs) {
        error_setg(errp, "Number of interrupt needs to be greater 0");
        return;
    }

    if (!xive->nr_ends) {
        error_setg(errp, "Number of interrupt needs to be greater 0");
        return;
    }

    /*
     * Initialize the internal sources, for IPIs and virtual devices.
     */
    object_property_set_int(OBJECT(xsrc), xive->nr_irqs, "nr-irqs",
                            &error_fatal);
    object_property_add_const_link(OBJECT(xsrc), "xive", OBJECT(xive),
                                   &error_fatal);
    object_property_set_bool(OBJECT(xsrc), true, "realized", &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    qdev_set_parent_bus(DEVICE(xsrc), sysbus_get_default());

    /*
     * Initialize the END ESB source
     */
    object_property_set_int(OBJECT(end_xsrc), xive->nr_irqs, "nr-ends",
                            &error_fatal);
    object_property_add_const_link(OBJECT(end_xsrc), "xive", OBJECT(xive),
                                   &error_fatal);
    object_property_set_bool(OBJECT(end_xsrc), true, "realized", &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    qdev_set_parent_bus(DEVICE(end_xsrc), sysbus_get_default());

    /* Set the mapping address of the END ESB pages after the source ESBs */
    xive->end_base = xive->vc_base + (1ull << xsrc->esb_shift) * xsrc->nr_irqs;

    /*
     * Allocate the routing tables
     */
    xive->eat = g_new0(XiveEAS, xive->nr_irqs);
    xive->endt = g_new0(XiveEND, xive->nr_ends);
}

static int spapr_xive_get_eas(XiveRouter *xrtr, uint32_t lisn, XiveEAS *eas)
{
    sPAPRXive *xive = SPAPR_XIVE_BASE(xrtr);

    if (lisn >= xive->nr_irqs) {
        return -1;
    }

    *eas = xive->eat[lisn];
    return 0;
}

static int spapr_xive_set_eas(XiveRouter *xrtr, uint32_t lisn, XiveEAS *eas)
{
    sPAPRXive *xive = SPAPR_XIVE_BASE(xrtr);

    if (lisn >= xive->nr_irqs) {
        return -1;
    }

    xive->eat[lisn] = *eas;
    return 0;
}

static int spapr_xive_get_end(XiveRouter *xrtr,
                              uint8_t end_blk, uint32_t end_idx, XiveEND *end)
{
    sPAPRXive *xive = SPAPR_XIVE_BASE(xrtr);

    if (end_idx >= xive->nr_ends) {
        return -1;
    }

    memcpy(end, &xive->endt[end_idx], sizeof(XiveEND));
    return 0;
}

static int spapr_xive_set_end(XiveRouter *xrtr,
                              uint8_t end_blk, uint32_t end_idx, XiveEND *end)
{
    sPAPRXive *xive = SPAPR_XIVE_BASE(xrtr);

    if (end_idx >= xive->nr_ends) {
        return -1;
    }

    memcpy(&xive->endt[end_idx], end, sizeof(XiveEND));
    return 0;
}

static int spapr_xive_get_nvt(XiveRouter *xrtr,
                              uint8_t nvt_blk, uint32_t nvt_idx, XiveNVT *nvt)
{
    sPAPRXive *xive = SPAPR_XIVE_BASE(xrtr);
    uint32_t vcpu_id = spapr_xive_nvt_to_target(xive, nvt_blk, nvt_idx);
    PowerPCCPU *cpu = spapr_find_cpu(vcpu_id);

    if (!cpu) {
        return -1;
    }

    /*
     * sPAPR does not maintain a NVT table. Return that the NVT is
     * valid if we have found a matching CPU
     */
    nvt->w0 = NVT_W0_VALID;
    return 0;
}

static int spapr_xive_set_nvt(XiveRouter *xrtr,
                              uint8_t nvt_blk, uint32_t nvt_idx, XiveNVT *nvt)
{
    /* no NVT table */
    return 0;
}

/*
 * When a Virtual Processor is scheduled to run on a HW thread, the
 * hypervisor pushes its identifier in the OS CAM line. Under QEMU, we
 * need to emulate the same behavior.
 */
static void spapr_xive_reset_tctx(XiveRouter *xrtr, XiveTCTX *tctx)
{
    uint8_t  nvt_blk;
    uint32_t nvt_idx;
    uint32_t nvt_cam;

    spapr_xive_cpu_to_nvt(SPAPR_XIVE_BASE(xrtr), POWERPC_CPU(tctx->cs),
                          &nvt_blk, &nvt_idx);

    nvt_cam = cpu_to_be32(TM_QW1W2_VO | xive_tctx_cam_line(nvt_blk, nvt_idx));
    memcpy(&tctx->regs[TM_QW1_OS + TM_WORD2], &nvt_cam, 4);
}

/*
 * The allocation of VP blocks is a complex operation in OPAL and the
 * VP identifiers have a relation with the number of HW chips, the
 * size of the VP blocks, VP grouping, etc. The QEMU sPAPR XIVE
 * controller model does not have the same constraints and can use a
 * simple mapping scheme of the CPU vcpu_id
 *
 * These identifiers are never returned to the OS.
 */

#define SPAPR_XIVE_VP_BASE 0x400

uint32_t spapr_xive_nvt_to_target(sPAPRXive *xive, uint8_t nvt_blk,
                                  uint32_t nvt_idx)
{
    return nvt_idx - SPAPR_XIVE_VP_BASE;
}

int spapr_xive_cpu_to_nvt(sPAPRXive *xive, PowerPCCPU *cpu,
                          uint8_t *out_nvt_blk, uint32_t *out_nvt_idx)
{
    XiveRouter *xrtr = XIVE_ROUTER(xive);

    if (!cpu) {
        return -1;
    }

    if (out_nvt_blk) {
        /* For testing purpose, we could use 0 for nvt_blk */
        *out_nvt_blk = xrtr->chip_id;
    }

    if (out_nvt_blk) {
        *out_nvt_idx = SPAPR_XIVE_VP_BASE + cpu->vcpu_id;
    }
    return 0;
}

int spapr_xive_target_to_nvt(sPAPRXive *xive, uint32_t target,
                             uint8_t *out_nvt_blk, uint32_t *out_nvt_idx)
{
    return spapr_xive_cpu_to_nvt(xive, spapr_find_cpu(target), out_nvt_blk,
                                 out_nvt_idx);
}

/*
 * sPAPR END indexing uses a simple mapping of the CPU vcpu_id, 8
 * priorities per CPU
 */
int spapr_xive_end_to_target(sPAPRXive *xive, uint8_t end_blk, uint32_t end_idx,
                             uint32_t *out_server, uint8_t *out_prio)
{
    if (out_server) {
        *out_server = end_idx >> 3;
    }

    if (out_prio) {
        *out_prio = end_idx & 0x7;
    }
    return 0;
}

int spapr_xive_cpu_to_end(sPAPRXive *xive, PowerPCCPU *cpu, uint8_t prio,
                          uint8_t *out_end_blk, uint32_t *out_end_idx)
{
    XiveRouter *xrtr = XIVE_ROUTER(xive);

    if (!cpu) {
        return -1;
    }

    if (out_end_blk) {
        /* For testing purpose, we could use 0 for nvt_blk */
        *out_end_blk = xrtr->chip_id;
    }

    if (out_end_idx) {
        *out_end_idx = (cpu->vcpu_id << 3) + prio;
    }
    return 0;
}

int spapr_xive_target_to_end(sPAPRXive *xive, uint32_t target, uint8_t prio,
                             uint8_t *out_end_blk, uint32_t *out_end_idx)
{
    return spapr_xive_cpu_to_end(xive, spapr_find_cpu(target), prio,
                                 out_end_blk, out_end_idx);
}

static const VMStateDescription vmstate_spapr_xive_end = {
    .name = TYPE_SPAPR_XIVE "/end",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField []) {
        VMSTATE_UINT32(w0, XiveEND),
        VMSTATE_UINT32(w1, XiveEND),
        VMSTATE_UINT32(w2, XiveEND),
        VMSTATE_UINT32(w3, XiveEND),
        VMSTATE_UINT32(w4, XiveEND),
        VMSTATE_UINT32(w5, XiveEND),
        VMSTATE_UINT32(w6, XiveEND),
        VMSTATE_UINT32(w7, XiveEND),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_spapr_xive_eas = {
    .name = TYPE_SPAPR_XIVE "/eas",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField []) {
        VMSTATE_UINT64(w, XiveEAS),
        VMSTATE_END_OF_LIST()
    },
};

static int vmstate_spapr_xive_pre_save(void *opaque)
{
    sPAPRXive *xive = SPAPR_XIVE_BASE(opaque);
    sPAPRXiveClass *sxc = SPAPR_XIVE_BASE_GET_CLASS(xive);

    if (sxc->pre_save) {
        return sxc->pre_save(xive);
    }

    return 0;
}

/* handled at the machine level */
int spapr_xive_post_load(sPAPRXive *xive, int version_id)
{
    sPAPRXiveClass *sxc = SPAPR_XIVE_BASE_GET_CLASS(xive);

    if (sxc->post_load) {
        return sxc->post_load(xive, version_id);
    }

    return 0;
}

static const VMStateDescription vmstate_spapr_xive_base = {
    .name = TYPE_SPAPR_XIVE,
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = vmstate_spapr_xive_pre_save,
    .post_load = NULL, /* handled at the machine level */
    .priority = MIG_PRI_XIVE_IC,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_EQUAL(nr_irqs, sPAPRXive, NULL),
        VMSTATE_STRUCT_VARRAY_POINTER_UINT32(eat, sPAPRXive, nr_irqs,
                                     vmstate_spapr_xive_eas, XiveEAS),
        VMSTATE_STRUCT_VARRAY_POINTER_UINT32(endt, sPAPRXive, nr_ends,
                                             vmstate_spapr_xive_end, XiveEND),
        VMSTATE_END_OF_LIST()
    },
};

static Property spapr_xive_base_properties[] = {
    DEFINE_PROP_UINT32("nr-irqs", sPAPRXive, nr_irqs, 0),
    DEFINE_PROP_UINT32("nr-ends", sPAPRXive, nr_ends, 0),
    DEFINE_PROP_UINT64("vc-base", sPAPRXive, vc_base, SPAPR_XIVE_VC_BASE),
    DEFINE_PROP_UINT64("tm-base", sPAPRXive, tm_base, SPAPR_XIVE_TM_BASE),
    DEFINE_PROP_END_OF_LIST(),
};

static void spapr_xive_base_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    XiveRouterClass *xrc = XIVE_ROUTER_CLASS(klass);

    dc->desc    = "sPAPR XIVE Interrupt Controller";
    dc->props   = spapr_xive_base_properties;
    dc->realize = spapr_xive_base_realize;
    dc->reset   = spapr_xive_base_reset;
    dc->vmsd    = &vmstate_spapr_xive_base;

    xrc->get_eas = spapr_xive_get_eas;
    xrc->set_eas = spapr_xive_set_eas;
    xrc->get_end = spapr_xive_get_end;
    xrc->set_end = spapr_xive_set_end;
    xrc->get_nvt = spapr_xive_get_nvt;
    xrc->set_nvt = spapr_xive_set_nvt;
    xrc->reset_tctx = spapr_xive_reset_tctx;
}

static const TypeInfo spapr_xive_base_info = {
    .name = TYPE_SPAPR_XIVE_BASE,
    .parent = TYPE_XIVE_ROUTER,
    .abstract = true,
    .instance_init = spapr_xive_base_instance_init,
    .instance_size = sizeof(sPAPRXive),
    .class_init = spapr_xive_base_class_init,
    .class_size = sizeof(sPAPRXiveClass),
};

static void spapr_xive_realize(DeviceState *dev, Error **errp)
{
    sPAPRXive *xive = SPAPR_XIVE(dev);
    sPAPRXiveClass *sxc = SPAPR_XIVE_BASE_GET_CLASS(dev);
    Error *local_err = NULL;

    sxc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /* TIMA */
    memory_region_init_io(&xive->tm_mmio, OBJECT(xive), &xive_tm_ops, xive,
                          "xive.tima", 4ull << TM_SHIFT);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &xive->tm_mmio);
}

static void spapr_xive_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    sPAPRXiveClass *sxc = SPAPR_XIVE_BASE_CLASS(klass);

    device_class_set_parent_realize(dc, spapr_xive_realize,
                                    &sxc->parent_realize);
}

static const TypeInfo spapr_xive_info = {
    .name = TYPE_SPAPR_XIVE,
    .parent = TYPE_SPAPR_XIVE_BASE,
    .instance_init = spapr_xive_base_instance_init,
    .instance_size = sizeof(sPAPRXive),
    .class_init = spapr_xive_class_init,
    .class_size = sizeof(sPAPRXiveClass),
};

static void spapr_xive_register_types(void)
{
    type_register_static(&spapr_xive_base_info);
    type_register_static(&spapr_xive_info);
}

type_init(spapr_xive_register_types)

bool spapr_xive_irq_enable(sPAPRXive *xive, uint32_t lisn, bool lsi)
{
    XiveSource *xsrc = &xive->source;

    if (lisn >= xive->nr_irqs) {
        return false;
    }

    xive->eat[lisn].w |= EAS_VALID;
    xive_source_irq_set(xsrc, lisn, lsi);
    return true;
}

bool spapr_xive_irq_disable(sPAPRXive *xive, uint32_t lisn)
{
    XiveSource *xsrc = &xive->source;

    if (lisn >= xive->nr_irqs) {
        return false;
    }

    xive->eat[lisn].w &= ~EAS_VALID;
    xive_source_irq_set(xsrc, lisn, false);
    return true;
}

qemu_irq spapr_xive_qirq(sPAPRXive *xive, uint32_t lisn)
{
    XiveSource *xsrc = &xive->source;

    if (lisn >= xive->nr_irqs) {
        return NULL;
    }

    if (!(xive->eat[lisn].w & EAS_VALID)) {
        qemu_log_mask(LOG_GUEST_ERROR, "XIVE: invalid LISN %x\n", lisn);
        return NULL;
    }

    return xive_source_qirq(xsrc, lisn);
}
