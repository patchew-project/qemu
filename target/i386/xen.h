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

#ifndef QEMU_I386_XEN_H
#define QEMU_I386_XEN_H

#define XEN_HYPERCALL_MSR 0x40000000

#define XEN_CPUID_SIGNATURE        0x40000000
#define XEN_CPUID_VENDOR           0x40000001
#define XEN_CPUID_HVM_MSR          0x40000002
#define XEN_CPUID_TIME             0x40000003
#define XEN_CPUID_HVM              0x40000004

#define XEN_VERSION(maj, min) ((maj) << 16 | (min))

int kvm_xen_init(KVMState *s, uint32_t xen_version);
int kvm_xen_handle_exit(X86CPU *cpu, struct kvm_xen_exit *exit);

#endif /* QEMU_I386_XEN_H */
