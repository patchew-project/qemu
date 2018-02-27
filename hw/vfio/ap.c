/*
 * VFIO based AP matrix device assignment
 *
 * Copyright 2017 IBM Corp.
 * Author(s): Tony Krowiak <akrowiak@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or(at
 * your option) any version. See the COPYING file in the top-level
 * directory.
 */

#include <linux/vfio.h>
#include <sys/ioctl.h>
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/vfio/vfio.h"
#include "hw/vfio/vfio-common.h"
#include "hw/s390x/ap-device.h"
#include "qemu/error-report.h"
#include "qemu/queue.h"
#include "cpu.h"

#define VFIO_AP_DEVICE_TYPE      "vfio-ap"
#define AP_SYSFSDEV_PROP_NAME    "sysfsdev"

typedef struct VFIOAPDevice {
    APDevice apdev;
    VFIODevice vdev;
    QTAILQ_ENTRY(VFIOAPDevice) sibling;
} VFIOAPDevice;

static void vfio_ap_compute_needs_reset(VFIODevice *vdev)
{
    vdev->needs_reset = false;
}

/*
 * We don't need vfio_hot_reset_multi and vfio_eoi operations for
 * vfio-ap-matrix device now.
 */
struct VFIODeviceOps vfio_ap_ops = {
    .vfio_compute_needs_reset = vfio_ap_compute_needs_reset,
};

static QTAILQ_HEAD(, VFIOAPDevice) vfio_ap_devices =
        QTAILQ_HEAD_INITIALIZER(vfio_ap_devices);

static void vfio_put_device(VFIOAPDevice *apdev)
{
    g_free(apdev->vdev.name);
    vfio_put_base_device(&apdev->vdev);
}

static VFIOGroup *vfio_ap_get_group(VFIOAPDevice *vapdev, Error **errp)
{
    char *tmp, group_path[PATH_MAX];
    ssize_t len;
    int groupid;

    tmp = g_strdup_printf("%s/iommu_group", vapdev->vdev.sysfsdev);
    len = readlink(tmp, group_path, sizeof(group_path));
    g_free(tmp);

    if (len <= 0 || len >= sizeof(group_path)) {
        error_setg(errp, "%s: no iommu_group found for %s",
                   VFIO_AP_DEVICE_TYPE, vapdev->vdev.sysfsdev);
        return NULL;
    }

    group_path[len] = 0;

    if (sscanf(basename(group_path), "%d", &groupid) != 1) {
        error_setg(errp, "vfio: failed to read %s", group_path);
        return NULL;
    }

    return vfio_get_group(groupid, &address_space_memory, errp);
}

static void vfio_ap_realize(DeviceState *dev, Error **errp)
{
    VFIODevice *vbasedev;
    VFIOGroup *vfio_group;
    APDevice *apdev = DO_UPCAST(APDevice, parent_obj, dev);
    VFIOAPDevice *vapdev = DO_UPCAST(VFIOAPDevice, apdev, apdev);
    char *mdevid;
    Error *local_err = NULL;
    int ret;

    if (!s390_has_feat(S390_FEAT_AP)) {
        error_setg(&local_err, "Invalid device configuration: ");
        error_append_hint(&local_err,
                          "Verify AP facilities are enabled for the guest"
                          "(ap=on)\n");
        goto out_err;
    }

    vfio_group = vfio_ap_get_group(vapdev, &local_err);
    if (!vfio_group) {
        goto out_err;
    }

    vapdev->vdev.ops = &vfio_ap_ops;
    vapdev->vdev.type = VFIO_DEVICE_TYPE_AP;
    mdevid = basename(vapdev->vdev.sysfsdev);
    vapdev->vdev.name = g_strdup_printf("%s", mdevid);
    vapdev->vdev.dev = dev;
    QLIST_FOREACH(vbasedev, &vfio_group->device_list, next) {
        if (strcmp(vbasedev->name, vapdev->vdev.name) == 0) {
            error_setg(&local_err,
                       "%s: AP device %s has already been realized",
                       VFIO_AP_DEVICE_TYPE, vapdev->vdev.name);
            goto out_device_err;
        }
    }

    ret = vfio_get_device(vfio_group, mdevid, &vapdev->vdev, &local_err);
    if (ret) {
        goto out_device_err;
    }

    QTAILQ_INSERT_TAIL(&vfio_ap_devices, vapdev, sibling);

    return;

out_device_err:
    vfio_put_group(vfio_group);
out_err:
    error_propagate(errp, local_err);
}

static void vfio_ap_unrealize(DeviceState *dev, Error **errp)
{
    APDevice *apdev = DO_UPCAST(APDevice, parent_obj, dev);
    VFIOAPDevice *vapdev = DO_UPCAST(VFIOAPDevice, apdev, apdev);
    VFIOGroup *group = vapdev->vdev.group;

    vfio_put_device(vapdev);
    vfio_put_group(group);
}

static Property vfio_ap_properties[] = {
    DEFINE_PROP_STRING(AP_SYSFSDEV_PROP_NAME, VFIOAPDevice, vdev.sysfsdev),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vfio_ap_vmstate = {
    .name = VFIO_AP_DEVICE_TYPE,
    .unmigratable = 1,
};

static void vfio_ap_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->props = vfio_ap_properties;
    dc->vmsd = &vfio_ap_vmstate;
    dc->desc = "VFIO-based AP device assignment";
    dc->realize = vfio_ap_realize;
    dc->unrealize = vfio_ap_unrealize;
}

static const TypeInfo vfio_ap_info = {
    .name = VFIO_AP_DEVICE_TYPE,
    .parent = AP_DEVICE_TYPE,
    .instance_size = sizeof(VFIOAPDevice),
    .class_init = vfio_ap_class_init,
};

static void register_vfio_ap_type(void)
{
    type_register_static(&vfio_ap_info);
}

type_init(register_vfio_ap_type)
