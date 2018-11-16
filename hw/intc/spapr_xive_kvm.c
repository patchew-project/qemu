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
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "target/ppc/cpu.h"
#include "sysemu/cpus.h"
#include "sysemu/kvm.h"
#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_xive.h"
#include "hw/ppc/xive.h"
#include "kvm_ppc.h"

#include <sys/ioctl.h>

/*
 * Helpers for CPU hotplug
 */
typedef struct KVMEnabledCPU {
    unsigned long vcpu_id;
    QLIST_ENTRY(KVMEnabledCPU) node;
} KVMEnabledCPU;

static QLIST_HEAD(, KVMEnabledCPU)
    kvm_enabled_cpus = QLIST_HEAD_INITIALIZER(&kvm_enabled_cpus);

static bool kvm_cpu_is_enabled(CPUState *cs)
{
    KVMEnabledCPU *enabled_cpu;
    unsigned long vcpu_id = kvm_arch_vcpu_id(cs);

    QLIST_FOREACH(enabled_cpu, &kvm_enabled_cpus, node) {
        if (enabled_cpu->vcpu_id == vcpu_id) {
            return true;
        }
    }
    return false;
}

static void kvm_cpu_enable(CPUState *cs)
{
    KVMEnabledCPU *enabled_cpu;
    unsigned long vcpu_id = kvm_arch_vcpu_id(cs);

    enabled_cpu = g_malloc(sizeof(*enabled_cpu));
    enabled_cpu->vcpu_id = vcpu_id;
    QLIST_INSERT_HEAD(&kvm_enabled_cpus, enabled_cpu, node);
}

/*
 * XIVE Thread Interrupt Management context (KVM)
 */

static void xive_tctx_kvm_init(XiveTCTX *tctx, Error **errp)
{
    sPAPRXive *xive;
    unsigned long vcpu_id;
    int ret;

    /* Check if CPU was hot unplugged and replugged. */
    if (kvm_cpu_is_enabled(tctx->cs)) {
        return;
    }

    vcpu_id = kvm_arch_vcpu_id(tctx->cs);
    xive = SPAPR_XIVE_KVM(tctx->xrtr);

    ret = kvm_vcpu_enable_cap(tctx->cs, KVM_CAP_PPC_IRQ_XIVE, 0, xive->fd,
                              vcpu_id, 0);
    if (ret < 0) {
        error_setg(errp, "Unable to connect CPU%ld to KVM XIVE device: %s",
                   vcpu_id, strerror(errno));
        return;
    }

    kvm_cpu_enable(tctx->cs);
}

static void xive_tctx_kvm_realize(DeviceState *dev, Error **errp)
{
    XiveTCTX *tctx = XIVE_TCTX_KVM(dev);
    XiveTCTXClass *xtc = XIVE_TCTX_BASE_GET_CLASS(dev);
    Error *local_err = NULL;

    xtc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    xive_tctx_kvm_init(tctx, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
}

static void xive_tctx_kvm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    XiveTCTXClass *xtc = XIVE_TCTX_BASE_CLASS(klass);

    dc->desc = "sPAPR XIVE KVM Interrupt Thread Context";

    device_class_set_parent_realize(dc, xive_tctx_kvm_realize,
                                    &xtc->parent_realize);
}

static const TypeInfo xive_tctx_kvm_info = {
    .name          = TYPE_XIVE_TCTX_KVM,
    .parent        = TYPE_XIVE_TCTX_BASE,
    .instance_size = sizeof(XiveTCTX),
    .class_init    = xive_tctx_kvm_class_init,
    .class_size    = sizeof(XiveTCTXClass),
};

/*
 * XIVE Interrupt Source (KVM)
 */

static void xive_source_kvm_init(XiveSource *xsrc, Error **errp)
{
    sPAPRXive *xive = SPAPR_XIVE_KVM(xsrc->xive);
    int i;

    /*
     * At reset, interrupt sources are simply created and MASKED. We
     * only need to inform the KVM device about their type: LSI or
     * MSI.
     */
    for (i = 0; i < xsrc->nr_irqs; i++) {
        Error *local_err = NULL;
        uint64_t state = 0;

        if (xive_source_irq_is_lsi(xsrc, i)) {
            state |= KVM_XIVE_LEVEL_SENSITIVE;
            if (xsrc->status[i] & XIVE_STATUS_ASSERTED) {
                state |= KVM_XIVE_LEVEL_ASSERTED;
            }
        }

        kvm_device_access(xive->fd, KVM_DEV_XIVE_GRP_SOURCES, i, &state,
                          true, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }
}

static void xive_source_kvm_reset(DeviceState *dev)
{
    XiveSource *xsrc = XIVE_SOURCE_KVM(dev);
    XiveSourceClass *xsc = XIVE_SOURCE_BASE_GET_CLASS(dev);

    xsc->parent_reset(dev);

    xive_source_kvm_init(xsrc, &error_fatal);
}

static void xive_source_kvm_set_irq(void *opaque, int srcno, int val)
{
    XiveSource *xsrc = opaque;
    struct kvm_irq_level args;
    int rc;

    args.irq = srcno;
    if (!xive_source_irq_is_lsi(xsrc, srcno)) {
        if (!val) {
            return;
        }
        args.level = KVM_INTERRUPT_SET;
    } else {
        if (val) {
            xsrc->status[srcno] |= XIVE_STATUS_ASSERTED;
            args.level = KVM_INTERRUPT_SET_LEVEL;
        } else {
            xsrc->status[srcno] &= ~XIVE_STATUS_ASSERTED;
            args.level = KVM_INTERRUPT_UNSET;
        }
    }
    rc = kvm_vm_ioctl(kvm_state, KVM_IRQ_LINE, &args);
    if (rc < 0) {
        error_report("kvm_irq_line() failed : %s", strerror(errno));
    }
}

static void *spapr_xive_kvm_mmap(sPAPRXive *xive, int ctrl, size_t len,
                                 Error **errp)
{
    Error *local_err = NULL;
    void *addr;
    int fd;

    kvm_device_access(xive->fd, KVM_DEV_XIVE_GRP_CTRL, ctrl, &fd, false,
                      &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return NULL;
    }

    addr = mmap(NULL, len, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (addr == MAP_FAILED) {
        error_setg_errno(errp, errno, "Unable to set XIVE mmaping");
        return NULL;
    }

    return addr;
}

/*
 * The sPAPRXive KVM model should have initialized the KVM device
 * before initializing the source
 */
static void xive_source_kvm_mmap(XiveSource *xsrc, Error **errp)
{
    sPAPRXive *xive = SPAPR_XIVE_KVM(xsrc->xive);
    Error *local_err = NULL;
    size_t esb_len;

    esb_len = (1ull << xsrc->esb_shift) * xsrc->nr_irqs;
    xsrc->esb_mmap = spapr_xive_kvm_mmap(xive, KVM_DEV_XIVE_GET_ESB_FD,
                                         esb_len, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    memory_region_init_ram_device_ptr(&xsrc->esb_mmio, OBJECT(xsrc),
                                      "xive.esb", esb_len, xsrc->esb_mmap);
    sysbus_init_mmio(SYS_BUS_DEVICE(xsrc), &xsrc->esb_mmio);
}

static void xive_source_kvm_realize(DeviceState *dev, Error **errp)
{
    XiveSource *xsrc = XIVE_SOURCE_KVM(dev);
    XiveSourceClass *xsc = XIVE_SOURCE_BASE_GET_CLASS(dev);
    Error *local_err = NULL;

    xsc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    xsrc->qirqs = qemu_allocate_irqs(xive_source_kvm_set_irq, xsrc,
                                     xsrc->nr_irqs);

    xive_source_kvm_mmap(xsrc, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
}

static void xive_source_kvm_unrealize(DeviceState *dev, Error **errp)
{
    XiveSource *xsrc = XIVE_SOURCE_KVM(dev);
    size_t esb_len = (1ull << xsrc->esb_shift) * xsrc->nr_irqs;

    munmap(xsrc->esb_mmap, esb_len);
}

static void xive_source_kvm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    XiveSourceClass *xsc = XIVE_SOURCE_BASE_CLASS(klass);

    device_class_set_parent_realize(dc, xive_source_kvm_realize,
                                    &xsc->parent_realize);
    device_class_set_parent_reset(dc, xive_source_kvm_reset,
                                  &xsc->parent_reset);

    dc->desc = "sPAPR XIVE KVM Interrupt Source";
    dc->unrealize = xive_source_kvm_unrealize;
}

static const TypeInfo xive_source_kvm_info = {
    .name = TYPE_XIVE_SOURCE_KVM,
    .parent = TYPE_XIVE_SOURCE_BASE,
    .instance_size = sizeof(XiveSource),
    .class_init    = xive_source_kvm_class_init,
    .class_size    = sizeof(XiveSourceClass),
};

/*
 * sPAPR XIVE Router (KVM)
 */

static void spapr_xive_kvm_instance_init(Object *obj)
{
    sPAPRXive *xive = SPAPR_XIVE_KVM(obj);

    xive->fd = -1;

    /* We need a KVM flavored source */
    object_initialize(&xive->source, sizeof(xive->source),
                      TYPE_XIVE_SOURCE_KVM);
    object_property_add_child(obj, "source", OBJECT(&xive->source), NULL);

    /* No KVM support for END ESBs. OPAL doesn't either */
    object_initialize(&xive->end_source, sizeof(xive->end_source),
                      TYPE_XIVE_END_SOURCE);
    object_property_add_child(obj, "end_source", OBJECT(&xive->end_source),
                              NULL);
}

static void spapr_xive_kvm_init(sPAPRXive *xive, Error **errp)
{
    Error *local_err = NULL;
    size_t tima_len;

    if (!kvm_enabled() || !kvmppc_has_cap_xive()) {
        error_setg(errp,
                   "IRQ_XIVE capability must be present for KVM XIVE device");
        return;
    }

    /* First, create the KVM XIVE device */
    xive->fd = kvm_create_device(kvm_state, KVM_DEV_TYPE_XIVE, false);
    if (xive->fd < 0) {
        error_setg_errno(errp, -xive->fd, "error creating KVM XIVE device");
        return;
    }

    /* Source ESBs KVM mapping
     *
     * Inform KVM where we will map the ESB pages. This is needed by
     * the H_INT_GET_SOURCE_INFO hcall which returns the source
     * characteristics, among which the ESB page address.
     */
    kvm_device_access(xive->fd, KVM_DEV_XIVE_GRP_CTRL, KVM_DEV_XIVE_VC_BASE,
                      &xive->vc_base, true, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /* Let the XiveSource KVM model handle the mapping for the moment */

    /* TIMA KVM mapping
     *
     * We could also inform KVM where the TIMA will be mapped but as
     * this is a fixed MMIO address for the system it does not seem
     * necessary to provide a KVM ioctl to change it.
     */
    tima_len = 4ull << TM_SHIFT;
    xive->tm_mmap = spapr_xive_kvm_mmap(xive, KVM_DEV_XIVE_GET_TIMA_FD,
                                        tima_len, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    memory_region_init_ram_device_ptr(&xive->tm_mmio, OBJECT(xive),
                                      "xive.tima", tima_len, xive->tm_mmap);
    sysbus_init_mmio(SYS_BUS_DEVICE(xive), &xive->tm_mmio);

    kvm_kernel_irqchip = true;
    kvm_msi_via_irqfd_allowed = true;
    kvm_gsi_direct_mapping = true;
}

static void spapr_xive_kvm_realize(DeviceState *dev, Error **errp)
{
    sPAPRXive *xive = SPAPR_XIVE_KVM(dev);
    sPAPRXiveClass *sxc = SPAPR_XIVE_BASE_GET_CLASS(dev);
    Error *local_err = NULL;

    spapr_xive_kvm_init(xive, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /* Initialize the source and the local routing tables */
    sxc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
}

static void spapr_xive_kvm_unrealize(DeviceState *dev, Error **errp)
{
    sPAPRXive *xive = SPAPR_XIVE_KVM(dev);

    close(xive->fd);
    xive->fd = -1;

    munmap(xive->tm_mmap, 4ull << TM_SHIFT);
}

static void spapr_xive_kvm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    sPAPRXiveClass *sxc = SPAPR_XIVE_BASE_CLASS(klass);

    device_class_set_parent_realize(dc, spapr_xive_kvm_realize,
                                    &sxc->parent_realize);

    dc->desc = "sPAPR XIVE KVM Interrupt Controller";
    dc->unrealize = spapr_xive_kvm_unrealize;
}

static const TypeInfo spapr_xive_kvm_info = {
    .name = TYPE_SPAPR_XIVE_KVM,
    .parent = TYPE_SPAPR_XIVE_BASE,
    .instance_init = spapr_xive_kvm_instance_init,
    .instance_size = sizeof(sPAPRXive),
    .class_init = spapr_xive_kvm_class_init,
    .class_size = sizeof(sPAPRXiveClass),
};

static void xive_kvm_register_types(void)
{
    type_register_static(&spapr_xive_kvm_info);
    type_register_static(&xive_source_kvm_info);
    type_register_static(&xive_tctx_kvm_info);
}

type_init(xive_kvm_register_types)
