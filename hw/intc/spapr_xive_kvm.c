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
#include "monitor/monitor.h"
#include "hw/intc/intc.h"
#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_xive.h"
#include "hw/ppc/xive.h"
#include "hw/ppc/xive_regs.h"
#include "kvm_ppc.h"

#include <sys/ioctl.h>

/* TODO: kernel_xive_fd is used as a global switch for XIVE */
static int kernel_xive_fd = -1;

typedef struct KVMEnabledCPU {
    unsigned long vcpu_id;
    QLIST_ENTRY(KVMEnabledCPU) node;
} KVMEnabledCPU;

static QLIST_HEAD(, KVMEnabledCPU)
    kvm_enabled_cpus = QLIST_HEAD_INITIALIZER(&kvm_enabled_cpus);

static bool xive_nvt_kvm_cpu_is_enabled(CPUState *cs)
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

static void xive_nvt_kvm_cpu_disable(CPUState *cs, Error **errp)
{
    KVMEnabledCPU *enabled_cpu;
    unsigned long vcpu_id = kvm_arch_vcpu_id(cs);

    QLIST_FOREACH(enabled_cpu, &kvm_enabled_cpus, node) {
        if (enabled_cpu->vcpu_id == vcpu_id) {
            break;
        }
    }

    if (enabled_cpu->vcpu_id == vcpu_id) {
        QLIST_REMOVE(enabled_cpu, node);
        g_free(enabled_cpu);
    } else {
        error_setg(errp, "Can not find enabled CPU%ld", vcpu_id);
    }
}

static void xive_nvt_kvm_cpu_enable(CPUState *cs)
{
    KVMEnabledCPU *enabled_cpu;
    unsigned long vcpu_id = kvm_arch_vcpu_id(cs);

    enabled_cpu = g_malloc(sizeof(*enabled_cpu));
    enabled_cpu->vcpu_id = vcpu_id;
    QLIST_INSERT_HEAD(&kvm_enabled_cpus, enabled_cpu, node);
}

static inline bool xive_queue_is_valid(int priority)
{
    switch (priority) {
    case 0 ... 6:
        return true;
    case 7: /* OPAL escalation queue */
    default:
        return false;
    }
}

static void xive_nvt_kvm_get_state(XiveNVT *nvt)
{
    uint64_t state[2] = { 0 };
    int ret;
    int i;

    ret = kvm_get_one_reg(nvt->cs, KVM_REG_PPC_VP_STATE, state);
    if (ret != 0) {
        error_report("Unable to retrieve KVM XIVE interrupt controller state"
                " for CPU %ld: %s", kvm_arch_vcpu_id(nvt->cs), strerror(errno));
        return;
    }

    /* First quad should be a backup of word0 and word1 of the OS
     * ring. Second quad is the OPAL internal state which holds word4
     * of the VP structure. We are only interested by the IPB in there
     * but we should consider it as opaque.
     *
     * As we won't use the registers of the HV ring on sPAPR, let's
     * hijack them to store the 'OPAL' state
     */
    *((uint64_t *) nvt->ring_os) = state[0];
    *((uint64_t *) &nvt->regs[TM_QW2_HV_POOL]) = state[1];

    /* Now dump all the queue internals  */
    for (i = 0; i < ARRAY_SIZE(nvt->eqt); i++) {
        XiveEQ eq = { 0 };

        if (!xive_queue_is_valid(i)) {
            continue;
        }

        ret = kvm_get_one_reg(nvt->cs, KVM_REG_PPC_VP_EQ0 + i, &eq);
        if (ret != 0) {
            error_report("Unable to retrieve KVM XIVE interrupt controller"
                         " state for CPU %ld priority %d: %s",
                         kvm_arch_vcpu_id(nvt->cs), i, strerror(errno));
            return;
        }

        if (eq.w0 & EQ_W0_VALID) {
            memcpy(&nvt->eqt[i], &eq, sizeof(nvt->eqt[i]));
        }
    }
}

static void xive_nvt_kvm_do_synchronize_state(CPUState *cpu,
                                              run_on_cpu_data arg)
{
    xive_nvt_kvm_get_state(arg.host_ptr);
}

static void xive_nvt_kvm_synchronize_state(XiveNVT *nvt)
{
    if (nvt->cs) {
        run_on_cpu(nvt->cs, xive_nvt_kvm_do_synchronize_state,
                   RUN_ON_CPU_HOST_PTR(nvt));
    }
}

static int xive_nvt_kvm_set_state(XiveNVT *nvt, int version_id)
{
    uint64_t state[2];
    int ret;
    int i;

    /* NVT for this CPU thread is not in use, exiting */
    if (!nvt->cs) {
        return 0;
    }

    state[0] = *((uint64_t *) nvt->ring_os);
    state[1] = *((uint64_t *) &nvt->regs[TM_QW2_HV_POOL]);

    ret = kvm_set_one_reg(nvt->cs, KVM_REG_PPC_VP_STATE, state);
    if (ret != 0) {
        error_report("Unable to restore KVM XIVE interrupt controller state"
                     " for CPU %ld: %s", kvm_arch_vcpu_id(nvt->cs),
                     strerror(errno));
        return ret;
    }

    for (i = 0; i < ARRAY_SIZE(nvt->eqt); i++) {
        XiveEQ *eq = &nvt->eqt[i];

        if (!xive_queue_is_valid(i)) {
            continue;
        }

        if (!(eq->w0 & EQ_W0_VALID)) {
            continue;
        }

        ret = kvm_set_one_reg(nvt->cs, KVM_REG_PPC_VP_EQ0 + i, eq);
        if (ret != 0) {
            error_report("Unable to restore KVM XIVE interrupt controller"
                         " state for CPU %ld priority %d: %s",
                         kvm_arch_vcpu_id(nvt->cs), i, strerror(errno));
            return ret;
        }
    }

    return 0;
}

static void xive_nvt_kvm_reset(XiveNVT *nvt)
{
    /* XIVE is not enabled at first machine reset, only after CAS. */
    if (kernel_xive_fd == -1) {
        return;
    }

    xive_nvt_kvm_set_state(nvt, 1);
}

static void xive_nvt_kvm_disconnect(CPUIntc *intc, Error **errp)
{
    XiveNVT *nvt = XIVE_NVT_KVM(intc);
    CPUState *cs = nvt->cs;
    unsigned long vcpu_id = kvm_arch_vcpu_id(cs);
    int ret;

    if (kernel_xive_fd == -1) {
        return;
    }

    /* Disable IRQ capability with a 'disable=1' as last argument.
     *
     * This is a bit hacky, we should introduce a KVM_DISABLE_CAP
     * iotcl
     */
    ret = kvm_vcpu_enable_cap(cs, KVM_CAP_PPC_IRQ_XIVE, 0, kernel_xive_fd,
                              vcpu_id, 1);
    if (ret < 0) {
        error_setg(errp, "Unable to disconnect CPU%ld from KVM XIVE device: %s",
                   vcpu_id, strerror(errno));
        return;
    }

    xive_nvt_kvm_cpu_disable(cs, errp);
}

static void xive_nvt_kvm_connect(CPUIntc *intc, Error **errp)
{
    XiveNVT *nvt = XIVE_NVT_KVM(intc);
    CPUState *cs = nvt->cs;
    unsigned long vcpu_id = kvm_arch_vcpu_id(cs);
    int ret;

    /* Check if CPU was hot unplugged and replugged. */
    if (xive_nvt_kvm_cpu_is_enabled(cs)) {
        return;
    }

    ret = kvm_vcpu_enable_cap(cs, KVM_CAP_PPC_IRQ_XIVE, 0, kernel_xive_fd,
                              vcpu_id, 0);
    if (ret < 0) {
        error_setg(errp, "Unable to connect CPU%ld to KVM XIVE device: %s",
                   vcpu_id, strerror(errno));
        return;
    }

    xive_nvt_kvm_cpu_enable(cs);
}

static void xive_nvt_kvm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    XiveNVTClass *xnc = XIVE_NVT_CLASS(klass);
    CPUIntcClass *cic = CPU_INTC_CLASS(klass);

    dc->desc = "XIVE KVM Interrupt Presenter";

    xnc->synchronize_state = xive_nvt_kvm_synchronize_state;
    xnc->reset = xive_nvt_kvm_reset;
    xnc->pre_save = xive_nvt_kvm_get_state;
    xnc->post_load = xive_nvt_kvm_set_state;

    cic->connect = xive_nvt_kvm_connect;
    cic->disconnect = xive_nvt_kvm_disconnect;
}

static const TypeInfo xive_nvt_kvm_info = {
    .name          = TYPE_XIVE_NVT_KVM,
    .parent        = TYPE_XIVE_NVT,
    .instance_size = sizeof(XiveNVT),
    .class_init    = xive_nvt_kvm_class_init,
    .class_size    = sizeof(XiveNVTClass),
};

static void xive_source_kvm_set_irq(void *opaque, int srcno, int val)
{
    XiveSource *xsrc = opaque;
    struct kvm_irq_level args;
    int rc;

    args.irq = srcno + xsrc->offset;
    if (!xive_source_irq_is_lsi(xsrc, srcno)) {
        if (!val) {
            return;
        }
        args.level = KVM_INTERRUPT_SET;
    } else {
        args.level = val ? KVM_INTERRUPT_SET_LEVEL : KVM_INTERRUPT_UNSET;
    }
    rc = kvm_vm_ioctl(kvm_state, KVM_IRQ_LINE, &args);
    if (rc < 0) {
        error_report("kvm_irq_line() failed : %s", strerror(errno));
    }
}

static void xive_source_kvm_reset(XiveSource *xsrc)
{
    sPAPRXive *xive = SPAPR_XIVE_KVM(xsrc->xive);
    int i;

    /* XIVE is not enabled at first machine reset, only after CAS. */
    if (xive->fd == -1) {
        return;
    }

    /*
     * At reset, interrupt sources are simply created and MASKED, we
     * only need to inform KVM about their type: LSI or MSI.
     */
    for (i = 0; i < xsrc->nr_irqs; i++) {
        Error *err = NULL;
        uint64_t state = 0;

        if (xive_source_irq_is_lsi(xsrc, i)) {
            state |= KVM_XICS_LEVEL_SENSITIVE;
        }

        kvm_device_access(xive->fd, KVM_DEV_XIVE_GRP_SOURCES,
                          i + xsrc->offset, &state, true, &err);
        if (err) {
            error_report_err(err);
            return;
        }
    }
}

/*
 * This is used to perform the magic loads from an ESB described in
 * xive.h.
 */
static uint8_t xive_esb_read(XiveSource *xsrc, int srcno, uint32_t offset)
{
    unsigned long addr = (unsigned long) xsrc->esb_mmap +
        (1ull << xsrc->esb_shift) * srcno;

    /* In a two pages ESB MMIO setting, the odd page is for management */
    if (xive_source_esb_2page(xsrc)) {
        addr += (1 << (xsrc->esb_shift - 1));
    }
    addr += offset;

    return *((uint8_t *) addr);
}

static void xive_source_kvm_get_state(XiveSource *xsrc)
{
    int i;

    for (i = 0; i < xsrc->nr_irqs; i++) {
        /* Perform a load without side effect to retrieve the PQ bits */
        uint8_t pq = xive_esb_read(xsrc, i, XIVE_ESB_GET);

        /* and save PQ locally */
        xive_source_pq_set(xsrc, i, pq);
    }
}

static int xive_source_kvm_set_state(XiveSource *xsrc, int version_id)
{
    int i;
    int unused = 0;

    for (i = 0; i < xsrc->nr_irqs; i++) {
        uint8_t pq = xive_source_pq_get(xsrc, i);

        /* TODO: prevent the compiler from optimizing away the load */
        unused |= xive_esb_read(xsrc, i, XIVE_ESB_SET_PQ_00 + (pq << 8));
    }

    return unused;
}

static void xive_source_kvm_synchronize_state(XiveSource *xsrc)
{
    xive_source_kvm_get_state(xsrc);
}

static void xive_source_kvm_realize(DeviceState *dev, Error **errp)
{
    XiveSource *xsrc = XIVE_SOURCE_KVM(dev);

    xive_source_common_realize(xsrc, xive_source_kvm_set_irq, errp);

    /* The memory regions for the ESB MMIOs will be initialized after
     * KVM is, but we need to declare them on SysBus for the first
     * munmap to work in spapr_reset_interrupt() */
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &xsrc->esb_mmio);
}

static void xive_source_kvm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    XiveSourceClass *xsc = XIVE_SOURCE_CLASS(klass);

    dc->realize = xive_source_kvm_realize;
    dc->desc = "XIVE KVM interrupt source";

    xsc->synchronize_state = xive_source_kvm_synchronize_state;
    xsc->reset = xive_source_kvm_reset;
    xsc->pre_save = xive_source_kvm_get_state;
    xsc->post_load = xive_source_kvm_set_state;
}

static const TypeInfo xive_source_kvm_info = {
    .name = TYPE_XIVE_SOURCE_KVM,
    .parent = TYPE_XIVE_SOURCE,
    .instance_size = sizeof(XiveSource),
    .class_init    = xive_source_kvm_class_init,
    .class_size    = sizeof(XiveSourceClass),
};

static void spapr_xive_kvm_get_state(sPAPRXive *xive)
{
    XiveSource *xsrc = &xive->source;
    int i;

    for (i = 0; i < xsrc->nr_irqs; i++) {
        XiveIVE *ive = &xive->ivt[i];

        if (!(ive->w & IVE_VALID)) {
            continue;
        }

        kvm_device_access(xive->fd, KVM_DEV_XIVE_GRP_IVE,
                          i + xsrc->offset, ive, false, &error_abort);
    }
}

static int spapr_xive_kvm_set_state(sPAPRXive *xive, int version_id)
{
    XiveSource *xsrc = &xive->source;
    int i;

    /* First initialize the KVM device sources, as XIVE is not enabled
     * at machine reset */
    xive_source_kvm_reset(xsrc);

    for (i = 0; i < xsrc->nr_irqs; i++) {
        XiveIVE *ive = &xive->ivt[i];
        Error *err = NULL;

        if (!(ive->w & IVE_VALID) || ive->w & IVE_MASKED) {
            continue;
        }

        kvm_device_access(xive->fd, KVM_DEV_XIVE_GRP_IVE,
                          i + xsrc->offset, ive, true, &err);
        if (err) {
            error_report_err(err);
            return -1;
        }
    }
    return 0;
}

static void spapr_xive_kvm_synchronize_state(sPAPRXive *xive)
{
    spapr_xive_kvm_get_state(xive);
}

static void spapr_xive_kvm_init(Object *obj)
{
    sPAPRXive *xive = SPAPR_XIVE_KVM(obj);

    /* We need a KVM flavored source */
    object_initialize(&xive->source, sizeof(xive->source),
                      TYPE_XIVE_SOURCE_KVM);
    object_property_add_child(obj, "source", OBJECT(&xive->source), NULL);
}

static void spapr_xive_kvm_realize(DeviceState *dev, Error **errp)
{
    sPAPRXive *xive = SPAPR_XIVE_KVM(dev);
    Error *local_err = NULL;

    spapr_xive_common_realize(xive, XIVE_ESB_64K_2PAGE, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    xive->fd = -1;

    /* The memory regions for the TIMA MMIOs will be initialized after
     * KVM is, but we need to declare them on SysBus for the first
     * munmap to work in spapr_reset_interrupt() */
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &xive->tm_mmio_user);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &xive->tm_mmio_os);
}

static void spapr_xive_kvm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    sPAPRXiveClass *sxc = SPAPR_XIVE_CLASS(klass);

    dc->realize = spapr_xive_kvm_realize;
    /* spapr_xive_reset() is used for reset. */
    dc->desc = "sPAPR XIVE KVM interrupt controller";

    sxc->synchronize_state = spapr_xive_kvm_synchronize_state;
    sxc->pre_save = spapr_xive_kvm_get_state;
    sxc->post_load = spapr_xive_kvm_set_state;
}

static const TypeInfo spapr_xive_kvm_info = {
    .name = TYPE_SPAPR_XIVE_KVM,
    .parent = TYPE_SPAPR_XIVE,
    .instance_init = spapr_xive_kvm_init,
    .instance_size = sizeof(sPAPRXive),
    .class_init = spapr_xive_kvm_class_init,
    .class_size = sizeof(sPAPRXiveClass),
};

static void xive_kvm_register_types(void)
{
    type_register_static(&spapr_xive_kvm_info);
    type_register_static(&xive_source_kvm_info);
    type_register_static(&xive_nvt_kvm_info);
}

type_init(xive_kvm_register_types)

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

void xive_kvm_init(sPAPRXive *xive, Error **errp)
{
    XiveSource *xsrc = &xive->source;
    Error *local_err = NULL;
    size_t esb_len, tima_len;

    if (!kvm_enabled() || !kvmppc_has_cap_xive()) {
        error_setg(errp,
                   "IRQ_XIVE capability must be present for KVM XIVE device");
        return;
    }

    xive->fd = kvm_create_device(kvm_state, KVM_DEV_TYPE_XIVE, false);
    if (xive->fd < 0) {
        error_setg_errno(errp, -xive->fd, "error creating KVM XIVE device");
        return;
    }

    kvm_device_access(xive->fd, KVM_DEV_XIVE_GRP_CTRL, KVM_DEV_XIVE_VC_BASE,
                      &xsrc->esb_base, true, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /* ESB MMIO region */
    esb_len = (1ull << xsrc->esb_shift) * xsrc->nr_irqs;
    xsrc->esb_mmap = spapr_xive_kvm_mmap(xive, KVM_DEV_XIVE_GET_ESB_FD,
                                         esb_len, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    memory_region_init_ram_device_ptr(&xsrc->esb_mmio, OBJECT(xsrc),
                                      "xive.esb", esb_len, xsrc->esb_mmap);

    /* TIMA USER MMIO region */
    tima_len = 1ull << TM_SHIFT;
    xive->tm_mmap_user = spapr_xive_kvm_mmap(xive, KVM_DEV_XIVE_GET_TIMA_FD,
                                             tima_len, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    memory_region_init_ram_device_ptr(&xive->tm_mmio_user, OBJECT(xive),
                                      "xive.tima.user", tima_len,
                                      xive->tm_mmap_user);

    /* TIMA OS MMIO region */
    xive->tm_mmap_os = spapr_xive_kvm_mmap(xive, KVM_DEV_XIVE_GET_TIMA_FD,
                                           tima_len, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    memory_region_init_ram_device_ptr(&xive->tm_mmio_os, OBJECT(xive),
                                      "xive.tima.os", tima_len,
                                      xive->tm_mmap_os);

    kernel_xive_fd = xive->fd;
    kvm_kernel_irqchip = true;
    kvm_msi_via_irqfd_allowed = true;
    kvm_gsi_direct_mapping = true;
}
