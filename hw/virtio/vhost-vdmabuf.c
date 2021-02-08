/*
 * Implementation of Virtio based Dmabuf device -- mostly inspired by
 * vfio/display.c and vhost-vsock.c.
 *
 * Copyright 2021 Intel Corporation.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <sys/ioctl.h>
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/typedefs.h"
#include "monitor/monitor.h"
#include "virtio-pci.h"
#include "qemu/module.h"
#include "qemu/uuid.h"
#include "sysemu/sysemu.h"
#include "ui/console.h"
#include "qapi/error.h"
#include "trace.h"

#include "standard-headers/linux/virtio_ids.h"
#include "standard-headers/drm/drm_fourcc.h"
#include "hw/qdev-properties.h"

#include "hw/virtio/virtio.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/virtio-access.h"
#include "hw/virtio/vhost-vdmabuf.h"

#define TYPE_VHOST_VDMABUF "vhost-vdmabuf"
#define VHOST_VDMABUF(obj) \
        OBJECT_CHECK(VHostVdmabuf, (obj), TYPE_VHOST_VDMABUF)

#define TYPE_VHOST_VDMABUF_PCI "vhost-vdmabuf-pci-base"
#define VHOST_VDMABUF_PCI(obj) \
        OBJECT_CHECK(VHostVdmabufPCI, (obj), TYPE_VHOST_VDMABUF_PCI)

#define VHOST_VDMABUF_QUEUE_SIZE 128
#define QEMU_UUID_SIZE_BYTES 16

static bool have_event = false;

typedef struct VHostVdmabufPCI VHostVdmabufPCI;

typedef struct VDMABUFDMABuf {
    QemuDmaBuf buf;
    QemuUUID dmabuf_id;
    QTAILQ_ENTRY(VDMABUFDMABuf) next;
} VDMABUFDMABuf;

typedef struct VDMABUFDisplay {
    QemuConsole *con;
    DisplaySurface *surface;
    struct {
        QTAILQ_HEAD(, VDMABUFDMABuf) bufs;
        VDMABUFDMABuf *guest_fb;
    } dmabuf;
} VDMABUFDisplay;

typedef struct {
    VirtIODevice parent;

    struct vhost_dev vhost_dev;
    struct vhost_virtqueue vhost_vqs[2];

    VirtQueue *send_vq;
    VirtQueue *recv_vq;
    VDMABUFDisplay *dpy;
    int vhostfd;
} VHostVdmabuf;

struct VHostVdmabufPCI {
    VirtIOPCIProxy parent_obj;
    VHostVdmabuf vdev;
};

typedef struct VDMABUFBlob {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    uint64_t modifier;
} VDMABUFBlob;

static int vhost_vdmabuf_start(VirtIODevice *vdev)
{
    VHostVdmabuf *vdmabuf = VHOST_VDMABUF(vdev);
    BusState *bs = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *bc = VIRTIO_BUS_GET_CLASS(bs);
    int ret, i;

    if (!bc->set_guest_notifiers) {
        error_report("No support for guest notifiers");
        return -ENOSYS;
    }

    ret = vhost_dev_enable_notifiers(&vdmabuf->vhost_dev, vdev);
    if (ret < 0) {
        error_report("Cannot enable host notifiers: %d", -ret);
        return ret;
    }

    ret = bc->set_guest_notifiers(bs->parent, vdmabuf->vhost_dev.nvqs, true);
    if (ret < 0) {
        error_report("Cannot set guest notifier: %d", -ret);
        return ret;
    }

    vdmabuf->vhost_dev.acked_features = vdev->guest_features;
    ret = vhost_dev_start(&vdmabuf->vhost_dev, vdev);
    if (ret < 0) {
        error_report("Cannot start vhost: %d", -ret);
        return ret;
    }

    for (i = 0; i < vdmabuf->vhost_dev.nvqs; i++) {
        vhost_virtqueue_mask(&vdmabuf->vhost_dev, vdev, i, false);
    }

    return 0;
}

static void vhost_vdmabuf_stop(VirtIODevice *vdev)
{
    VHostVdmabuf *vdmabuf = VHOST_VDMABUF(vdev);
    BusState *bs = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusClass *bc = VIRTIO_BUS_GET_CLASS(bs);

    if (!bc->set_guest_notifiers) {
        return;
    }

    vhost_dev_stop(&vdmabuf->vhost_dev, vdev);
    vhost_dev_disable_notifiers(&vdmabuf->vhost_dev, vdev);
}

static int vhost_vdmabuf_set_running(VirtIODevice *vdev, int start)
{
    VHostVdmabuf *vdmabuf = VHOST_VDMABUF(vdev);
    const VhostOps *vhost_ops = vdmabuf->vhost_dev.vhost_ops;
    int ret;

    if (!vhost_ops->vhost_vdmabuf_set_running) {
        return -ENOSYS;
    }

    ret = vhost_ops->vhost_vdmabuf_set_running(&vdmabuf->vhost_dev, start);
    if (ret < 0) {
        return -errno;
    }

    return 0;
}

static void vhost_vdmabuf_set_status(VirtIODevice *vdev, uint8_t status)
{
    VHostVdmabuf *vdmabuf = VHOST_VDMABUF(vdev);
    bool should_start = status & VIRTIO_CONFIG_S_DRIVER_OK;
    int ret = 0;

    if (!vdev->vm_running) {
        should_start = false;
    }

    if (vdmabuf->vhost_dev.started == should_start) {
        return;
    }

    if (should_start) {
        ret = vhost_vdmabuf_start(vdev);
        if (ret < 0) {
            error_report("Cannot start vhost vdmabuf: %d", -ret);
	    return;
	}

        ret = vhost_vdmabuf_set_running(vdev, 1);
        if (ret < 0) {
            vhost_vdmabuf_stop(vdev);
            error_report("vhost vdmabuf set running failed: %d", ret);
            return;
        }
    } else {
        ret = vhost_vdmabuf_set_running(vdev, 0);
        if (ret < 0) {
            error_report("vhost vdmabuf set running failed: %d", ret);
            return;
        }

        vhost_vdmabuf_stop(vdev);
    }
}

static int vhost_vdmabuf_pre_save(void *opaque)
{
    return 0;
}

static int vhost_vdmabuf_post_load(void *opaque, int version_id)
{
    return 0;
}

static const VMStateDescription vmstate_virtio_vhost_vdmabuf = {
    .name = "virtio-vhost_vdmabuf",
    .minimum_version_id = 0,
    .version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
    .pre_save = vhost_vdmabuf_pre_save,
    .post_load = vhost_vdmabuf_post_load,
};

static void vhost_vdmabuf_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
    return;
}

static void vhost_vdmabuf_guest_notifier_mask(VirtIODevice *vdev, int idx,
                                              bool mask)
{
    VHostVdmabuf *vdmabuf = VHOST_VDMABUF(vdev);

    vhost_virtqueue_mask(&vdmabuf->vhost_dev, vdev, idx, mask);
}

static bool vhost_vdmabuf_guest_notifier_pending(VirtIODevice *vdev,
                                                 int idx)
{
    VHostVdmabuf *vdmabuf = VHOST_VDMABUF(vdev);

    return vhost_virtqueue_pending(&vdmabuf->vhost_dev, idx);
}

static void vhost_vdmabuf_device_unrealize(DeviceState *dev)
{
    VHostVdmabuf *vdmabuf = VHOST_VDMABUF(dev);
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);

    vhost_vdmabuf_set_status(vdev, 0);
    vhost_dev_cleanup(&vdmabuf->vhost_dev);

    virtio_delete_queue(vdmabuf->send_vq);
    virtio_delete_queue(vdmabuf->recv_vq);
    virtio_cleanup(vdev);
}

static VDMABUFDMABuf *vdmabuf_display_get_dmabuf(VHostVdmabuf *vdmabuf)
{
    VDMABUFDisplay *dpy = vdmabuf->dpy;
    VDMABUFDMABuf *dmabuf;
    VDMABUFBlob *dmabuf_blob;
    QemuUUID uuid;
    struct virtio_vdmabuf_import msg;
    struct virtio_vdmabuf_e_hdr *ev_hdr;
    int fd = vdmabuf->vhostfd;
    char data[256] = {0};
    long ret = 0;

    ret = read(fd, data, sizeof *ev_hdr + sizeof *dmabuf_blob);
    if (ret <= 0) {
        error_report("Cannot read event: %ld", -ret);
        return NULL;
    }

    ev_hdr = (struct virtio_vdmabuf_e_hdr *)data;
    memcpy(&uuid, &ev_hdr->buf_id, QEMU_UUID_SIZE_BYTES);
    dmabuf_blob = (VDMABUFBlob *)(data + sizeof *ev_hdr);

    QTAILQ_FOREACH(dmabuf, &dpy->dmabuf.bufs, next) {
        if (qemu_uuid_is_equal(&uuid, &dmabuf->dmabuf_id)) {
            QTAILQ_REMOVE(&dpy->dmabuf.bufs, dmabuf, next);
            QTAILQ_INSERT_HEAD(&dpy->dmabuf.bufs, dmabuf, next);
            return dmabuf;
        }
    }

    memcpy(&msg.buf_id, &uuid, QEMU_UUID_SIZE_BYTES);
    ret = ioctl(fd, VIRTIO_VDMABUF_IOCTL_IMPORT, &msg);
    if (ret) {
        error_report("Cannot import dmabuf: %ld", -ret);
	return NULL;
    }

    dmabuf = g_new0(VDMABUFDMABuf, 1);
    memcpy(&dmabuf->dmabuf_id, &uuid, QEMU_UUID_SIZE_BYTES);
    dmabuf->buf.fd = msg.fd;

    dmabuf->buf.width = dmabuf_blob->width;
    dmabuf->buf.height = dmabuf_blob->height;
    dmabuf->buf.stride = dmabuf_blob->stride;
    dmabuf->buf.fourcc = dmabuf_blob->format;
    dmabuf->buf.modifier = dmabuf_blob->modifier;

    QTAILQ_INSERT_HEAD(&dpy->dmabuf.bufs, dmabuf, next);
    return dmabuf;
}

static void vdmabuf_display_free_one_dmabuf(VHostVdmabuf *vdmabuf,
					    VDMABUFDisplay *dpy,
                                            VDMABUFDMABuf *dmabuf)
{
    struct virtio_vdmabuf_import msg;
    int fd = vdmabuf->vhostfd;

    QTAILQ_REMOVE(&dpy->dmabuf.bufs, dmabuf, next);
    dpy_gl_release_dmabuf(dpy->con, &dmabuf->buf);

    memcpy(&msg.buf_id, &dmabuf->dmabuf_id, QEMU_UUID_SIZE_BYTES);
    if (ioctl(fd, VIRTIO_VDMABUF_IOCTL_RELEASE, &msg))
        error_report("Error releasing dmabuf");

    close(dmabuf->buf.fd);
    g_free(dmabuf);
}

static void vdmabuf_display_free_dmabufs(VHostVdmabuf *vdmabuf)
{
    VDMABUFDisplay *dpy = vdmabuf->dpy;
    VDMABUFDMABuf *dmabuf, *tmp;
    uint32_t keep = 2;

    QTAILQ_FOREACH_SAFE(dmabuf, &dpy->dmabuf.bufs, next, tmp) {
        if (keep > 0) {
            keep--;
            continue;
        }

        assert(dmabuf != dpy->dmabuf.guest_fb);
        vdmabuf_display_free_one_dmabuf(vdmabuf, dpy, dmabuf);
    }
}

static void vdmabuf_display_dmabuf_update(void *opaque)
{
    VHostVdmabuf *vdmabuf = opaque;
    VDMABUFDisplay *dpy = vdmabuf->dpy;
    VDMABUFDMABuf *guest_fb;
    bool free_bufs = false;

    if (!have_event)
        return;

    guest_fb = vdmabuf_display_get_dmabuf(vdmabuf);
    if (guest_fb == NULL) {
        return;
    }

    if (dpy->dmabuf.guest_fb != guest_fb) {
        dpy->dmabuf.guest_fb = guest_fb;
        qemu_console_resize(dpy->con,
                            guest_fb->buf.width, guest_fb->buf.height);
        dpy_gl_scanout_dmabuf(dpy->con, &guest_fb->buf);
        free_bufs = true;
    }

    dpy_gl_update(dpy->con, 0, 0, guest_fb->buf.width, guest_fb->buf.height);

    if (free_bufs) {
        vdmabuf_display_free_dmabufs(vdmabuf);
    }

    have_event = false;
}

static void vdmabuf_event_handler(void *opaque)
{
    VHostVdmabuf *vdmabuf = opaque;
    VDMABUFDisplay *dpy = vdmabuf->dpy;

    have_event = true;
    graphic_hw_dpy_refresh(dpy->con);
}

static const GraphicHwOps vdmabuf_display_dmabuf_ops = {
    .gfx_update = vdmabuf_display_dmabuf_update,
};

static int vdmabuf_display_dmabuf_init(VHostVdmabuf *vdmabuf, Error **errp)
{
    if (!display_opengl) {
        error_setg(errp, "vhost-vdmabuf: opengl not available");
        return -1;
    }

    vdmabuf->dpy = g_new0(VDMABUFDisplay, 1);
    vdmabuf->dpy->con = graphic_console_init(NULL, 0,
                                             &vdmabuf_display_dmabuf_ops,
                                             vdmabuf);
    return 0;
}


static void vhost_vdmabuf_device_realize(DeviceState *dev, Error **errp)
{
    VHostVdmabuf *vdmabuf = VHOST_VDMABUF(dev);
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    int vhostfd;
    int ret;

    vhostfd = open("/dev/vhost-vdmabuf", O_RDWR);
    if (vhostfd < 0) {
        error_setg_errno(errp, errno,
                         "vhost-vdmabuf: failed to open vhost device");
        return;
    }

    virtio_init(vdev, "vhost-vdmabuf", VIRTIO_ID_VDMABUF, 0);
    vdmabuf->send_vq = virtio_add_queue(vdev, VHOST_VDMABUF_QUEUE_SIZE,
                                        vhost_vdmabuf_handle_output);
    vdmabuf->recv_vq = virtio_add_queue(vdev, VHOST_VDMABUF_QUEUE_SIZE,
                                        vhost_vdmabuf_handle_output);

    vdmabuf->vhost_dev.nvqs = ARRAY_SIZE(vdmabuf->vhost_vqs);
    vdmabuf->vhost_dev.vqs = vdmabuf->vhost_vqs;
    
    ret = vhost_dev_init(&vdmabuf->vhost_dev, (void *)(uintptr_t)vhostfd,
                         VHOST_BACKEND_TYPE_KERNEL, 0);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "vhost-vdmabuf: vhost_dev_init failed");
        goto err_virtio;
    }

    vdmabuf->vhostfd = vhostfd;
    qemu_set_fd_handler(vhostfd, vdmabuf_event_handler, NULL, vdmabuf);

    ret = vdmabuf_display_dmabuf_init(vdmabuf, errp);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "vhost-vdmabuf: dmabuf_init failed");
        goto err_virtio;
    }

    return;

err_virtio:
    vhost_vdmabuf_device_unrealize(dev);
    if (vhostfd >= 0) {
        close(vhostfd);
    }
}

static uint64_t vhost_vdmabuf_get_features(VirtIODevice *vdev,
                                           uint64_t req_features,
                                           Error **errp)
{
    return req_features;
}

static void vhost_vdmabuf_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_virtio_vhost_vdmabuf;
    vdc->realize = vhost_vdmabuf_device_realize;
    vdc->unrealize = vhost_vdmabuf_device_unrealize;
    vdc->get_features = vhost_vdmabuf_get_features;
    vdc->set_status = vhost_vdmabuf_set_status;

    vdc->guest_notifier_mask = vhost_vdmabuf_guest_notifier_mask;
    vdc->guest_notifier_pending = vhost_vdmabuf_guest_notifier_pending;
}

static const TypeInfo vhost_vdmabuf_info = {
    .name = TYPE_VHOST_VDMABUF,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VHostVdmabuf),
    .class_init = vhost_vdmabuf_class_init,
};

static void vhost_vdmabuf_register_types(void)
{
    type_register_static(&vhost_vdmabuf_info);
}

static void vhost_vdmabuf_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VHostVdmabufPCI *dev = VHOST_VDMABUF_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    qdev_realize(vdev, BUS(&vpci_dev->bus), errp);
}

static void vhost_vdmabuf_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *pc = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    pc->realize = vhost_vdmabuf_pci_realize;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);

    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_VDMABUF;
    pcidev_k->revision = 0x00;
    pcidev_k->class_id = PCI_CLASS_COMMUNICATION_OTHER;
}

static void vhost_vdmabuf_pci_instance_init(Object *obj)
{
    VHostVdmabufPCI *dev = VHOST_VDMABUF_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VHOST_VDMABUF);
}

static const VirtioPCIDeviceTypeInfo vhost_vdmabuf_pci_info = {
    .base_name     = TYPE_VHOST_VDMABUF_PCI,
    .generic_name  = "vhost-vdmabuf-pci",
    .instance_size = sizeof(VHostVdmabufPCI),
    .instance_init = vhost_vdmabuf_pci_instance_init,
    .class_init    = vhost_vdmabuf_pci_class_init,
};

static void virtio_pci_vhost_register(void)
{
    virtio_pci_types_register(&vhost_vdmabuf_pci_info);
}

type_init(virtio_pci_vhost_register)
type_init(vhost_vdmabuf_register_types)
