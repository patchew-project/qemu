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

static void kvm_cpu_disable_all(void)
{
    KVMEnabledCPU *enabled_cpu, *next;

    QLIST_FOREACH_SAFE(enabled_cpu, &kvm_enabled_cpus, node, next) {
        QLIST_REMOVE(enabled_cpu, node);
        g_free(enabled_cpu);
    }
}

/*
 * XIVE Thread Interrupt Management context (KVM)
 */

static void kvmppc_xive_cpu_set_state(XiveTCTX *tctx, Error **errp)
{
    uint64_t state[4];
    int ret;

    /* word0 and word1 of the OS ring. */
    state[0] = *((uint64_t *) &tctx->regs[TM_QW1_OS]);

    /*
     * OS CAM line. Used by KVM to print out the VP identifier. This
     * is for debug only.
     */
    state[1] = *((uint64_t *) &tctx->regs[TM_QW1_OS + TM_WORD2]);

    ret = kvm_set_one_reg(tctx->cs, KVM_REG_PPC_NVT_STATE, state);
    if (ret != 0) {
        error_setg_errno(errp, errno, "Could restore KVM XIVE CPU %ld state",
                         kvm_arch_vcpu_id(tctx->cs));
    }
}

void kvmppc_xive_cpu_get_state(XiveTCTX *tctx, Error **errp)
{
    sPAPRXive *xive = SPAPR_MACHINE(qdev_get_machine())->xive;
    uint64_t state[4] = { 0 };
    int ret;

    /* The KVM XIVE device is not in use */
    if (xive->fd == -1) {
        return;
    }

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

    /* The KVM XIVE device is not in use */
    if (xive->fd == -1) {
        return;
    }

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
    sPAPRXive *xive = SPAPR_XIVE(xsrc->xive);
    struct kvm_irq_level args;
    int rc;

    /* The KVM XIVE device should be in use */
    assert(xive->fd != -1);

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

static int kvmppc_xive_set_eq_state(sPAPRXive *xive, CPUState *cs,
                                       Error **errp)
{
    unsigned long vcpu_id = kvm_arch_vcpu_id(cs);
    int ret;
    int i;

    for (i = 0; i < XIVE_PRIORITY_MAX + 1; i++) {
        Error *local_err = NULL;
        XiveEND *end;
        uint8_t end_blk;
        uint32_t end_idx;
        struct kvm_ppc_xive_eq kvm_eq = { 0 };
        uint64_t kvm_eq_idx;

        if (spapr_xive_priority_is_reserved(i)) {
            continue;
        }

        spapr_xive_cpu_to_end(xive, POWERPC_CPU(cs), i, &end_blk, &end_idx);
        assert(end_idx < xive->nr_ends);
        end = &xive->endt[end_idx];

        if (!xive_end_is_valid(end)) {
            continue;
        }

        /* Build the KVM state from the local END structure */
        kvm_eq.flags   = KVM_XIVE_EQ_FLAG_ALWAYS_NOTIFY;
        kvm_eq.qsize   = GETFIELD_BE32(END_W0_QSIZE, end->w0) + 12;
        kvm_eq.qpage   = (uint64_t) be32_to_cpu(end->w2 & 0x0fffffff) << 32 |
            be32_to_cpu(end->w3);
        kvm_eq.qtoggle = GETFIELD_BE32(END_W1_GENERATION, end->w1);
        kvm_eq.qindex  = GETFIELD_BE32(END_W1_PAGE_OFF, end->w1);

        /* Encode the tuple (server, prio) as a KVM EQ index */
        kvm_eq_idx = i << KVM_XIVE_EQ_PRIORITY_SHIFT &
            KVM_XIVE_EQ_PRIORITY_MASK;
        kvm_eq_idx |= vcpu_id << KVM_XIVE_EQ_SERVER_SHIFT &
            KVM_XIVE_EQ_SERVER_MASK;

        ret = kvm_device_access(xive->fd, KVM_DEV_XIVE_GRP_EQ, kvm_eq_idx,
                                &kvm_eq, true, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return ret;
        }
    }

    return 0;
}

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

static void kvmppc_xive_set_eas_state(sPAPRXive *xive, Error **errp)
{
    XiveSource *xsrc = &xive->source;
    int i;

    for (i = 0; i < xsrc->nr_irqs; i++) {
        XiveEAS *eas = &xive->eat[i];
        uint32_t end_idx;
        uint32_t end_blk;
        uint32_t eisn;
        uint8_t priority;
        uint32_t server;
        uint64_t kvm_eas;
        Error *local_err = NULL;

        /* No need to set MASKED EAS, this is the default state after reset */
        if (!xive_eas_is_valid(eas) || xive_eas_is_masked(eas)) {
            continue;
        }

        end_idx = GETFIELD_BE64(EAS_END_INDEX, eas->w);
        end_blk = GETFIELD_BE64(EAS_END_BLOCK, eas->w);
        eisn = GETFIELD_BE64(EAS_END_DATA, eas->w);

        spapr_xive_end_to_target(xive, end_blk, end_idx, &server, &priority);

        kvm_eas = priority << KVM_XIVE_EAS_PRIORITY_SHIFT &
            KVM_XIVE_EAS_PRIORITY_MASK;
        kvm_eas |= server << KVM_XIVE_EAS_SERVER_SHIFT &
            KVM_XIVE_EAS_SERVER_MASK;
        kvm_eas |= ((uint64_t)eisn << KVM_XIVE_EAS_EISN_SHIFT) &
            KVM_XIVE_EAS_EISN_MASK;

        kvm_device_access(xive->fd, KVM_DEV_XIVE_GRP_EAS, i, &kvm_eas, true,
                          &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }
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

/*
 * Sync the XIVE controller through KVM to flush any in-flight event
 * notification and stabilize the EQs.
 */
 static void kvmppc_xive_sync_all(sPAPRXive *xive, Error **errp)
{
    XiveSource *xsrc = &xive->source;
    Error *local_err = NULL;
    int i;

    /* Sync the KVM source. This reaches the XIVE HW through OPAL */
    for (i = 0; i < xsrc->nr_irqs; i++) {
        XiveEAS *eas = &xive->eat[i];

        if (!xive_eas_is_valid(eas)) {
            continue;
        }

        kvm_device_access(xive->fd, KVM_DEV_XIVE_GRP_SYNC, i, NULL, true,
                          &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }
}

/*
 * The primary goal of the XIVE VM change handler is to mark the EQ
 * pages dirty when all XIVE event notifications have stopped.
 *
 * Whenever the VM is stopped, the VM change handler masks the sources
 * (PQ=01) to stop the flow of events and saves the previous state in
 * anticipation of a migration. The XIVE controller is then synced
 * through KVM to flush any in-flight event notification and stabilize
 * the EQs.
 *
 * At this stage, we can mark the EQ page dirty and let a migration
 * sequence transfer the EQ pages to the destination, which is done
 * just after the stop state.
 *
 * The previous configuration of the sources is restored when the VM
 * runs again.
 */
static void kvmppc_xive_change_state_handler(void *opaque, int running,
                                             RunState state)
{
    sPAPRXive *xive = opaque;
    XiveSource *xsrc = &xive->source;
    Error *local_err = NULL;
    int i;

    /*
     * Restore the sources to their initial state. This is called when
     * the VM resumes after a stop or a migration.
     */
    if (running) {
        for (i = 0; i < xsrc->nr_irqs; i++) {
            uint8_t pq = xive_source_esb_get(xsrc, i);
            if (xive_esb_read(xsrc, i, XIVE_ESB_SET_PQ_00 + (pq << 8)) != 0x1) {
                error_report("XIVE: IRQ %d has an invalid state", i);
            }
        }

        return;
    }

    /*
     * Mask the sources, to stop the flow of event notifications, and
     * save the PQs locally in the XiveSource object. The XiveSource
     * state will be collected later on by its vmstate handler if a
     * migration is in progress.
     */
    for (i = 0; i < xsrc->nr_irqs; i++) {
        uint8_t pq = xive_esb_read(xsrc, i, XIVE_ESB_SET_PQ_01);
        xive_source_esb_set(xsrc, i, pq);
    }

    /*
     * Sync the XIVE controller in KVM, to flush in-flight event
     * notification that should be enqueued in the EQs.
     */
    kvmppc_xive_sync_all(xive, &local_err);
    if (local_err) {
        error_report_err(local_err);
        return;
    }

    /*
     * Mark the XIVE EQ pages dirty to collect all updates.
     */
    kvm_device_access(xive->fd, KVM_DEV_XIVE_GRP_CTRL,
                      KVM_DEV_XIVE_SAVE_EQ_PAGES, NULL, true, &local_err);
    if (local_err) {
        error_report_err(local_err);
    }
}

int kvmppc_xive_pre_save(sPAPRXive *xive)
{
    Error *local_err = NULL;
    CPUState *cs;

    /* The KVM XIVE device is not in use */
    if (xive->fd == -1) {
        return 0;
    }

    /* Grab the EAT */
    kvmppc_xive_get_eas_state(xive, &local_err);
    if (local_err) {
        error_report_err(local_err);
        return -1;
    }

    /*
     * Grab the ENDT. The EQ index and the toggle bit are what we want
     * to capture.
     */
    CPU_FOREACH(cs) {
        kvmppc_xive_get_eq_state(xive, cs, &local_err);
        if (local_err) {
            error_report_err(local_err);
            return -1;
        }
    }

    return 0;
}

/*
 * The sPAPRXive 'post_load' method is called by the sPAPR machine
 * 'post_load' method, when all XIVE states have been transferred and
 * loaded.
 */
int kvmppc_xive_post_load(sPAPRXive *xive, int version_id)
{
    Error *local_err = NULL;
    CPUState *cs;

    /* The KVM XIVE device should be in use */
    assert(xive->fd != -1);

    /* Restore the ENDT first. The targetting depends on it. */
    CPU_FOREACH(cs) {
        kvmppc_xive_set_eq_state(xive, cs, &local_err);
        if (local_err) {
            error_report_err(local_err);
            return -1;
        }
    }

    /* Restore the EAT */
    kvmppc_xive_set_eas_state(xive, &local_err);
    if (local_err) {
        error_report_err(local_err);
        return -1;
    }

    /* Restore the thread interrupt contexts */
    CPU_FOREACH(cs) {
        PowerPCCPU *cpu = POWERPC_CPU(cs);

        kvmppc_xive_cpu_set_state(XIVE_TCTX(cpu->intc), &local_err);
        if (local_err) {
            error_report_err(local_err);
            return -1;
        }
    }

    /* The source states will be restored when the machine starts running */
    return 0;
}

void kvmppc_xive_synchronize_state(sPAPRXive *xive)
{
    XiveSource *xsrc = &xive->source;
    CPUState *cs;

    /* The KVM XIVE device is not in use */
    if (xive->fd == -1) {
        return;
    }

    /*
     * When the VM is stopped, the sources are masked and the previous
     * state is saved in anticipation of a migration. We should not
     * synchronize the source state in that case else we will override
     * the saved state.
     */
    if (runstate_is_running()) {
        kvmppc_xive_source_get_state(xsrc);
    }

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
    CPUState *cs;

    /* The KVM XIVE device already in use. This is the case when
     * rebooting XIVE -> XIVE
     */
    if (xive->fd != -1) {
        return;
    }

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

    xive->change = qemu_add_vm_change_state_handler(
        kvmppc_xive_change_state_handler, xive);

    /* Connect the presenters to the initial VCPUs of the machine */
    CPU_FOREACH(cs) {
        PowerPCCPU *cpu = POWERPC_CPU(cs);

        kvmppc_xive_cpu_connect(XIVE_TCTX(cpu->intc), &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }

    /* Update the KVM sources */
    kvmppc_xive_source_reset(xsrc, &local_err);
    if (local_err) {
            error_propagate(errp, local_err);
            return;
    }

    kvm_kernel_irqchip = true;
    kvm_msi_via_irqfd_allowed = true;
    kvm_gsi_direct_mapping = true;

    /* Map all regions */
    spapr_xive_map_mmio(xive);
}

void kvmppc_xive_disconnect(sPAPRXive *xive, Error **errp)
{
    XiveSource *xsrc;
    struct kvm_create_device xive_destroy_device = { 0 };
    size_t esb_len;
    int rc;

    if (!kvm_enabled() || !kvmppc_has_cap_xive()) {
        error_setg(errp,
                   "IRQ_XIVE capability must be present for KVM XIVE device");
        return;
    }

    /* The KVM XIVE device is not in use */
    if (!xive || xive->fd == -1) {
        return;
    }

    /* Clear the KVM mapping */
    xsrc = &xive->source;
    esb_len = (1ull << xsrc->esb_shift) * xsrc->nr_irqs;

    sysbus_mmio_unmap(SYS_BUS_DEVICE(xive), 0);
    munmap(xsrc->esb_mmap, esb_len);

    sysbus_mmio_unmap(SYS_BUS_DEVICE(xive), 1);

    sysbus_mmio_unmap(SYS_BUS_DEVICE(xive), 2);
    munmap(xive->tm_mmap, 4ull << TM_SHIFT);

    /* Destroy the KVM device. This also clears the VCPU presenters */
    xive_destroy_device.fd = xive->fd;
    xive_destroy_device.type = KVM_DEV_TYPE_XIVE;
    rc = kvm_vm_ioctl(kvm_state, KVM_DESTROY_DEVICE, &xive_destroy_device);
    if (rc < 0) {
        error_setg_errno(errp, -rc, "Error on KVM_DESTROY_DEVICE for XIVE");
    }
    close(xive->fd);
    xive->fd = -1;

    kvm_kernel_irqchip = false;
    kvm_msi_via_irqfd_allowed = false;
    kvm_gsi_direct_mapping = false;

    /* Clear the local list of presenter (hotplug) */
    kvm_cpu_disable_all();

    /* VM Change state handler is not needed anymore */
    qemu_del_vm_change_state_handler(xive->change);
}
