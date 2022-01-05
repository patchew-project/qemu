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

static void vhost_vdpa_device_realize(DeviceState *dev, Error **errp)
{
    return;
}

static void vhost_vdpa_device_unrealize(DeviceState *dev)
{
    return;
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
