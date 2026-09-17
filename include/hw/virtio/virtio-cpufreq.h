/*
 * Virtio CPU frequency backend (Xen)
 *
 * Copyright (c) 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_VIRTIO_CPUFREQ_H
#define QEMU_VIRTIO_CPUFREQ_H

#include "hw/virtio/virtio.h"
#include "qom/object.h"

#define TYPE_VIRTIO_CPUFREQ "virtio-cpufreq-device"
OBJECT_DECLARE_SIMPLE_TYPE(VirtIOCPUFreq, VIRTIO_CPUFREQ)

struct VirtIOCPUFreq {
    VirtIODevice parent_obj;
    VirtQueue *vq;
};

#endif /* QEMU_VIRTIO_CPUFREQ_H */
