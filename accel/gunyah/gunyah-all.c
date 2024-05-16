/*
 * QEMU Gunyah hypervisor support
 *
 * (based on KVM accelerator code structure)
 *
 * Copyright 2008 IBM Corporation
 *           2008 Red Hat, Inc.
 *
 * Copyright(c) 2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <sys/ioctl.h>
#include "qemu/osdep.h"
#include "qemu/typedefs.h"
#include "hw/core/cpu.h"
#include "sysemu/cpus.h"
#include "sysemu/gunyah.h"
#include "sysemu/gunyah_int.h"
#include "linux-headers/linux/gunyah.h"
#include "qemu/error-report.h"

static int gunyah_ioctl(int type, ...)
{
    void *arg;
    va_list ap;
    GUNYAHState *s = GUNYAH_STATE(current_accel());

    assert(s->fd);

    va_start(ap, type);
    arg = va_arg(ap, void *);
    va_end(ap);

    return ioctl(s->fd, type, arg);
}

int gunyah_create_vm(void)
{
    GUNYAHState *s;

    s = GUNYAH_STATE(current_accel());

    s->fd = qemu_open_old("/dev/gunyah", O_RDWR);
    if (s->fd == -1) {
        error_report("Could not access Gunyah kernel module at /dev/gunyah: %s",
                                strerror(errno));
        exit(1);
    }

    s->vmfd = gunyah_ioctl(GH_CREATE_VM, 0);
    if (s->vmfd < 0) {
        error_report("Could not create VM: %s", strerror(errno));
        exit(1);
    }

    return 0;
}

void *gunyah_cpu_thread_fn(void *arg)
{
    CPUState *cpu = arg;

    do {
        /* Do nothing */
    } while (!cpu->unplug || cpu_can_run(cpu));

    return NULL;
}
