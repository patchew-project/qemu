/*
 * VFIO based AP matrix device assignment
 *
 * Copyright 2018 IBM Corp.
 * Author(s): Tony Krowiak <akrowiak@linux.ibm.com>
 *            Halil Pasic <pasic@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "qemu/osdep.h"
#include <linux/vfio.h>
#include <sys/ioctl.h>
#include "qapi/error.h"
#include "hw/vfio/vfio.h"
#include "hw/vfio/vfio-common.h"
#include "hw/s390x/ap-device.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "kvm/kvm_s390x.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/s390x/ap-bridge.h"
#include "exec/address-spaces.h"
#include "qom/object.h"

#define TYPE_VFIO_AP_DEVICE      "vfio-ap"

struct VFIOAPDevice {
    APDevice apdev;
    VFIODevice vdev;
};

OBJECT_DECLARE_SIMPLE_TYPE(VFIOAPDevice, VFIO_AP_DEVICE)

static void vfio_ap_compute_needs_reset(VFIODevice *vdev)
{
    vdev->needs_reset = false;
}

/*
 * We don't need vfio_hot_reset_multi and vfio_eoi operations for
 * vfio-ap device now.
 */
struct VFIODeviceOps vfio_ap_ops = {
    .vfio_compute_needs_reset = vfio_ap_compute_needs_reset,
};

static void vfio_ap_realize(DeviceState *dev, Error **errp)
{
    APDevice *apdev = AP_DEVICE(dev);
    VFIOAPDevice *vapdev = VFIO_AP_DEVICE(apdev);
    VFIODevice *vbasedev = &vapdev->vdev;
    int ret;

    vbasedev->name = g_path_get_basename(vbasedev->sysfsdev);
    vbasedev->ops = &vfio_ap_ops;
    vbasedev->type = VFIO_DEVICE_TYPE_AP;
    vbasedev->dev = dev;

    /*
     * vfio-ap devices operate in a way compatible with discarding of
     * memory in RAM blocks, as no pages are pinned in the host.
     * This needs to be set before vfio_get_device() for vfio common to
     * handle ram_block_discard_disable().
     */
    vapdev->vdev.ram_block_discard_allowed = true;

    ret = vfio_attach_device(vbasedev, &address_space_memory, errp);
    if (ret) {
        goto out_get_dev_err;
    }

    return;

out_get_dev_err:
    vfio_detach_device(vbasedev);
}

static void vfio_ap_unrealize(DeviceState *dev)
{
    APDevice *apdev = AP_DEVICE(dev);
    VFIOAPDevice *vapdev = VFIO_AP_DEVICE(apdev);

    vfio_detach_device(&vapdev->vdev);
}

static Property vfio_ap_properties[] = {
    DEFINE_PROP_STRING("sysfsdev", VFIOAPDevice, vdev.sysfsdev),
    DEFINE_PROP_END_OF_LIST(),
};

static void vfio_ap_reset(DeviceState *dev)
{
    int ret;
    APDevice *apdev = AP_DEVICE(dev);
    VFIOAPDevice *vapdev = VFIO_AP_DEVICE(apdev);

    ret = ioctl(vapdev->vdev.fd, VFIO_DEVICE_RESET);
    if (ret) {
        error_report("%s: failed to reset %s device: %s", __func__,
                     vapdev->vdev.name, strerror(errno));
    }
}

static const VMStateDescription vfio_ap_vmstate = {
    .name = "vfio-ap",
    .unmigratable = 1,
};

static void vfio_ap_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, vfio_ap_properties);
    dc->vmsd = &vfio_ap_vmstate;
    dc->desc = "VFIO-based AP device assignment";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->realize = vfio_ap_realize;
    dc->unrealize = vfio_ap_unrealize;
    dc->hotpluggable = true;
    dc->reset = vfio_ap_reset;
    dc->bus_type = TYPE_AP_BUS;
}

static const TypeInfo vfio_ap_info = {
    .name = TYPE_VFIO_AP_DEVICE,
    .parent = TYPE_AP_DEVICE,
    .instance_size = sizeof(VFIOAPDevice),
    .class_init = vfio_ap_class_init,
};

static void vfio_ap_type_init(void)
{
    type_register_static(&vfio_ap_info);
}

type_init(vfio_ap_type_init)
