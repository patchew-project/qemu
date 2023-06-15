/*
 * Virtio cpu stall watchdog Support
 *
 * Copyright Kylin, Inc. 2023
 * Copyright zhanghao1 <zhanghao1@kylinos.cn>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#ifndef QEMU_VIRTIO_CPU_STALL_WATCHDOG_H
#define QEMU_VIRTIO_CPU_STALL_WATCHDOG_H

#include "hw/virtio/virtio.h"

#define DEBUG_VIRTIO_CPU_STALL_WATCHDOG 0

#define DPRINTF(fmt, ...) \
do { \
    if (DEBUG_VIRTIO_CPU_STALL_WATCHDOG) { \
        fprintf(stderr, "virtio_cpu_stall_watchdog: " fmt, ##__VA_ARGS__); \
    } \
} while (0)

#define TYPE_VIRTIO_CPU_STALL_WATCHDOG "virtio-cpu-stall-watchdog-device"
#define VIRTIO_VCPU_STALL_WATCHDOG(obj) \
        OBJECT_CHECK(VirtIOCPUSTALLWATCHDOG, (obj), TYPE_VIRTIO_CPU_STALL_WATCHDOG)
#define VIRTIO_CPU_STALL_WATCHDOG_GET_PARENT_CLASS(obj) \
        OBJECT_GET_PARENT_CLASS(obj, TYPE_VIRTIO_CPU_STALL_WATCHDOG)
typedef struct VirtIOCPUSTALLWATCHDOG {
    VirtIODevice parent_obj;

    /* Only one vq - guest puts message on it when vcpu is stall */
    VirtQueue *vq;

    QEMUTimer **timer;
    int num_timers;

    struct vcpu_stall_info **recv_buf;

    uint64_t not_running_last_timestamp;
} VirtIOCPUSTALLWATCHDOG;

#endif
