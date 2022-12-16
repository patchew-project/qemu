/*
 * Xen HVM emulation support in KVM
 *
 * Copyright © 2019 Oracle and/or its affiliates. All rights reserved.
 * Copyright © 2022 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#define __XEN_INTERFACE_VERSION__ 0x00040e00

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/xen/xen.h"
#include "sysemu/kvm_int.h"
#include "sysemu/kvm_xen.h"
#include "kvm/kvm_i386.h"
#include "exec/address-spaces.h"
#include "xen-emu.h"
#include "trace.h"
#include "hw/pci/msi.h"
#include "hw/i386/apic-msidef.h"
#include "hw/i386/kvm/xen_overlay.h"
#include "hw/i386/kvm/xen_evtchn.h"
#include "sysemu/runstate.h"

#include "standard-headers/xen/version.h"
#include "standard-headers/xen/memory.h"
#include "standard-headers/xen/hvm/hvm_op.h"
#include "standard-headers/xen/hvm/params.h"
#include "standard-headers/xen/vcpu.h"
#include "standard-headers/xen/sched.h"
#include "standard-headers/xen/event_channel.h"

#include "xen-compat.h"

#ifdef TARGET_X86_64
#define hypercall_compat32(longmode) (!(longmode))
#else
#define hypercall_compat32(longmode) (false)
#endif

static bool kvm_gva_to_gpa(CPUState *cs, uint64_t gva, uint64_t *gpa,
                           size_t *len, bool is_write)
{
        struct kvm_translation tr = {
            .linear_address = gva,
        };

        if (len) {
            *len = TARGET_PAGE_SIZE - (gva & ~TARGET_PAGE_MASK);
        }

        if (kvm_vcpu_ioctl(cs, KVM_TRANSLATE, &tr) || !tr.valid ||
            (is_write && !tr.writeable)) {
            return false;
        }
        *gpa = tr.physical_address;
        return true;
}

static int kvm_gva_rw(CPUState *cs, uint64_t gva, void *_buf, size_t sz,
                      bool is_write)
{
    uint8_t *buf = (uint8_t *)_buf;
    uint64_t gpa;
    size_t len;

    while (sz) {
        if (!kvm_gva_to_gpa(cs, gva, &gpa, &len, is_write)) {
            return -EFAULT;
        }
        if (len > sz)
            len = sz;

        cpu_physical_memory_rw(gpa, buf, len, is_write);

        buf += len;
        sz -= len;
        gva += len;
    }

    return 0;
}

static inline int kvm_copy_from_gva(CPUState *cs, uint64_t gva, void *buf,
                                    size_t sz)
{
    return kvm_gva_rw(cs, gva, buf, sz, false);
}

static inline int kvm_copy_to_gva(CPUState *cs, uint64_t gva, void *buf,
                                  size_t sz)
{
    return kvm_gva_rw(cs, gva, buf, sz, true);
}

int kvm_xen_init(KVMState *s, uint32_t hypercall_msr)
{
    const int required_caps = KVM_XEN_HVM_CONFIG_HYPERCALL_MSR |
        KVM_XEN_HVM_CONFIG_INTERCEPT_HCALL | KVM_XEN_HVM_CONFIG_SHARED_INFO;
    struct kvm_xen_hvm_config cfg = {
        .msr = hypercall_msr,
        .flags = KVM_XEN_HVM_CONFIG_INTERCEPT_HCALL,
    };
    int xen_caps, ret;

    xen_caps = kvm_check_extension(s, KVM_CAP_XEN_HVM);
    if (required_caps & ~xen_caps) {
        error_report("kvm: Xen HVM guest support not present or insufficient");
        return -ENOSYS;
    }

    if (xen_caps & KVM_XEN_HVM_CONFIG_EVTCHN_SEND) {
        struct kvm_xen_hvm_attr ha = {
            .type = KVM_XEN_ATTR_TYPE_XEN_VERSION,
            .u.xen_version = s->xen_version,
        };
        (void)kvm_vm_ioctl(s, KVM_XEN_HVM_SET_ATTR, &ha);

        cfg.flags |= KVM_XEN_HVM_CONFIG_EVTCHN_SEND;
    }

    ret = kvm_vm_ioctl(s, KVM_XEN_HVM_CONFIG, &cfg);
    if (ret < 0) {
        error_report("kvm: Failed to enable Xen HVM support: %s", strerror(-ret));
        return ret;
    }

    return 0;
}

static bool kvm_xen_hcall_xen_version(struct kvm_xen_exit *exit, X86CPU *cpu,
                                     int cmd, uint64_t arg)
{
    int err = 0;

    switch (cmd) {
    case XENVER_get_features: {
        struct xen_feature_info fi;

        /* No need for 32/64 compat handling */
        qemu_build_assert(sizeof(fi) == 8);

        err = kvm_copy_from_gva(CPU(cpu), arg, &fi, sizeof(fi));
        if (err) {
            break;
        }

        fi.submap = 0;
        if (fi.submap_idx == 0) {
            fi.submap |= 1 << XENFEAT_writable_page_tables |
                         1 << XENFEAT_writable_descriptor_tables |
                         1 << XENFEAT_auto_translated_physmap |
                         1 << XENFEAT_supervisor_mode_kernel |
                         1 << XENFEAT_hvm_callback_vector;
        }

        err = kvm_copy_to_gva(CPU(cpu), arg, &fi, sizeof(fi));
        break;
    }

    default:
        return false;
    }

    exit->u.hcall.result = err;
    return true;
}

int kvm_xen_set_vcpu_attr(CPUState *cs, uint16_t type, uint64_t gpa)
{
    struct kvm_xen_vcpu_attr xhsi;

    xhsi.type = type;
    xhsi.u.gpa = gpa;

    trace_kvm_xen_set_vcpu_attr(cs->cpu_index, type, gpa);

    return kvm_vcpu_ioctl(cs, KVM_XEN_VCPU_SET_ATTR, &xhsi);
}

int kvm_xen_set_vcpu_callback_vector(CPUState *cs)
{
    uint8_t vector = X86_CPU(cs)->env.xen_vcpu_callback_vector;
    struct kvm_xen_vcpu_attr xva;

    xva.type = KVM_XEN_VCPU_ATTR_TYPE_UPCALL_VECTOR;
    xva.u.vector = vector;

    trace_kvm_xen_set_vcpu_callback(cs->cpu_index, vector);

    return kvm_vcpu_ioctl(cs, KVM_XEN_HVM_SET_ATTR, &xva);
}

static void do_set_vcpu_callback_vector(CPUState *cs, run_on_cpu_data data)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    env->xen_vcpu_callback_vector = data.host_int;

    kvm_xen_set_vcpu_callback_vector(cs);
}

static void do_set_vcpu_info_default_gpa(CPUState *cs, run_on_cpu_data data)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    env->xen_vcpu_info_default_gpa = data.host_ulong;

    /* Changing the default does nothing if a vcpu_info was explicitly set. */
    if (env->xen_vcpu_info_gpa == UINT64_MAX) {
        kvm_xen_set_vcpu_attr(cs, KVM_XEN_VCPU_ATTR_TYPE_VCPU_INFO,
                              env->xen_vcpu_info_default_gpa);
    }
}

static void do_set_vcpu_info_gpa(CPUState *cs, run_on_cpu_data data)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    env->xen_vcpu_info_gpa = data.host_ulong;

    kvm_xen_set_vcpu_attr(cs, KVM_XEN_VCPU_ATTR_TYPE_VCPU_INFO,
                          env->xen_vcpu_info_gpa);
}


static void *gpa_to_hva(uint64_t gpa)
{
    MemoryRegionSection mrs;

    mrs = memory_region_find(get_system_memory(), gpa, 1);
    return !mrs.mr ? NULL : qemu_map_ram_ptr(mrs.mr->ram_block,
                                             mrs.offset_within_region);
}

void *kvm_xen_get_vcpu_info_hva(uint32_t vcpu_id)
{
    CPUState *cs = qemu_get_cpu(vcpu_id);
    CPUX86State *env;
    uint64_t gpa;

    if (!cs) {
        return NULL;
    }
    env = &X86_CPU(cs)->env;

    gpa = env->xen_vcpu_info_gpa;
    if (gpa == UINT64_MAX)
        gpa = env->xen_vcpu_info_default_gpa;
    if (gpa == UINT64_MAX)
        return NULL;

    return gpa_to_hva(gpa);
}

void kvm_xen_inject_vcpu_callback_vector(uint32_t vcpu_id)
{
    CPUState *cs = qemu_get_cpu(vcpu_id);
    uint8_t vector;

    if (!cs) {
        return;
    }
    vector = X86_CPU(cs)->env.xen_vcpu_callback_vector;

    if (vector) {
        /* The per-vCPU callback vector injected via lapic. Just
         * deliver it as an MSI. */
        MSIMessage msg = {
            .address = APIC_DEFAULT_ADDRESS | X86_CPU(cs)->apic_id,
            .data = vector | (1UL << MSI_DATA_LEVEL_SHIFT),
        };
        kvm_irqchip_send_msi(kvm_state, msg);
        return;
    }

    /* If the evtchn_upcall_pending field in the vcpu_info is set, then
     * KVM will automatically deliver the vector on entering the vCPU
     * so all we have to do is kick it out. */
    qemu_cpu_kick(cs);
}

static void do_set_vcpu_time_info_gpa(CPUState *cs, run_on_cpu_data data)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    env->xen_vcpu_time_info_gpa = data.host_ulong;

    kvm_xen_set_vcpu_attr(cs, KVM_XEN_VCPU_ATTR_TYPE_VCPU_TIME_INFO,
                          env->xen_vcpu_time_info_gpa);
}

static void do_set_vcpu_runstate_gpa(CPUState *cs, run_on_cpu_data data)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    env->xen_vcpu_runstate_gpa = data.host_ulong;

    kvm_xen_set_vcpu_attr(cs, KVM_XEN_VCPU_ATTR_TYPE_RUNSTATE_ADDR,
                          env->xen_vcpu_runstate_gpa);
}

static int xen_set_shared_info(uint64_t gfn)
{
    uint64_t gpa = gfn << TARGET_PAGE_BITS;
    int i, err;

    /* The xen_overlay device tells KVM about it too, since it had to
     * do that on migration load anyway (unless we're going to jump
     * through lots of hoops to maintain the fiction that this isn't
     * KVM-specific */
    err = xen_overlay_map_page(XENMAPSPACE_shared_info, 0, gpa);
    if (err)
            return err;

    trace_kvm_xen_set_shared_info(gfn);

    for (i = 0; i < XEN_LEGACY_MAX_VCPUS; i++) {
        CPUState *cpu = qemu_get_cpu(i);
        if (cpu) {
            async_run_on_cpu(cpu, do_set_vcpu_info_default_gpa, RUN_ON_CPU_HOST_ULONG(gpa));
        }
        gpa += sizeof(vcpu_info_t);
    }

    return err;
}

static int add_to_physmap_one(uint32_t space, uint64_t idx, uint64_t gfn)
{
    switch (space) {
    case XENMAPSPACE_shared_info:
        if (idx > 0) {
            return -EINVAL;
        }
        return xen_set_shared_info(gfn);

    case XENMAPSPACE_grant_table:
    case XENMAPSPACE_gmfn:
    case XENMAPSPACE_gmfn_range:
        return -ENOTSUP;

    case XENMAPSPACE_gmfn_foreign:
    case XENMAPSPACE_dev_mmio:
        return -EPERM;

    default:
        return -EINVAL;;
    }
}

static int do_add_to_physmap(struct kvm_xen_exit *exit, X86CPU *cpu, uint64_t arg)
{
    struct xen_add_to_physmap xatp;
    CPUState *cs = CPU(cpu);

    if (hypercall_compat32(exit->u.hcall.longmode)) {
        struct compat_xen_add_to_physmap xatp32;

        qemu_build_assert(sizeof(struct compat_xen_add_to_physmap) == 16);
        if (kvm_copy_from_gva(cs, arg, &xatp32, sizeof(xatp32))) {
            return -EFAULT;
        }
        xatp.domid = xatp32.domid;
        xatp.size = xatp32.size;
        xatp.space = xatp32.space;
        xatp.idx = xatp32.idx;
        xatp.gpfn = xatp32.gpfn;
    } else {
        if (kvm_copy_from_gva(cs, arg, &xatp, sizeof(xatp))) {
            return -EFAULT;
        }
    }

    if (xatp.domid != DOMID_SELF && xatp.domid != xen_domid) {
        return -ESRCH;
    }

    return add_to_physmap_one(xatp.space, xatp.idx, xatp.gpfn);
}

static int do_add_to_physmap_batch(struct kvm_xen_exit *exit, X86CPU *cpu,
                                   uint64_t arg)
{
    struct xen_add_to_physmap_batch xatpb;
    unsigned long idxs_gva, gpfns_gva, errs_gva;
    CPUState *cs = CPU(cpu);
    size_t op_sz;

    if (hypercall_compat32(exit->u.hcall.longmode)) {
        struct compat_xen_add_to_physmap_batch xatpb32;

        qemu_build_assert(sizeof(struct compat_xen_add_to_physmap_batch) == 20);
        if (kvm_copy_from_gva(cs, arg, &xatpb32, sizeof(xatpb32))) {
            return -EFAULT;
        }
        xatpb.domid = xatpb32.domid;
        xatpb.space = xatpb32.space;
        xatpb.size = xatpb32.size;

        idxs_gva = xatpb32.idxs.c;
        gpfns_gva = xatpb32.gpfns.c;
        errs_gva = xatpb32.errs.c;
        op_sz = sizeof(uint32_t);;
    } else {
        if (kvm_copy_from_gva(cs, arg, &xatpb, sizeof(xatpb))) {
            return -EFAULT;
        }
        op_sz = sizeof(unsigned long);
        idxs_gva = (unsigned long)xatpb.idxs.p;
        gpfns_gva = (unsigned long)xatpb.gpfns.p;
        errs_gva = (unsigned long)xatpb.errs.p;
    }

    if (xatpb.domid != DOMID_SELF && xatpb.domid != xen_domid) {
        return -ESRCH;
    }

    /* Explicitly invalid for the batch op. Not that we implement it anyway. */
    if (xatpb.space == XENMAPSPACE_gmfn_range) {
        return -EINVAL;
    }

    while (xatpb.size--) {
        unsigned long idx = 0;
        unsigned long gpfn = 0;
        int err;

        /* For 32-bit compat this only copies the low 32 bits of each */
        if (kvm_copy_from_gva(cs, idxs_gva, &idx, op_sz) ||
            kvm_copy_from_gva(cs, gpfns_gva, &gpfn, op_sz)) {
            return -EFAULT;
        }
        idxs_gva += op_sz;
        gpfns_gva += op_sz;

        err = add_to_physmap_one(xatpb.space, idx, gpfn);

        if (kvm_copy_to_gva(cs, errs_gva, &err, sizeof(err))) {
            return -EFAULT;
        }
        errs_gva += sizeof(err);
    }
    return 0;
}

static bool kvm_xen_hcall_memory_op(struct kvm_xen_exit *exit, X86CPU *cpu,
                                   int cmd, uint64_t arg)
{
    int err;

    switch (cmd) {
    case XENMEM_add_to_physmap:
        err = do_add_to_physmap(exit, cpu, arg);
        break;

    case XENMEM_add_to_physmap_batch:
        err = do_add_to_physmap_batch(exit, cpu, arg);
        break;

    default:
        return false;
    }

    exit->u.hcall.result = err;
    return true;
}

static bool handle_set_param(struct kvm_xen_exit *exit, X86CPU *cpu,
                             uint64_t arg)
{
    CPUState *cs = CPU(cpu);
    struct xen_hvm_param hp;
    int err = 0;

    /* No need for 32/64 compat handling */
    qemu_build_assert(sizeof(hp) == 16);

    if (kvm_copy_from_gva(cs, arg, &hp, sizeof(hp))) {
        err = -EFAULT;
        goto out;
    }

    if (hp.domid != DOMID_SELF) {
        err = -EINVAL;
        goto out;
    }

    switch (hp.index) {
    case HVM_PARAM_CALLBACK_IRQ:
        err = xen_evtchn_set_callback_param(hp.value);
        xen_set_long_mode(exit->u.hcall.longmode);
        break;
    default:
        return false;
    }

out:
    exit->u.hcall.result = err;
    return true;
}

static int kvm_xen_hcall_evtchn_upcall_vector(struct kvm_xen_exit *exit,
                                              X86CPU *cpu, uint64_t arg)
{
    struct xen_hvm_evtchn_upcall_vector up;
    CPUState *target_cs;

    /* No need for 32/64 compat handling */
    qemu_build_assert(sizeof(up) == 8);

    if (kvm_copy_from_gva(CPU(cpu), arg, &up, sizeof(up))) {
        return -EFAULT;
    }

    if (up.vector < 0x10) {
        return -EINVAL;
    }

    target_cs = qemu_get_cpu(up.vcpu);
    if (!target_cs) {
        return -EINVAL;
    }

    async_run_on_cpu(target_cs, do_set_vcpu_callback_vector, RUN_ON_CPU_HOST_INT(up.vector));
    return 0;
}

static bool kvm_xen_hcall_hvm_op(struct kvm_xen_exit *exit, X86CPU *cpu,
                                 int cmd, uint64_t arg)
{
    int ret = -ENOSYS;
    switch (cmd) {
    case HVMOP_set_evtchn_upcall_vector:
        ret = kvm_xen_hcall_evtchn_upcall_vector(exit, cpu,
                                                 exit->u.hcall.params[0]);
        break;
    case HVMOP_pagetable_dying:
        ret = -ENOSYS;
        break;

    case HVMOP_set_param:
        return handle_set_param(exit, cpu, arg);

    default:
        return false;
    }

    exit->u.hcall.result = ret;
    return true;
}

static int vcpuop_register_vcpu_info(CPUState *cs, CPUState *target,
                                     uint64_t arg)
{
    struct vcpu_register_vcpu_info rvi;
    uint64_t gpa;

    /* No need for 32/64 compat handling */
    qemu_build_assert(sizeof(rvi) == 16);
    qemu_build_assert(sizeof(struct vcpu_info) == 64);

    if (!target)
        return -ENOENT;

    if (kvm_copy_from_gva(cs, arg, &rvi, sizeof(rvi))) {
        return -EFAULT;
    }

    if (rvi.offset > TARGET_PAGE_SIZE - sizeof(struct vcpu_info)) {
        return -EINVAL;
    }

    gpa = ((rvi.mfn << TARGET_PAGE_BITS) + rvi.offset);
    async_run_on_cpu(target, do_set_vcpu_info_gpa, RUN_ON_CPU_HOST_ULONG(gpa));
    return 0;
}

static int vcpuop_register_vcpu_time_info(CPUState *cs, CPUState *target,
                                          uint64_t arg)
{
    struct vcpu_register_time_memory_area tma;
    uint64_t gpa;
    size_t len;

    /* No need for 32/64 compat handling */
    qemu_build_assert(sizeof(tma) == 8);
    qemu_build_assert(sizeof(struct vcpu_time_info) == 32);

    if (!target)
        return -ENOENT;

    if (kvm_copy_from_gva(cs, arg, &tma, sizeof(tma))) {
        return -EFAULT;
    }

    /*
     * Xen actually uses the GVA and does the translation through the guest
     * page tables each time. But Linux/KVM uses the GPA, on the assumption
     * that guests only ever use *global* addresses (kernel virtual addresses)
     * for it. If Linux is changed to redo the GVA→GPA translation each time,
     * it will offer a new vCPU attribute for that, and we'll use it instead.
     */
    if (!kvm_gva_to_gpa(cs, tma.addr.p, &gpa, &len, false) ||
        len < sizeof(struct vcpu_time_info)) {
        return -EFAULT;
    }

    async_run_on_cpu(target, do_set_vcpu_time_info_gpa,
                     RUN_ON_CPU_HOST_ULONG(gpa));
    return 0;
}

static int vcpuop_register_runstate_info(CPUState *cs, CPUState *target,
                                         uint64_t arg)
{
    struct vcpu_register_runstate_memory_area rma;
    uint64_t gpa;
    size_t len;

    /* No need for 32/64 compat handling */
    qemu_build_assert(sizeof(rma) == 8);
    /* The runstate area actually does change size, but Linux copes. */

    if (!target)
        return -ENOENT;

    if (kvm_copy_from_gva(cs, arg, &rma, sizeof(rma))) {
        return -EFAULT;
    }

    /* As with vcpu_time_info, Xen actually uses the GVA but KVM doesn't. */
    if (!kvm_gva_to_gpa(cs, rma.addr.p, &gpa, &len, false)) {
        return -EFAULT;
    }

    async_run_on_cpu(target, do_set_vcpu_runstate_gpa,
                     RUN_ON_CPU_HOST_ULONG(gpa));
    return 0;
}

static bool kvm_xen_hcall_vcpu_op(struct kvm_xen_exit *exit, X86CPU *cpu,
                                  int cmd, int vcpu_id, uint64_t arg)
{
    CPUState *dest = qemu_get_cpu(vcpu_id);
    CPUState *cs = CPU(cpu);
    int err;

    switch (cmd) {
    case VCPUOP_register_runstate_memory_area:
        err = vcpuop_register_runstate_info(cs, dest, arg);
        break;
    case VCPUOP_register_vcpu_time_memory_area:
        err = vcpuop_register_vcpu_time_info(cs, dest, arg);
        break;
    case VCPUOP_register_vcpu_info:
        err = vcpuop_register_vcpu_info(cs, dest, arg);
        break;

    default:
        return false;
    }

    exit->u.hcall.result = err;
    return true;
}

static bool kvm_xen_hcall_evtchn_op(struct kvm_xen_exit *exit, X86CPU *cpu,
                                    int cmd, uint64_t arg)
{
    CPUState *cs = CPU(cpu);
    int err = -ENOSYS;

    switch (cmd) {
    case EVTCHNOP_init_control:
        err = -ENOSYS;
        break;

    case EVTCHNOP_status: {
        struct evtchn_status status;

        qemu_build_assert(sizeof(status) == 24);
        if (kvm_copy_from_gva(cs, arg, &status, sizeof(status))) {
            err = -EFAULT;
            break;
        }

        err = xen_evtchn_status_op(&status);
        if (!err && kvm_copy_to_gva(cs, arg, &status, sizeof(status))) {
            err = -EFAULT;
        }
        break;
    }
    case EVTCHNOP_close: {
        struct evtchn_close close;

        qemu_build_assert(sizeof(close) == 4);
        if (kvm_copy_from_gva(cs, arg, &close, sizeof(close))) {
            err = -EFAULT;
            break;
        }

        err = xen_evtchn_close_op(&close);
        break;
    }
    case EVTCHNOP_unmask: {
        struct evtchn_unmask unmask;

        qemu_build_assert(sizeof(unmask) == 4);
        if (kvm_copy_from_gva(cs, arg, &unmask, sizeof(unmask))) {
            err = -EFAULT;
            break;
        }

        err = xen_evtchn_unmask_op(&unmask);
        break;
    }
    default:
        return false;
    }

    exit->u.hcall.result = err;
    return true;
}

static int schedop_shutdown(CPUState *cs, uint64_t arg)
{
    struct sched_shutdown shutdown;
    int ret = 0;

    /* No need for 32/64 compat handling */
    qemu_build_assert(sizeof(shutdown) == 4);

    if (kvm_copy_from_gva(cs, arg, &shutdown, sizeof(shutdown))) {
        return -EFAULT;
    }

    switch(shutdown.reason) {
    case SHUTDOWN_crash:
        cpu_dump_state(cs, stderr, CPU_DUMP_CODE);
        qemu_system_guest_panicked(NULL);
        break;

    case SHUTDOWN_reboot:
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        break;

    case SHUTDOWN_poweroff:
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
        break;

    default:
            ret = -EINVAL;
            break;
    }

    return ret;
}

static bool kvm_xen_hcall_sched_op(struct kvm_xen_exit *exit, X86CPU *cpu,
                                   int cmd, uint64_t arg)
{
    CPUState *cs = CPU(cpu);
    int err = -ENOSYS;

    switch (cmd) {
    case SCHEDOP_shutdown:
        err = schedop_shutdown(cs, arg);
        break;

    default:
        return false;
    }

    exit->u.hcall.result = err;
    return true;
}

static bool do_kvm_xen_handle_exit(X86CPU *cpu, struct kvm_xen_exit *exit)
{
    uint16_t code = exit->u.hcall.input;

    if (exit->u.hcall.cpl > 0) {
        exit->u.hcall.result = -EPERM;
        return true;
    }

    switch (code) {
    case __HYPERVISOR_sched_op:
        return kvm_xen_hcall_sched_op(exit, cpu, exit->u.hcall.params[0],
                                      exit->u.hcall.params[1]);
    case __HYPERVISOR_event_channel_op:
        return kvm_xen_hcall_evtchn_op(exit, cpu, exit->u.hcall.params[0],
                                       exit->u.hcall.params[1]);
    case __HYPERVISOR_vcpu_op:
        return kvm_xen_hcall_vcpu_op(exit, cpu,
                                     exit->u.hcall.params[0],
                                     exit->u.hcall.params[1],
                                     exit->u.hcall.params[2]);
    case __HYPERVISOR_hvm_op:
        return kvm_xen_hcall_hvm_op(exit, cpu, exit->u.hcall.params[0],
                                    exit->u.hcall.params[1]);
    case __HYPERVISOR_memory_op:
        return kvm_xen_hcall_memory_op(exit, cpu, exit->u.hcall.params[0],
                                       exit->u.hcall.params[1]);
    case __HYPERVISOR_xen_version:
        return kvm_xen_hcall_xen_version(exit, cpu, exit->u.hcall.params[0],
                                         exit->u.hcall.params[1]);
    default:
        return false;
    }
}

int kvm_xen_handle_exit(X86CPU *cpu, struct kvm_xen_exit *exit)
{
    if (exit->type != KVM_EXIT_XEN_HCALL)
        return -1;

    /* The kernel latches the guest 32/64 mode when the MSR is used to fill
     * the hypercall page. So if we see a hypercall in a mode that doesn't
     * match our own idea of the guest mode, fetch the kernel's idea of the
     * "long mode" to remain in sync. */
    if (exit->u.hcall.longmode != xen_is_long_mode()) {
        xen_sync_long_mode();
    }

    if (!do_kvm_xen_handle_exit(cpu, exit)) {
        /* Some hypercalls will be deliberately "implemented" by returning
         * -ENOSYS. This case is for hypercalls which are unexpected. */
        exit->u.hcall.result = -ENOSYS;
        qemu_log_mask(LOG_UNIMP, "Unimplemented Xen hypercall %"
                      PRId64 " (0x%" PRIx64 " 0x%" PRIx64 " 0x%" PRIx64 ")\n",
                      (uint64_t)exit->u.hcall.input, (uint64_t)exit->u.hcall.params[0],
                      (uint64_t)exit->u.hcall.params[1], (uint64_t)exit->u.hcall.params[1]);
    }

    trace_kvm_xen_hypercall(CPU(cpu)->cpu_index, exit->u.hcall.cpl,
                            exit->u.hcall.input, exit->u.hcall.params[0],
                            exit->u.hcall.params[1], exit->u.hcall.params[2],
                            exit->u.hcall.result);
    return 0;
}
