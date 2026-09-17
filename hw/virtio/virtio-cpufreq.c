/*
 * Virtio CPU frequency backend
 *
 * Reports the host pCPU frequency that currently runs a Xen vCPU to a
 * guest virtio-cpufreq frontend.
 *
 * Copyright (c) 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/iov.h"
#include "qemu/module.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-cpufreq.h"
#include "hw/xen/xen_native.h"
#include "migration/vmstate.h"
#include "standard-headers/linux/virtio_ids.h"

struct freq_request {
    uint32_t cpu_id;      /* vCPU ID to query */
    uint32_t freq_khz;    /* Returned frequency in kHz */
} QEMU_PACKED;

static bool pinning_checked;
static bool pinning_ok;

/*
 * Each guest vCPU must be pinned to a unique pCPU so the reported
 * frequency is meaningful.
 */
static bool check_cpu_pinned(void)
{
    uint32_t domid = xen_domid;
    xc_cpumap_t cpumap_hard;
    xc_domaininfo_t info;
    int vcpu, pcpu, vcpus_num, pcpus_num;
    bool checked_result = true;
    int *used_pcpu;

    if (!xen_xc) {
        error_report("virtio-cpufreq: Xen interface is not available");
        return false;
    }

    if (xc_domain_getinfo_single(xen_xc, domid, &info) < 0) {
        error_report("virtio-cpufreq: xc_domain_getinfo_single failed");
        return false;
    }

    vcpus_num = info.max_vcpu_id + 1;
    pcpus_num = xc_get_max_cpus(xen_xc);
    used_pcpu = g_new(int, pcpus_num);

    cpumap_hard = xc_cpumap_alloc(xen_xc);
    if (cpumap_hard == NULL) {
        g_free(used_pcpu);
        return false;
    }

    for (pcpu = 0; pcpu < pcpus_num; pcpu++) {
        used_pcpu[pcpu] = -1;
    }

    for (vcpu = 0; vcpu < vcpus_num; vcpu++) {
        int count = 0, pinned_pcpu = -1;

        memset(cpumap_hard, 0, xc_get_cpumap_size(xen_xc));
        if (xc_vcpu_getaffinity(xen_xc, domid, vcpu, cpumap_hard,
                                NULL, XEN_VCPUAFFINITY_HARD)) {
            error_report("virtio-cpufreq: xc_vcpu_getaffinity failed");
            checked_result = false;
            break;
        }

        for (pcpu = 0; pcpu < pcpus_num; pcpu++) {
            if (xc_cpumap_testcpu(pcpu, cpumap_hard)) {
                count++;
                pinned_pcpu = pcpu;
            }
        }

        if (count != 1) {
            error_report("virtio-cpufreq: vCPU%d is pinned to %d pCPUs "
                         "(expected 1)", vcpu, count);
            checked_result = false;
            break;
        }

        if (used_pcpu[pinned_pcpu] != -1) {
            error_report("virtio-cpufreq: pCPU%d already used by another vCPU",
                         pinned_pcpu);
            checked_result = false;
            break;
        }

        used_pcpu[pinned_pcpu] = vcpu;
    }

    free(cpumap_hard);
    g_free(used_pcpu);
    return checked_result;
}

static uint32_t get_host_cpu_freq(uint32_t vcpu_id)
{
    uint32_t pcpu_id;
    uint32_t domid = xen_domid;
    int cpufreq;
    xc_vcpuinfo_t vcpu_info;

    if (!xen_xc) {
        return 0;
    }

    if (xc_vcpu_getinfo(xen_xc, domid, vcpu_id, &vcpu_info)) {
        error_report("virtio-cpufreq: xc_vcpu_getinfo failed");
        return 0;
    }
    pcpu_id = vcpu_info.cpu;

    if (xc_get_cpufreq_avgfreq(xen_xc, pcpu_id, &cpufreq)) {
        error_report("virtio-cpufreq: get freq failed");
        return 0;
    }

    return cpufreq;
}

static void virtio_cpufreq_handle_request(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtQueueElement *elem;
    struct freq_request req;
    size_t len;

    elem = virtqueue_pop(vq, sizeof(VirtQueueElement));
    if (!elem) {
        return;
    }

    /*
     * out_sg: request (cpu_id)
     * in_sg: response (freq_khz)
     */
    if (elem->out_num == 0 || elem->in_num == 0) {
        error_report("virtio-cpufreq: invalid buffer configuration");
        virtqueue_push(vq, elem, 0);
        virtio_notify(vdev, vq);
        g_free(elem);
        return;
    }

    len = iov_to_buf(elem->out_sg, elem->out_num, 0, &req, sizeof(req));
    if (len < sizeof(req)) {
        error_report("virtio-cpufreq: invalid request size %zu (expected %zu)",
                     len, sizeof(req));
        virtqueue_push(vq, elem, 0);
        virtio_notify(vdev, vq);
        g_free(elem);
        return;
    }

    if (!pinning_checked) {
        pinning_ok = check_cpu_pinned();
        pinning_checked = true;
    }

    if (!pinning_ok) {
        req.freq_khz = 0;
    } else {
        req.freq_khz = get_host_cpu_freq(req.cpu_id);
    }

    len = iov_from_buf(elem->in_sg, elem->in_num, 0, &req, sizeof(req));

    virtqueue_push(vq, elem, len);
    virtio_notify(vdev, vq);
    g_free(elem);
}

static uint64_t virtio_cpufreq_get_features(VirtIODevice *vdev,
                                            uint64_t features,
                                            Error **errp)
{
    return features;
}

static void virtio_cpufreq_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOCPUFreq *vcpufreq = VIRTIO_CPUFREQ(dev);

    virtio_init(vdev, VIRTIO_ID_CPUFREQ, 0);
    vcpufreq->vq = virtio_add_queue(vdev, 128, virtio_cpufreq_handle_request);
}

static void virtio_cpufreq_device_unrealize(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);

    virtio_del_queue(vdev, 0);
    virtio_cleanup(vdev);
}

static const VMStateDescription vmstate_virtio_cpufreq = {
    .name = "virtio-cpufreq",
    .minimum_version_id = 1,
    .version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
};

static void virtio_cpufreq_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_virtio_cpufreq;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    vdc->realize = virtio_cpufreq_device_realize;
    vdc->unrealize = virtio_cpufreq_device_unrealize;
    vdc->get_features = virtio_cpufreq_get_features;
}

static const TypeInfo virtio_cpufreq_info = {
    .name = TYPE_VIRTIO_CPUFREQ,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VirtIOCPUFreq),
    .class_init = virtio_cpufreq_class_init,
};

static void virtio_register_types(void)
{
    type_register_static(&virtio_cpufreq_info);
}

type_init(virtio_register_types)
