/*
 * A virtio device implementing a vcpu stall watchdog.
 *
 * Copyright 2023 Kylin, Inc.
 * Copyright 2023 zhanghao1 <zhanghao1@kylinos.cn>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/iov.h"
#include "qemu/module.h"
#include "qemu/bswap.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-vcpu-stall-watchdog.h"
#include "qom/object_interfaces.h"
#include "trace.h"
#include "standard-headers/linux/virtio_ids.h"
#include "hw/virtio/virtio-access.h"
#include "hw/boards.h"
#include "sysemu/cpus.h"
#include "sysemu/runstate.h"

#define MAX_PATH 1024

#define VCPU_STALL_DEFAULT_CLOCK_HZ (5)
#define VCPU_STALL_DEFAULT_TIMEOUT_SEC (8)
#define MSEC_PER_SEC 1000
#define PROCSTAT_UTIME_INDX 13
#define PROCSTAT_GUEST_TIME_INDX 42

struct vcpu_stall_info {
    uint32_t cpu_id;
    bool is_initialized;
    uint32_t ticks;
    uint64_t not_running_last_timestamp;
};

static VirtIOCPUSTALLWATCHDOG *vwdt;

static bool is_guest_ready(VirtIOCPUSTALLWATCHDOG *vwdt)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(vwdt);
    if (virtio_queue_ready(vwdt->vq)
        && (vdev->status & VIRTIO_CONFIG_S_FEATURES_OK)) {
        return true;
    }
    return false;
}

/* receive data from guest */
static void receive_vcpu_info(void *opaque, void *buf, size_t size)
{
    VirtIOCPUSTALLWATCHDOG *vwdt = opaque;
    VirtIODevice *vdev = VIRTIO_DEVICE(vwdt);
    VirtQueueElement *elem;
    size_t len;

    if (!is_guest_ready(vwdt)) {
        return;
    }

    elem = virtqueue_pop(vwdt->vq, sizeof(VirtQueueElement));
    if (!elem) {
        return;
    }

    len = iov_size(elem->out_sg, elem->out_num);

    len = iov_to_buf(elem->out_sg, elem->out_num,
                     0, buf, len);

    int cpu = virtio_ldl_p(vdev, &((struct vcpu_stall_info *)buf)->cpu_id);
    DPRINTF("read to buf:%lu cpu_id:%u is_initialized:%d ticks:%u\n", len, cpu,
                     ((struct vcpu_stall_info *)buf)->is_initialized,
                     ((struct vcpu_stall_info *)buf)->ticks);

    virtqueue_push(vwdt->vq, elem, len);
    g_free(elem);
    virtio_notify(vdev, vwdt->vq);
}

static void vcpu_stall_check(void *opaque)
{
    int *cpu_id = (int *)opaque;

    struct vcpu_stall_info *priv = vwdt->recv_buf[*cpu_id];

    DPRINTF("start to vcpu stall check, cpu:%d ticks:%u\n",
                *cpu_id, priv->ticks);
    priv->ticks -= 1;

    if (priv->ticks <= 0) {
        /* cpu is stall, reset vm */
        qemu_log("CPU:%d is stall, need to reset vm\n", *cpu_id);
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
    }

    int64_t expire_timer = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
    expire_timer += (MSEC_PER_SEC / VCPU_STALL_DEFAULT_CLOCK_HZ);
    timer_mod(vwdt->timer[*cpu_id], expire_timer);
}

static void virtio_vcpu_stall_watchdog_process(VirtIOCPUSTALLWATCHDOG *vwdt)
{
    int i = 0;
    struct vcpu_stall_info recv_buf;

    if (!is_guest_ready(vwdt)) {
        qemu_log("guest is not ready\n");
        return;
    }

    receive_vcpu_info(vwdt, &recv_buf, sizeof(recv_buf));

    for (i = 0; i < vwdt->num_timers; i++) {
        if (vwdt->recv_buf[i]) {
            if (vwdt->recv_buf[i]->cpu_id == recv_buf.cpu_id) {
                /* update ticks */
                vwdt->recv_buf[i]->is_initialized = true;
                vwdt->recv_buf[i]->ticks = recv_buf.ticks;
            }
        } else {
            break;
        }
    }

    if (i != vwdt->num_timers) {
        struct vcpu_stall_info *priv = malloc(sizeof(struct vcpu_stall_info));
        if (!priv) {
            qemu_log("failed to alloc vcpu_stall_info\n");
            return;
        }
        memcpy(priv, &recv_buf, sizeof(struct vcpu_stall_info));
        vwdt->recv_buf[i] = priv;
        vwdt->timer[i] = timer_new_ms(QEMU_CLOCK_VIRTUAL,
                                            vcpu_stall_check, &priv->cpu_id);

        int64_t expire_timer = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
        expire_timer += (MSEC_PER_SEC / VCPU_STALL_DEFAULT_CLOCK_HZ);
        timer_mod(vwdt->timer[i], expire_timer);

        CPUState *cpu = qemu_get_cpu(recv_buf.cpu_id);
        if (!cpu) {
            DPRINTF("failed to get cpu:%d\n", recv_buf.cpu_id);
        }
        DPRINTF("vcpu thread id:%d\n", cpu->thread_id);
    }
}

static void handle_input(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOCPUSTALLWATCHDOG *vwdt = VIRTIO_VCPU_STALL_WATCHDOG(vdev);
    virtio_vcpu_stall_watchdog_process(vwdt);
}

static uint64_t get_features(VirtIODevice *vdev, uint64_t f, Error **errp)
{
    return f;
}

static void virtio_vcpu_stall_watchdog_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    vwdt = VIRTIO_VCPU_STALL_WATCHDOG(dev);

    virtio_init(vdev, VIRTIO_ID_WATCHDOG, 0);

    vwdt->vq = virtio_add_queue(vdev, 1024, handle_input);

    MachineState *ms = MACHINE(qdev_get_machine());
    unsigned int smp_cpus = ms->smp.cpus;

    vwdt->timer = malloc(sizeof(struct QEMUTimer *) * smp_cpus);
    if (!vwdt->timer) {
        qemu_log("failed to alloc timer\n");
        return;
    }

    vwdt->recv_buf = malloc(sizeof(struct vcpu_stall_info *) * smp_cpus);
    if (!vwdt->recv_buf) {
        qemu_log("failed to alloc recv_buf\n");
        return;
    }

    vwdt->num_timers = smp_cpus;
}

static void virtio_vcpu_stall_watchdog_device_unrealize(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOCPUSTALLWATCHDOG  *vwdt = VIRTIO_VCPU_STALL_WATCHDOG(dev);

    g_free(vwdt->timer);
    g_free(vwdt->recv_buf);
    virtio_cleanup(vdev);
}

static const VMStateDescription vmstate_virtio_vcpu_stall_watchdog = {
    .name = "virtio-vcpu-stall-watchdog",
    .minimum_version_id = 1,
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
};

static Property virtio_vcpu_stall_watchdog_properties[] = {
};

static void virtio_vcpu_stall_watchdog_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    device_class_set_props(dc, virtio_vcpu_stall_watchdog_properties);
    dc->vmsd = &vmstate_virtio_vcpu_stall_watchdog;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    vdc->realize = virtio_vcpu_stall_watchdog_device_realize;
    vdc->unrealize = virtio_vcpu_stall_watchdog_device_unrealize;
    vdc->get_features = get_features;
}

static const TypeInfo virtio_vcpu_stall_watchdog_info = {
    .name = TYPE_VIRTIO_CPU_STALL_WATCHDOG,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VirtIOCPUSTALLWATCHDOG),
    .class_init = virtio_vcpu_stall_watchdog_class_init,
};

static void virtio_register_types(void)
{
    type_register_static(&virtio_vcpu_stall_watchdog_info);
}

type_init(virtio_register_types)
