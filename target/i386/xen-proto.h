/*
 * Definitions for Xen guest/hypervisor interaction - x86-specific part
 *
 * Copyright (c) 2019 Oracle and/or its affiliates. All rights reserved.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef TARGET_I386_XEN_PROTO_H
#define TARGET_I386_XEN_PROTO_H

typedef struct XenState {
    struct shared_info *shared_info;
} XenState;

#endif

