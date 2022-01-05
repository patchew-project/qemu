#include "qemu/osdep.h"
#include <sys/ioctl.h>
#include <linux/vhost.h>
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/cutils.h"
#include "hw/qdev-core.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-access.h"
#include "hw/virtio/vdpa-dev.h"
#include "sysemu/sysemu.h"
#include "sysemu/runstate.h"

static void
vhost_vdpa_device_dummy_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
    /* Nothing to do */
}

static int vdpa_dev_get_info_by_fd(int fd, uint64_t cmd, Error **errp)
{
    int val;

    if (ioctl(fd, cmd, &val) < 0) {
        error_setg(errp, "vhost-vdpa-device: cmd 0x%lx failed: %s",
                   cmd, strerror(errno));
        return -1;
    }

    return val;
}

static inline int vdpa_dev_get_queue_size(int fd, Error **errp)
{
    return vdpa_dev_get_info_by_fd(fd, VHOST_VDPA_GET_VRING_NUM, errp);
}

static inline int vdpa_dev_get_vqs_num(int fd, Error **errp)
{
    return vdpa_dev_get_info_by_fd(fd, VHOST_VDPA_GET_VQS_NUM, errp);
}

static inline int vdpa_dev_get_config_size(int fd, Error **errp)
{
    return vdpa_dev_get_info_by_fd(fd, VHOST_VDPA_GET_CONFIG_SIZE, errp);
}

static void vhost_vdpa_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VhostVdpaDevice *s = VHOST_VDPA_DEVICE(vdev);
    uint32_t device_id;
    int max_queue_size;
    int fd;
    int i, ret;

    fd = qemu_open(s->vdpa_dev, O_RDWR, errp);
    if (fd == -1) {
        return;
    }
    s->vdpa.device_fd = fd;

    max_queue_size = vdpa_dev_get_queue_size(fd, errp);
    if (*errp) {
        goto out;
    }

    if (s->queue_size > max_queue_size) {
        error_setg(errp, "vhost-vdpa-device: invalid queue_size: %d (max:%d)",
                   s->queue_size, max_queue_size);
        goto out;
    } else if (!s->queue_size) {
        s->queue_size = max_queue_size;
    }

    ret = vdpa_dev_get_vqs_num(fd, errp);
    if (*errp) {
        goto out;
    }

    s->dev.nvqs = ret;
    s->dev.vqs = g_new0(struct vhost_virtqueue, s->dev.nvqs);
    s->dev.vq_index = 0;
    s->dev.vq_index_end = s->dev.nvqs;
    s->dev.backend_features = 0;
    s->started = false;

    ret = vhost_dev_init(&s->dev, &s->vdpa, VHOST_BACKEND_TYPE_VDPA, 0, NULL);
    if (ret < 0) {
        error_setg(errp, "vhost-vdpa-device: vhost initialization failed: %s",
                   strerror(-ret));
        goto out;
    }

    ret = s->dev.vhost_ops->vhost_get_device_id(&s->dev, &device_id);
    if (ret < 0) {
        error_setg(errp, "vhost-vdpa-device: vhost get device id failed: %s",
                   strerror(-ret));
        goto vhost_cleanup;
    }

    s->config_size = vdpa_dev_get_config_size(fd, errp);
    if (*errp) {
        goto vhost_cleanup;
    }

    s->config = g_malloc0(s->config_size);

    ret = vhost_dev_get_config(&s->dev, s->config, s->config_size, NULL);
    if (ret < 0) {
        error_setg(errp, "vhost-vdpa-device: get config failed");
        goto config_err;
    }

    virtio_init(vdev, "vhost-vdpa", device_id, s->config_size);

    s->virtqs = g_new0(VirtQueue *, s->dev.nvqs);
    for (i = 0; i < s->dev.nvqs; i++) {
        s->virtqs[i] = virtio_add_queue(vdev, s->queue_size,
                                        vhost_vdpa_device_dummy_handle_output);
    }

    return;
config_err:
    g_free(s->config);
vhost_cleanup:
    vhost_dev_cleanup(&s->dev);
out:
    close(fd);
}

static void vhost_vdpa_vdev_unrealize(VhostVdpaDevice *s)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(s);
    int i;

    for (i = 0; i < s->num_queues; i++) {
        virtio_delete_queue(s->virtqs[i]);
    }
    g_free(s->virtqs);
    virtio_cleanup(vdev);

    g_free(s->config);
}

static void vhost_vdpa_device_unrealize(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VhostVdpaDevice *s = VHOST_VDPA_DEVICE(vdev);

    virtio_set_status(vdev, 0);
    vhost_dev_cleanup(&s->dev);
    vhost_vdpa_vdev_unrealize(s);
    close(s->vdpa.device_fd);
}

static void
vhost_vdpa_device_get_config(VirtIODevice *vdev, uint8_t *config)
{
    return;
}

static void
vhost_vdpa_device_set_config(VirtIODevice *vdev, const uint8_t *config)
{
    return;
}

static uint64_t vhost_vdpa_device_get_features(VirtIODevice *vdev,
                                               uint64_t features,
                                               Error **errp)
{
    return (uint64_t)-1;
}

static void vhost_vdpa_device_set_status(VirtIODevice *vdev, uint8_t status)
{
    return;
}

static Property vhost_vdpa_device_properties[] = {
    DEFINE_PROP_STRING("vdpa-dev", VhostVdpaDevice, vdpa_dev),
    DEFINE_PROP_UINT16("queue-size", VhostVdpaDevice, queue_size, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_vhost_vdpa_device = {
    .name = "vhost-vdpa-device",
    .minimum_version_id = 1,
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
};

static void vhost_vdpa_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    device_class_set_props(dc, vhost_vdpa_device_properties);
    dc->desc = "VDPA-based generic PCI device assignment";
    dc->vmsd = &vmstate_vhost_vdpa_device;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    vdc->realize = vhost_vdpa_device_realize;
    vdc->unrealize = vhost_vdpa_device_unrealize;
    vdc->get_config = vhost_vdpa_device_get_config;
    vdc->set_config = vhost_vdpa_device_set_config;
    vdc->get_features = vhost_vdpa_device_get_features;
    vdc->set_status = vhost_vdpa_device_set_status;
}

static void vhost_vdpa_device_instance_init(Object *obj)
{
    VhostVdpaDevice *s = VHOST_VDPA_DEVICE(obj);

    device_add_bootindex_property(obj, &s->bootindex, "bootindex",
                                  NULL, DEVICE(obj));
}

static const TypeInfo vhost_vdpa_device_info = {
    .name = TYPE_VHOST_VDPA_DEVICE,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VhostVdpaDevice),
    .class_init = vhost_vdpa_device_class_init,
    .instance_init = vhost_vdpa_device_instance_init,
};

static void register_vhost_vdpa_device_type(void)
{
    type_register_static(&vhost_vdpa_device_info);
}

type_init(register_vhost_vdpa_device_type);
