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
#include "qemu/log.h"
#include "sysemu/kvm_int.h"
#include "kvm/kvm_i386.h"
#include "exec/address-spaces.h"
#include "xen-emu.h"
#include "xen.h"
#include "trace.h"
#include "hw/i386/kvm/xen_overlay.h"
#include "standard-headers/xen/version.h"

static int kvm_gva_rw(CPUState *cs, uint64_t gva, void *_buf, size_t sz,
                      bool is_write)
{
    uint8_t *buf = (uint8_t *)_buf;
    int ret;

    while (sz) {
        struct kvm_translation tr = {
            .linear_address = gva,
        };

        size_t len = TARGET_PAGE_SIZE - (tr.linear_address & ~TARGET_PAGE_MASK);
        if (len > sz)
            len = sz;

        ret = kvm_vcpu_ioctl(cs, KVM_TRANSLATE, &tr);
        if (ret || !tr.valid || (is_write && !tr.writeable)) {
            return -EFAULT;
        }

        cpu_physical_memory_rw(tr.physical_address, buf, len, is_write);

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
                         1 << XENFEAT_supervisor_mode_kernel;
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

static bool do_kvm_xen_handle_exit(X86CPU *cpu, struct kvm_xen_exit *exit)
{
    uint16_t code = exit->u.hcall.input;

    if (exit->u.hcall.cpl > 0) {
        exit->u.hcall.result = -EPERM;
        return true;
    }

    switch (code) {
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
