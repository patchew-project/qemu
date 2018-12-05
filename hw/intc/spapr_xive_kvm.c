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
 *
 * TODO: make a common KVMEnabledCPU layer for XICS and XIVE
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
static void kvmppc_xive_cpu_get_state(XiveTCTX *tctx, Error **errp)
{
    uint64_t state[4] = { 0 };
    int ret;

    ret = kvm_get_one_reg(tctx->cs, KVM_REG_PPC_NVT_STATE, state);
    if (ret != 0) {
        error_setg_errno(errp, errno, "Could capture KVM XIVE CPU %ld state",
                         kvm_arch_vcpu_id(tctx->cs));
        return;
    }

    /* word0 and word1 of the OS ring. */
    *((uint64_t *) &tctx->regs[TM_QW1_OS]) = state[0];

    /*
     * KVM also returns word2 containing the OS CAM line which is
     * interesting to print out in the QEMU monitor.
     */
    *((uint64_t *) &tctx->regs[TM_QW1_OS + TM_WORD2]) = state[1];
}

static void kvmppc_xive_cpu_do_synchronize_state(CPUState *cpu,
                                              run_on_cpu_data arg)
{
    kvmppc_xive_cpu_get_state(arg.host_ptr, &error_fatal);
}

void kvmppc_xive_cpu_synchronize_state(XiveTCTX *tctx)
{
    run_on_cpu(tctx->cs, kvmppc_xive_cpu_do_synchronize_state,
               RUN_ON_CPU_HOST_PTR(tctx));
}

void kvmppc_xive_cpu_connect(XiveTCTX *tctx, Error **errp)
{
    sPAPRXive *xive = SPAPR_MACHINE(qdev_get_machine())->xive;
    unsigned long vcpu_id;
    int ret;

    /* Check if CPU was hot unplugged and replugged. */
    if (kvm_cpu_is_enabled(tctx->cs)) {
        return;
    }

    vcpu_id = kvm_arch_vcpu_id(tctx->cs);

    ret = kvm_vcpu_enable_cap(tctx->cs, KVM_CAP_PPC_IRQ_XIVE, 0, xive->fd,
                              vcpu_id, 0);
    if (ret < 0) {
        error_setg(errp, "Unable to connect CPU%ld to KVM XIVE device: %s",
                   vcpu_id, strerror(errno));
        return;
    }

    kvm_cpu_enable(tctx->cs);
}

/*
 * XIVE Interrupt Source (KVM)
 */

/*
 * At reset, the interrupt sources are simply created and MASKED. We
 * only need to inform the KVM XIVE device about their type: LSI or
 * MSI.
 */
void kvmppc_xive_source_reset(XiveSource *xsrc, Error **errp)
{
    sPAPRXive *xive = SPAPR_XIVE(xsrc->xive);
    int i;

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

/*
 * This is used to perform the magic loads on the ESB pages, described
 * in xive.h.
 */
static uint8_t xive_esb_read(XiveSource *xsrc, int srcno, uint32_t offset)
{
    unsigned long addr = (unsigned long) xsrc->esb_mmap +
        xive_source_esb_mgmt(xsrc, srcno) + offset;

    /* Prevent the compiler from optimizing away the load */
    volatile uint64_t value = *((uint64_t *) addr);

    return be64_to_cpu(value) & 0x3;
}

static void kvmppc_xive_source_get_state(XiveSource *xsrc)
{
    int i;

    for (i = 0; i < xsrc->nr_irqs; i++) {
        /* Perform a load without side effect to retrieve the PQ bits */
        uint8_t pq = xive_esb_read(xsrc, i, XIVE_ESB_GET);

        /* and save PQ locally */
        xive_source_esb_set(xsrc, i, pq);
    }
}

void kvmppc_xive_source_set_irq(void *opaque, int srcno, int val)
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

/*
 * sPAPR XIVE interrupt controller (KVM)
 */
static int kvmppc_xive_get_eq_state(sPAPRXive *xive, CPUState *cs,
                                       Error **errp)
{
    unsigned long vcpu_id = kvm_arch_vcpu_id(cs);
    int ret;
    int i;

    for (i = 0; i < XIVE_PRIORITY_MAX + 1; i++) {
        Error *local_err = NULL;
        struct kvm_ppc_xive_eq kvm_eq = { 0 };
        uint64_t kvm_eq_idx;
        XiveEND end = { 0 };
        uint8_t end_blk, nvt_blk;
        uint32_t end_idx, nvt_idx;

        /* Skip priorities reserved for the hypervisor */
        if (spapr_xive_priority_is_reserved(i)) {
            continue;
        }

        /* Encode the tuple (server, prio) as a KVM EQ index */
        kvm_eq_idx = i << KVM_XIVE_EQ_PRIORITY_SHIFT &
            KVM_XIVE_EQ_PRIORITY_MASK;
        kvm_eq_idx |= vcpu_id << KVM_XIVE_EQ_SERVER_SHIFT &
            KVM_XIVE_EQ_SERVER_MASK;

        ret = kvm_device_access(xive->fd, KVM_DEV_XIVE_GRP_EQ, kvm_eq_idx,
                                &kvm_eq, false, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return ret;
        }

        if (!(kvm_eq.flags & KVM_XIVE_EQ_FLAG_ENABLED)) {
            continue;
        }

        /* Update the local END structure with the KVM input */
        if (kvm_eq.flags & KVM_XIVE_EQ_FLAG_ENABLED) {
            end.w0 |= cpu_to_be32(END_W0_VALID | END_W0_ENQUEUE);
        }
        if (kvm_eq.flags & KVM_XIVE_EQ_FLAG_ALWAYS_NOTIFY) {
            end.w0 |= cpu_to_be32(END_W0_UCOND_NOTIFY);
        }
        if (kvm_eq.flags & KVM_XIVE_EQ_FLAG_ESCALATE) {
            end.w0 |= cpu_to_be32(END_W0_ESCALATE_CTL);
        }
        end.w0 |= SETFIELD_BE32(END_W0_QSIZE, 0ul, kvm_eq.qsize - 12);

        end.w1 = SETFIELD_BE32(END_W1_GENERATION, 0ul, kvm_eq.qtoggle) |
            SETFIELD_BE32(END_W1_PAGE_OFF, 0ul, kvm_eq.qindex);
        end.w2 = cpu_to_be32((kvm_eq.qpage >> 32) & 0x0fffffff);
        end.w3 = cpu_to_be32(kvm_eq.qpage & 0xffffffff);
        end.w4 = 0;
        end.w5 = 0;

        spapr_xive_cpu_to_nvt(xive, POWERPC_CPU(cs), &nvt_blk, &nvt_idx);

        end.w6 = SETFIELD_BE32(END_W6_NVT_BLOCK, 0ul, nvt_blk) |
            SETFIELD_BE32(END_W6_NVT_INDEX, 0ul, nvt_idx);
        end.w7 = SETFIELD_BE32(END_W7_F0_PRIORITY, 0ul, i);

        spapr_xive_cpu_to_end(xive, POWERPC_CPU(cs), i, &end_blk, &end_idx);

        assert(end_idx < xive->nr_ends);
        memcpy(&xive->endt[end_idx], &end, sizeof(XiveEND));
    }

    return 0;
}

static void kvmppc_xive_get_eas_state(sPAPRXive *xive, Error **errp)
{
    XiveSource *xsrc = &xive->source;
    int i;

    for (i = 0; i < xsrc->nr_irqs; i++) {
        XiveEAS *eas = &xive->eat[i];
        XiveEAS new_eas;
        uint64_t kvm_eas;
        uint8_t priority;
        uint32_t server;
        uint32_t end_idx;
        uint8_t end_blk;
        uint32_t eisn;
        Error *local_err = NULL;

        if (!xive_eas_is_valid(eas)) {
            continue;
        }

        kvm_device_access(xive->fd, KVM_DEV_XIVE_GRP_EAS, i, &kvm_eas, false,
                          &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }

        priority = (kvm_eas & KVM_XIVE_EAS_PRIORITY_MASK) >>
            KVM_XIVE_EAS_PRIORITY_SHIFT;
        server = (kvm_eas & KVM_XIVE_EAS_SERVER_MASK) >>
            KVM_XIVE_EAS_SERVER_SHIFT;
        eisn = (kvm_eas & KVM_XIVE_EAS_EISN_MASK) >> KVM_XIVE_EAS_EISN_SHIFT;

        if (spapr_xive_target_to_end(xive, server, priority, &end_blk,
                                     &end_idx)) {
            error_setg(errp, "XIVE: invalid tuple CPU %d priority %d", server,
                       priority);
            return;
        }

        new_eas.w = cpu_to_be64(EAS_VALID);
        if (kvm_eas & KVM_XIVE_EAS_MASK_MASK) {
            new_eas.w |= cpu_to_be64(EAS_MASKED);
        }

        new_eas.w = SETFIELD_BE64(EAS_END_INDEX, new_eas.w, end_idx);
        new_eas.w = SETFIELD_BE64(EAS_END_BLOCK, new_eas.w, end_blk);
        new_eas.w = SETFIELD_BE64(EAS_END_DATA, new_eas.w, eisn);

        *eas = new_eas;
    }
}

void kvmppc_xive_synchronize_state(sPAPRXive *xive)
{
    XiveSource *xsrc = &xive->source;
    CPUState *cs;

    kvmppc_xive_source_get_state(xsrc);

    kvmppc_xive_get_eas_state(xive, &error_fatal);

    CPU_FOREACH(cs) {
        kvmppc_xive_get_eq_state(xive, cs, &error_fatal);
    }
}

static void *kvmppc_xive_mmap(sPAPRXive *xive, int ctrl, size_t len,
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
 * All the XIVE memory regions are now backed by mappings from the KVM
 * XIVE device.
 */
void kvmppc_xive_connect(sPAPRXive *xive, Error **errp)
{
    XiveSource *xsrc = &xive->source;
    XiveENDSource *end_xsrc = &xive->end_source;
    Error *local_err = NULL;
    size_t esb_len;
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

    esb_len = (1ull << xsrc->esb_shift) * xsrc->nr_irqs;
    xsrc->esb_mmap = kvmppc_xive_mmap(xive, KVM_DEV_XIVE_GET_ESB_FD,
                                      esb_len, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    memory_region_init_ram_device_ptr(&xsrc->esb_mmio, OBJECT(xsrc),
                                      "xive.esb", esb_len, xsrc->esb_mmap);
    sysbus_init_mmio(SYS_BUS_DEVICE(xive), &xsrc->esb_mmio);

    /* END ESBs mapping (No KVM) */
    sysbus_init_mmio(SYS_BUS_DEVICE(xive), &end_xsrc->esb_mmio);

    /* TIMA KVM mapping
     *
     * We could also inform KVM where the TIMA will be mapped but as
     * this is a fixed MMIO address for the system it does not seem
     * necessary to provide a KVM ioctl to change it.
     */
    tima_len = 4ull << TM_SHIFT;
    xive->tm_mmap = kvmppc_xive_mmap(xive, KVM_DEV_XIVE_GET_TIMA_FD,
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

    /* Map all regions */
    spapr_xive_map_mmio(xive);
}
