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

#include "standard-headers/xen/version.h"
#include "standard-headers/xen/memory.h"
#include "standard-headers/xen/hvm/hvm_op.h"
#include "standard-headers/xen/vcpu.h"

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

static void *gpa_to_hva(uint64_t gpa)
{
    MemoryRegionSection mrs;

    mrs = memory_region_find(get_system_memory(), gpa, 1);
    return !mrs.mr ? NULL : qemu_map_ram_ptr(mrs.mr->ram_block,
                                             mrs.offset_within_region);
}

static void *gva_to_hva(CPUState *cs, uint64_t gva)
{
    struct kvm_translation t = { .linear_address = gva };
    int err;

    err = kvm_vcpu_ioctl(cs, KVM_TRANSLATE, &t);
    if (err || !t.valid) {
        return NULL;
    }

    return gpa_to_hva(t.physical_address);
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
            fi->submap = (1U << XENFEAT_auto_translated_physmap);
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
    int err;

    xhsi.type = KVM_XEN_ATTR_TYPE_SHARED_INFO;
    xhsi.u.shared_info.gfn = gfn;
    err = kvm_vm_ioctl(s, KVM_XEN_HVM_SET_ATTR, &xhsi);
    trace_kvm_xen_set_shared_info(gfn);
    xen->shared_info = shi;
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

static int kvm_xen_hcall_hvm_op(struct kvm_xen_exit *exit,
                                int cmd, uint64_t arg)
{
    switch (cmd) {
    case HVMOP_pagetable_dying: {
            exit->u.hcall.result = -ENOSYS;
            return 0;
        }
    }

    exit->u.hcall.result = -ENOSYS;
    return HCALL_ERR;
}

static int kvm_xen_hcall_vcpu_op(struct kvm_xen_exit *exit,
                                 int cmd, uint64_t arg)
{
    switch (cmd) {
    case VCPUOP_register_vcpu_info: {
            /* no vcpu info placement for now */
            exit->u.hcall.result = -ENOSYS;
            return 0;
        }
    }

    exit->u.hcall.result = -ENOSYS;
    return HCALL_ERR;
}

static int __kvm_xen_handle_exit(X86CPU *cpu, struct kvm_xen_exit *exit)
{
    uint16_t code = exit->u.hcall.input;

    if (exit->u.hcall.cpl > 0) {
            exit->u.hcall.result = -EPERM;
            return HCALL_ERR;
    }

    switch (code) {
    case __HYPERVISOR_vcpu_op:
        return kvm_xen_hcall_vcpu_op(exit, exit->u.hcall.params[0],
                                     exit->u.hcall.params[1]);
    case __HYPERVISOR_hvm_op:
        return kvm_xen_hcall_hvm_op(exit, exit->u.hcall.params[0],
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
