/*
 * QEMU Gunyah hypervisor support
 *
 * Copyright(c) 2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/* header to be included in Gunyah-specific code */

#ifndef GUNYAH_INT_H
#define GUNYAH_INT_H

#include "qemu/accel.h"
#include "qemu/typedefs.h"

struct GUNYAHState {
    AccelState parent_obj;

    int fd;
    int vmfd;
};

int gunyah_create_vm(void);
void *gunyah_cpu_thread_fn(void *arg);

#endif    /* GUNYAH_INT_H */
