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

int kvm_xen_init(KVMState *s, uint32_t xen_version);

#endif /* QEMU_I386_XEN_H */
