/*
 * vfio based subchannel assignment support
 *
 * Copyright 2017 IBM Corp.
 * Author(s): Dong Jia Shi <bjsdjshi@linux.vnet.ibm.com>
 *            Xiao Feng Ren <renxiaof@linux.vnet.ibm.com>
 *            Pierre Morel <pmorel@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or(at
 * your option) any version. See the COPYING file in the top-level
 * directory.
 */

#include <linux/vfio.h>
#include <linux/vfio_ccw.h>
#include <sys/ioctl.h>

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/vfio/vfio.h"
#include "hw/vfio/vfio-common.h"
#include "hw/s390x/s390-ccw.h"
#include "hw/s390x/ccw-device.h"
#include "qemu/error-report.h"

#define TYPE_VFIO_CCW "vfio-ccw"
typedef struct VFIOCCWDevice {
    S390CCWDevice cdev;
    VFIODevice vdev;
    uint64_t io_region_size;
    uint64_t io_region_offset;
    struct ccw_io_region *io_region;
    EventNotifier io_notifier;
} VFIOCCWDevice;

static void vfio_ccw_compute_needs_reset(VFIODevice *vdev)
{
    vdev->needs_reset = false;
}

/*
 * We don't need vfio_hot_reset_multi and vfio_eoi operationis for
 * vfio_ccw device now.
 */
struct VFIODeviceOps vfio_ccw_ops = {
    .vfio_compute_needs_reset = vfio_ccw_compute_needs_reset,
};

static void vfio_ccw_reset(DeviceState *dev)
{
    CcwDevice *ccw_dev = DO_UPCAST(CcwDevice, parent_obj, dev);
    S390CCWDevice *cdev = DO_UPCAST(S390CCWDevice, parent_obj, ccw_dev);
    VFIOCCWDevice *vcdev = DO_UPCAST(VFIOCCWDevice, cdev, cdev);

    ioctl(vcdev->vdev.fd, VFIO_DEVICE_RESET);
}

static void vfio_ccw_io_notifier_handler(void *opaque)
{
    VFIOCCWDevice *vcdev = opaque;

    if (!event_notifier_test_and_clear(&vcdev->io_notifier)) {
        return;
    }
}

static void vfio_ccw_register_io_notifier(VFIOCCWDevice *vcdev, Error **errp)
{
    VFIODevice *vdev = &vcdev->vdev;
    struct vfio_irq_info *irq_info;
    struct vfio_irq_set *irq_set;
    size_t argsz;
    int32_t *pfd;

    if (vdev->num_irqs < VFIO_CCW_IO_IRQ_INDEX + 1) {
        error_setg(errp, "vfio: unexpected number of io irqs %u",
                   vdev->num_irqs);
        return;
    }

    argsz = sizeof(*irq_set);
    irq_info = g_malloc0(argsz);
    irq_info->index = VFIO_CCW_IO_IRQ_INDEX;
    irq_info->argsz = argsz;
    if (ioctl(vdev->fd, VFIO_DEVICE_GET_IRQ_INFO,
              irq_info) < 0 || irq_info->count < 1) {
        error_setg(errp, "vfio: Error getting irq info");
        goto get_error;
    }

    if (event_notifier_init(&vcdev->io_notifier, 0)) {
        error_setg(errp, "vfio: Unable to init event notifier for IO");
        goto get_error;
    }

    argsz = sizeof(*irq_set) + sizeof(*pfd);
    irq_set = g_malloc0(argsz);
    irq_set->argsz = argsz;
    irq_set->flags = VFIO_IRQ_SET_DATA_EVENTFD |
                     VFIO_IRQ_SET_ACTION_TRIGGER;
    irq_set->index = VFIO_CCW_IO_IRQ_INDEX;
    irq_set->start = 0;
    irq_set->count = 1;
    pfd = (int32_t *) &irq_set->data;

    *pfd = event_notifier_get_fd(&vcdev->io_notifier);
    qemu_set_fd_handler(*pfd, vfio_ccw_io_notifier_handler, NULL, vcdev);
    if (ioctl(vdev->fd, VFIO_DEVICE_SET_IRQS, irq_set)) {
        error_setg(errp, "vfio: Failed to set up io notification");
        qemu_set_fd_handler(*pfd, NULL, NULL, vcdev);
        event_notifier_cleanup(&vcdev->io_notifier);
        goto set_error;
    }

set_error:
    g_free(irq_set);

get_error:
    g_free(irq_info);
}

static void vfio_ccw_unregister_io_notifier(VFIOCCWDevice *vcdev)
{
    struct vfio_irq_set *irq_set;
    size_t argsz;
    int32_t *pfd;

    argsz = sizeof(*irq_set) + sizeof(*pfd);
    irq_set = g_malloc0(argsz);
    irq_set->argsz = argsz;
    irq_set->flags = VFIO_IRQ_SET_DATA_EVENTFD |
                     VFIO_IRQ_SET_ACTION_TRIGGER;
    irq_set->index = VFIO_CCW_IO_IRQ_INDEX;
    irq_set->start = 0;
    irq_set->count = 1;
    pfd = (int32_t *) &irq_set->data;
    *pfd = -1;

    if (ioctl(vcdev->vdev.fd, VFIO_DEVICE_SET_IRQS, irq_set)) {
        error_report("vfio: Failed to de-assign device io fd");
    }

    qemu_set_fd_handler(event_notifier_get_fd(&vcdev->io_notifier),
                        NULL, NULL, vcdev);
    event_notifier_cleanup(&vcdev->io_notifier);

    g_free(irq_set);
}

static void vfio_ccw_get_region(VFIOCCWDevice *vcdev, Error **errp)
{
    VFIODevice *vdev = &vcdev->vdev;
    struct vfio_region_info *info;
    int ret;

    /* Sanity check device */
    if (!(vdev->flags & VFIO_DEVICE_FLAGS_CCW)) {
        error_setg(errp, "vfio: Um, this isn't a vfio-ccw device");
        return;
    }

    if (vdev->num_regions < VFIO_CCW_CONFIG_REGION_INDEX + 1) {
        error_setg(errp, "vfio: Unexpected number of the I/O region %u",
                   vdev->num_regions);
        return;
    }

    ret = vfio_get_region_info(vdev, VFIO_CCW_CONFIG_REGION_INDEX, &info);
    if (ret) {
        error_setg(errp, "vfio: Error getting config info: %d", ret);
        return;
    }

    vcdev->io_region_size = info->size;
    if (sizeof(*vcdev->io_region) != vcdev->io_region_size) {
        error_setg(errp, "vfio: Unexpected size of the I/O region");
        return;
    }
    vcdev->io_region_offset = info->offset;
    vcdev->io_region = g_malloc0(info->size);

    g_free(info);
}

static void vfio_ccw_put_region(VFIOCCWDevice *vcdev)
{
    g_free(vcdev->io_region);
}

static void vfio_put_device(VFIOCCWDevice *vcdev)
{
    g_free(vcdev->vdev.name);
    vfio_put_base_device(&vcdev->vdev);
}

static VFIOGroup *vfio_ccw_get_group(S390CCWDevice *cdev, char **path,
                                     Error **errp)
{
    struct stat st;
    int groupid;
    GError *gerror = NULL;

    /* Check that host subchannel exists. */
    path[0] = g_strdup_printf("/sys/bus/css/devices/%x.%x.%04x",
                              cdev->hostid.cssid,
                              cdev->hostid.ssid,
                              cdev->hostid.devid);
    if (stat(path[0], &st) < 0) {
        error_setg(errp, "vfio: no such host subchannel %s", path[0]);
        return NULL;
    }

    /* Check that mediated device exists. */
    path[1] = g_strdup_printf("%s/%s", path[0], cdev->mdevid);
    if (stat(path[0], &st) < 0) {
        error_setg(errp, "vfio: no such mediated device %s", path[1]);
        return NULL;
    }

    /* Get the iommu_group patch as the interim variable. */
    path[2] = g_strconcat(path[1], "/iommu_group", NULL);

    /* Get the link file path of the device iommu_group. */
    path[3] = g_file_read_link(path[2], &gerror);
    if (!path[3]) {
        error_setg(errp, "vfio: error no iommu_group for subchannel");
        return NULL;
    }

    /* Get the device groupid. */
    if (sscanf(basename(path[3]), "%d", &groupid) != 1) {
        error_setg(errp, "vfio: error reading %s:%m", path[3]);
        return NULL;
    }

    return vfio_get_group(groupid, &address_space_memory, errp);
}

static void vfio_ccw_put_group(VFIOGroup *group, char **path)
{
    g_free(path);
    vfio_put_group(group);
}

static void vfio_ccw_realize(DeviceState *dev, Error **errp)
{
    VFIODevice *vbasedev;
    VFIOGroup *group;
    CcwDevice *ccw_dev = DO_UPCAST(CcwDevice, parent_obj, dev);
    S390CCWDevice *cdev = DO_UPCAST(S390CCWDevice, parent_obj, ccw_dev);
    VFIOCCWDevice *vcdev = DO_UPCAST(VFIOCCWDevice, cdev, cdev);
    S390CCWDeviceClass *cdc = S390_CCW_DEVICE_GET_CLASS(cdev);
    char *path[4] = {NULL, NULL, NULL, NULL};

    /* Call the class init function for subchannel. */
    if (cdc->realize) {
        cdc->realize(cdev, vcdev->vdev.sysfsdev, errp);
        if (*errp) {
            return;
        }
    }

    group = vfio_ccw_get_group(cdev, path, errp);
    if (!group) {
        goto out_group_err;
    }

    vcdev->vdev.ops = &vfio_ccw_ops;
    vcdev->vdev.type = VFIO_DEVICE_TYPE_CCW;
    vcdev->vdev.name = g_strdup_printf("%x.%x.%04x", cdev->hostid.cssid,
                                       cdev->hostid.ssid, cdev->hostid.devid);
    QLIST_FOREACH(vbasedev, &group->device_list, next) {
        if (strcmp(vbasedev->name, vcdev->vdev.name) == 0) {
            error_setg(errp, "vfio: subchannel %s has already been attached",
                       basename(path[0]));
            goto out_device_err;
        }
    }

    if (vfio_get_device(group, cdev->mdevid, &vcdev->vdev, errp)) {
        goto out_device_err;
    }

    vfio_ccw_get_region(vcdev, errp);
    if (*errp) {
        goto out_region_err;
    }

    vfio_ccw_register_io_notifier(vcdev, errp);
    if (*errp) {
        goto out_notifier_err;
    }

    return;

out_notifier_err:
    vfio_ccw_put_region(vcdev);
out_region_err:
    vfio_put_device(vcdev);
out_device_err:
    vfio_ccw_put_group(group, path);
out_group_err:
    if (cdc->unrealize) {
        cdc->unrealize(cdev, errp);
    }
}

static void vfio_ccw_unrealize(DeviceState *dev, Error **errp)
{
    CcwDevice *ccw_dev = DO_UPCAST(CcwDevice, parent_obj, dev);
    S390CCWDevice *cdev = DO_UPCAST(S390CCWDevice, parent_obj, ccw_dev);
    VFIOCCWDevice *vcdev = DO_UPCAST(VFIOCCWDevice, cdev, cdev);
    S390CCWDeviceClass *cdc = S390_CCW_DEVICE_GET_CLASS(cdev);
    VFIOGroup *group = vcdev->vdev.group;

    if (cdc->unrealize) {
        cdc->unrealize(cdev, errp);
    }

    vfio_ccw_unregister_io_notifier(vcdev);
    vfio_ccw_put_region(vcdev);
    vfio_put_device(vcdev);
    vfio_put_group(group);
}

static Property vfio_ccw_properties[] = {
    DEFINE_PROP_STRING("sysfsdev", VFIOCCWDevice, vdev.sysfsdev),
    DEFINE_PROP_CSS_DEV_ID("devno", VFIOCCWDevice, cdev.parent_obj.bus_id),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vfio_ccw_vmstate = {
    .name = TYPE_VFIO_CCW,
    .unmigratable = 1,
};

static void vfio_ccw_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->props = vfio_ccw_properties;
    dc->vmsd = &vfio_ccw_vmstate;
    dc->desc = "VFIO-based subchannel assignment";
    dc->realize = vfio_ccw_realize;
    dc->unrealize = vfio_ccw_unrealize;
    dc->reset = vfio_ccw_reset;
}

static const TypeInfo vfio_ccw_info = {
    .name = TYPE_VFIO_CCW,
    .parent = TYPE_S390_CCW,
    .instance_size = sizeof(VFIOCCWDevice),
    .class_init = vfio_ccw_class_init,
};

static void register_vfio_ccw_type(void)
{
    type_register_static(&vfio_ccw_info);
}

type_init(register_vfio_ccw_type)
