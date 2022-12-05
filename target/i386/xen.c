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

#include "qemu/osdep.h"
#include "kvm/kvm_i386.h"
#include "exec/address-spaces.h"
#include "xen.h"
#include "trace.h"
#include "sysemu/sysemu.h"

#define __XEN_INTERFACE_VERSION__ 0x00040400

#include "standard-headers/xen/version.h"
#include "standard-headers/xen/memory.h"
#include "standard-headers/xen/hvm/hvm_op.h"
#include "standard-headers/xen/hvm/params.h"
#include "standard-headers/xen/vcpu.h"
#include "standard-headers/xen/event_channel.h"

#define PAGE_OFFSET    0xffffffff80000000UL
#define PAGE_SHIFT     12

/*
 * Unhandled hypercalls error:
 *
 * -1 crash and dump registers
 *  0 no abort and guest handles -ENOSYS (default)
 */
#ifndef HCALL_ERR
#define HCALL_ERR      0
#endif

static QemuMutex xen_global_mutex;

static void *gpa_to_hva(uint64_t gpa)
{
    MemoryRegionSection mrs;

    mrs = memory_region_find(get_system_memory(), gpa, 1);
    return !mrs.mr ? NULL : qemu_map_ram_ptr(mrs.mr->ram_block,
                                             mrs.offset_within_region);
}

static uint64_t gva_to_gpa(CPUState *cs, uint64_t gva)
{
    struct kvm_translation t = { .linear_address = gva };
    int err;

    err = kvm_vcpu_ioctl(cs, KVM_TRANSLATE, &t);
    if (err || !t.valid) {
        return 0;
    }

    return t.physical_address;
}

static void *gva_to_hva(CPUState *cs, uint64_t gva)
{
    return gpa_to_hva(gva_to_gpa(cs, gva));
}

int kvm_xen_init(KVMState *s, uint32_t xen_version)
{
    const int required_caps = KVM_XEN_HVM_CONFIG_HYPERCALL_MSR |
        KVM_XEN_HVM_CONFIG_INTERCEPT_HCALL | KVM_XEN_HVM_CONFIG_SHARED_INFO;
    struct kvm_xen_hvm_config cfg = {
        .msr = XEN_HYPERCALL_MSR,
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
            .u.xen_version = xen_version,
        };
        (void)kvm_vm_ioctl(s, KVM_XEN_HVM_SET_ATTR, &ha);

        cfg.flags |= KVM_XEN_HVM_CONFIG_EVTCHN_SEND;
    }

    ret = kvm_vm_ioctl(s, KVM_XEN_HVM_CONFIG, &cfg);
    if (ret < 0) {
        error_report("kvm: Failed to enable Xen HVM support: %s", strerror(-ret));
        return ret;
    }

    qemu_mutex_init(&xen_global_mutex);

    return 0;
}

static int kvm_xen_hcall_xen_version(struct kvm_xen_exit *exit, X86CPU *cpu,
                                     int cmd, uint64_t arg)
{
    int err = 0;

    switch (cmd) {
    case XENVER_get_features: {
            struct xen_feature_info *fi;

            fi = gva_to_hva(CPU(cpu), arg);
            if (!fi) {
                err = -EFAULT;
                break;
            }

            if (fi->submap_idx != 0) {
                err = -EINVAL;
                break;
            }

            /*
             * There's only HVM guests and we only expose what
             * we intend to support. These are left in the open
             * whether we should or not support them:
             *
             *   XENFEAT_memory_op_vnode_supported
             *   XENFEAT_writable_page_tables
             */
            fi->submap = (1U << XENFEAT_auto_translated_physmap) |
                         (1U << XENFEAT_hvm_callback_vector);
            break;
         }
    }

    exit->u.hcall.result = err;
    return err ? HCALL_ERR : 0;
}

static int xen_set_shared_info(CPUState *cs, struct shared_info *shi,
                               uint64_t gfn)
{
    struct kvm_xen_hvm_attr xhsi;
    XenState *xen = cs->xen_state;
    KVMState *s = cs->kvm_state;
    XenCPUState *xcpu;
    CPUState *cpu;
    int i, err;

    xhsi.type = KVM_XEN_ATTR_TYPE_SHARED_INFO;
    xhsi.u.shared_info.gfn = gfn;
    err = kvm_vm_ioctl(s, KVM_XEN_HVM_SET_ATTR, &xhsi);
    trace_kvm_xen_set_shared_info(gfn);
    xen->shared_info = shi;

    for (i = 0; i < XEN_LEGACY_MAX_VCPUS; i++) {
        cpu = qemu_get_cpu(i);
        if (cpu) {
                xcpu = &X86_CPU(cpu)->env.xen_vcpu;
                xcpu->info = &shi->vcpu_info[cpu->cpu_index];
        }
    }

    return err;
}

static int kvm_xen_hcall_memory_op(struct kvm_xen_exit *exit,
                                   int cmd, uint64_t arg, X86CPU *cpu)
{
    CPUState *cs = CPU(cpu);
    int err = 0;

    switch (cmd) {
    case XENMEM_add_to_physmap: {
            struct xen_add_to_physmap *xatp;
            struct shared_info *shi;

            xatp = gva_to_hva(cs, arg);
            if (!xatp) {
                err = -EFAULT;
                break;
            }

            switch (xatp->space) {
            case XENMAPSPACE_shared_info:
                break;
            default:
                err = -ENOSYS;
                break;
            }

            shi = gpa_to_hva(xatp->gpfn << PAGE_SHIFT);
            if (!shi) {
                err = -EFAULT;
                break;
            }

            err = xen_set_shared_info(cs, shi, xatp->gpfn);
            break;
         }
    }

    exit->u.hcall.result = err;
    return err ? HCALL_ERR : 0;
}

static void xen_vcpu_set_callback(CPUState *cs, run_on_cpu_data data)
{
    struct kvm_xen_vcpu_attr xvuv;
    uint8_t vector = data.host_int;
    int err;

    xvuv.type = KVM_XEN_VCPU_ATTR_TYPE_UPCALL_VECTOR;
    xvuv.u.vector = vector;
    err = kvm_vcpu_ioctl(cs, KVM_XEN_HVM_SET_ATTR, &xvuv);
    if (err < 0) {
        return;
    }

    trace_kvm_xen_set_vcpu_callback(cs->cpu_index, vector);
}

static int handle_set_param(struct kvm_xen_exit *exit, X86CPU *cpu,
                            uint64_t arg)
{
    CPUState *cs = CPU(cpu);
    struct xen_hvm_param *hp;
    int err = 0, via;

    hp = gva_to_hva(cs, arg);
    if (!hp) {
        err = -EFAULT;
        goto out;
    }

    if (hp->domid != DOMID_SELF) {
        err = -EINVAL;
        goto out;
    }

#define CALLBACK_VIA_TYPE_SHIFT       56
#define CALLBACK_VIA_TYPE_GSI         0x0
#define CALLBACK_VIA_TYPE_PCI_INTX    0x1
#define CALLBACK_VIA_TYPE_VECTOR      0x2
#define CALLBACK_VIA_TYPE_EVTCHN      0x3
    switch (hp->index) {
    case HVM_PARAM_CALLBACK_IRQ:
        via = hp->value >> CALLBACK_VIA_TYPE_SHIFT;
        if (via == CALLBACK_VIA_TYPE_GSI ||
            via == CALLBACK_VIA_TYPE_PCI_INTX) {
            err = -ENOSYS;
            goto out;
        } else if (via == CALLBACK_VIA_TYPE_VECTOR) {
            struct kvm_xen_hvm_attr xhuv;
            xhuv.type = KVM_XEN_ATTR_TYPE_UPCALL_VECTOR;
            xhuv.u.vector = (uint8_t)hp->value;
            err = kvm_vm_ioctl(cs->kvm_state, KVM_XEN_HVM_SET_ATTR, &xhuv);
        }
        break;
    default:
        err = -ENOSYS;
        goto out;
    }


out:
    exit->u.hcall.result = err;
    return err ? HCALL_ERR : 0;
}

static int kvm_xen_hcall_evtchn_upcall_vector(struct kvm_xen_exit *exit,
                                              X86CPU *cpu, uint64_t arg)
{
    struct xen_hvm_evtchn_upcall_vector *up;
    CPUState *target_cs;
    int err = 0, vector;

    up = gva_to_hva(CPU(cpu), arg);
    if (!up) {
        err = -EFAULT;
        goto out;
    }

    vector = up->vector;
    if (vector < 0x10) {
        err = -EINVAL;
        goto out;
    }

    target_cs = qemu_get_cpu(up->vcpu);
    if (!target_cs) {
        err = -EINVAL;
        goto out;
    }
    if (X86_CPU(target_cs) == cpu) {
        struct kvm_xen_vcpu_attr xvuv;

        xvuv.type = KVM_XEN_VCPU_ATTR_TYPE_UPCALL_VECTOR;
        xvuv.u.vector = vector;
        err = kvm_vcpu_ioctl(target_cs, KVM_XEN_HVM_SET_ATTR, &xvuv);
        if (err < 0) {
            goto out;
        }
        trace_kvm_xen_set_vcpu_callback(target_cs->cpu_index, vector);
    } else {
        do_run_on_cpu(target_cs, xen_vcpu_set_callback,
                      RUN_ON_CPU_HOST_INT(vector), &xen_global_mutex);
    }

out:
    exit->u.hcall.result = err;
    return err ? HCALL_ERR : 0;
}

static int kvm_xen_hcall_hvm_op(struct kvm_xen_exit *exit, X86CPU *cpu,
                                int cmd, uint64_t arg)
{
    int ret = -ENOSYS;
    switch (cmd) {
    case HVMOP_pagetable_dying: {
            exit->u.hcall.result = -ENOSYS;
            return 0;
        }
    case HVMOP_set_param: {
            ret = handle_set_param(exit, cpu, arg);
            break;
        }
    }

    exit->u.hcall.result = ret;
    return ret ? HCALL_ERR : 0;
}

static int xen_set_vcpu_attr(CPUState *cs, uint16_t type, uint64_t gpa)
{
    struct kvm_xen_vcpu_attr xhsi;

    xhsi.type = type;
    xhsi.u.gpa = gpa;

    trace_kvm_xen_set_vcpu_attr(cs->cpu_index, type, gpa);

    return kvm_vcpu_ioctl(cs, KVM_XEN_VCPU_SET_ATTR, &xhsi);
}

static int vcpuop_register_vcpu_info(CPUState *cs, CPUState *target,
                                     uint64_t arg)
{
    XenCPUState *xt = &X86_CPU(target)->env.xen_vcpu;
    struct vcpu_register_vcpu_info *rvi;
    uint64_t gpa;
    void *hva;

    rvi = gva_to_hva(cs, arg);
    if (!rvi) {
        return -EFAULT;
    }

    gpa = ((rvi->mfn << PAGE_SHIFT) + rvi->offset);
    hva = gpa_to_hva(gpa);
    if (!hva) {
        return -EFAULT;
    }

    xt->info = hva;
    return xen_set_vcpu_attr(target, KVM_XEN_VCPU_ATTR_TYPE_VCPU_INFO, gpa);
}

static int vcpuop_register_vcpu_time_info(CPUState *cs, CPUState *target,
                                          uint64_t arg)
{
    struct vcpu_register_time_memory_area *tma;
    uint64_t gpa;
    void *hva;

    tma = gva_to_hva(cs, arg);
    if (!tma) {
        return -EFAULT;
    }

    hva = gva_to_hva(cs, tma->addr.p);
    if (!hva || !tma->addr.p) {
        return -EFAULT;
    }

    gpa = gva_to_gpa(cs, tma->addr.p);
    return xen_set_vcpu_attr(target, KVM_XEN_VCPU_ATTR_TYPE_VCPU_TIME_INFO, gpa);
}

static int vcpuop_register_runstate_info(CPUState *cs, CPUState *target,
                                         uint64_t arg)
{
    struct vcpu_register_runstate_memory_area *rma;
    uint64_t gpa;
    void *hva;

    rma = gva_to_hva(cs, arg);
    if (!rma) {
        return -EFAULT;
    }

    hva = gva_to_hva(cs, rma->addr.p);
    if (!hva || !rma->addr.p) {
        return -EFAULT;
    }

    gpa = gva_to_gpa(cs, rma->addr.p);
    return xen_set_vcpu_attr(target, KVM_XEN_VCPU_ATTR_TYPE_RUNSTATE_ADDR, gpa);
}

static int kvm_xen_hcall_vcpu_op(struct kvm_xen_exit *exit, X86CPU *cpu,
                                 int cmd, int vcpu_id, uint64_t arg)
{
    CPUState *dest = qemu_get_cpu(vcpu_id);
    CPUState *cs = CPU(cpu);
    int err = -ENOSYS;

    switch (cmd) {
    case VCPUOP_register_runstate_memory_area: {
            err = vcpuop_register_runstate_info(cs, dest, arg);
            break;
        }
    case VCPUOP_register_vcpu_time_memory_area: {
            err = vcpuop_register_vcpu_time_info(cs, dest, arg);
            break;
        }
    case VCPUOP_register_vcpu_info: {
            err = vcpuop_register_vcpu_info(cs, dest, arg);
            break;
        }
    }

    exit->u.hcall.result = err;
    return err ? HCALL_ERR : 0;
}

static int kvm_xen_hcall_evtchn_op_compat(struct kvm_xen_exit *exit,
                                          X86CPU *cpu, uint64_t arg)
{
    struct evtchn_op *op = gva_to_hva(CPU(cpu), arg);
    int err = -ENOSYS;

    if (!op) {
        goto err;
    }

    switch (op->cmd) {
    default:
        exit->u.hcall.result = err;
        return 0;
    }
err:
    exit->u.hcall.result = err;
    return err ? HCALL_ERR : 0;
}

static int kvm_xen_hcall_evtchn_op(struct kvm_xen_exit *exit,
                                   int cmd, uint64_t arg)
{
    int err = -ENOSYS;

    switch (cmd) {
    case EVTCHNOP_init_control:
        /* FIFO ABI */
    default:
        exit->u.hcall.result = err;
        return 0;
    }

    exit->u.hcall.result = err;
    return err ? HCALL_ERR : 0;
}

static int __kvm_xen_handle_exit(X86CPU *cpu, struct kvm_xen_exit *exit)
{
    uint16_t code = exit->u.hcall.input;

    if (exit->u.hcall.cpl > 0) {
            exit->u.hcall.result = -EPERM;
            return HCALL_ERR;
    }

    switch (code) {
    case HVMOP_set_evtchn_upcall_vector:
        return kvm_xen_hcall_evtchn_upcall_vector(exit, cpu,
                                                  exit->u.hcall.params[0]);
    case __HYPERVISOR_event_channel_op_compat:
        return kvm_xen_hcall_evtchn_op_compat(exit, cpu,
                                              exit->u.hcall.params[0]);
    case __HYPERVISOR_event_channel_op:
        return kvm_xen_hcall_evtchn_op(exit, exit->u.hcall.params[0],
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
        return kvm_xen_hcall_memory_op(exit, exit->u.hcall.params[0],
                                       exit->u.hcall.params[1], cpu);
    case __HYPERVISOR_xen_version:
        return kvm_xen_hcall_xen_version(exit, cpu, exit->u.hcall.params[0],
                                         exit->u.hcall.params[1]);
    default:
        exit->u.hcall.result = -ENOSYS;
        return HCALL_ERR;
    }
}

int kvm_xen_handle_exit(X86CPU *cpu, struct kvm_xen_exit *exit)
{
    int ret = HCALL_ERR;

    switch (exit->type) {
    case KVM_EXIT_XEN_HCALL: {
        ret = __kvm_xen_handle_exit(cpu, exit);
        trace_kvm_xen_hypercall(CPU(cpu)->cpu_index, exit->u.hcall.cpl,
                           exit->u.hcall.input, exit->u.hcall.params[0],
                           exit->u.hcall.params[1], exit->u.hcall.params[2],
                           exit->u.hcall.result);
        return ret;
    }
    default:
        return ret;
    }
}
