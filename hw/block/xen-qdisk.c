/*
 * Copyright (c) Citrix Systems Inc.
 * All rights reserved.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/hw.h"
#include "hw/xen/xen-qdisk.h"
#include "trace.h"

static void xen_qdisk_realize(XenDevice *xendev, Error **errp)
{
    XenQdiskDevice *qdiskdev = XEN_QDISK_DEVICE(xendev);
    XenQdiskVdev *vdev = &qdiskdev->vdev;

    if (!vdev->valid) {
        error_setg(errp, "vdev property not set");
        return;
    }

    trace_xen_qdisk_realize(vdev->disk, vdev->partition);
}

static void xen_qdisk_unrealize(XenDevice *xendev, Error **errp)
{
    XenQdiskDevice *qdiskdev = XEN_QDISK_DEVICE(xendev);
    XenQdiskVdev *vdev = &qdiskdev->vdev;

    trace_xen_qdisk_unrealize(vdev->disk, vdev->partition);
}

static char *disk_to_vbd_name(unsigned int disk)
{
    unsigned int len = DIV_ROUND_UP(disk, 26);
    char *name = g_malloc0(len + 1);

    do {
        name[len--] = 'a' + (disk % 26);
        disk /= 26;
    } while (disk != 0);
    assert(len == 0);

    return name;
}

static void xen_qdisk_get_vdev(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    XenQdiskVdev *vdev = qdev_get_prop_ptr(dev, prop);
    char *str;

    switch (vdev->type) {
    case XEN_QDISK_VDEV_TYPE_DP:
        str = g_strdup_printf("d%lup%lu", vdev->disk, vdev->partition);
        break;

    case XEN_QDISK_VDEV_TYPE_XVD:
    case XEN_QDISK_VDEV_TYPE_HD:
    case XEN_QDISK_VDEV_TYPE_SD: {
        char *name = disk_to_vbd_name(vdev->disk);

        str = g_strdup_printf("%s%s%lu",
                              (vdev->type == XEN_QDISK_VDEV_TYPE_XVD) ?
                              "xvd" :
                              (vdev->type == XEN_QDISK_VDEV_TYPE_HD) ?
                              "hd" :
                              "sd",
                              name, vdev->partition);
        g_free(name);
        break;
    }
    default:
        error_setg(errp, "invalid vdev type");
        return;
    }

    visit_type_str(v, name, &str, errp);
    g_free(str);
}

static unsigned int vbd_name_to_disk(const char *name, const char **endp)
{
    unsigned int disk = 0;

    while (*name != '\0') {
        if (!g_ascii_isalpha(*name) || !g_ascii_islower(*name)) {
            break;
        }

        disk *= 26;
        disk += *name++ - 'a';
    }
    *endp = name;

    return disk;
}

static void xen_qdisk_set_vdev(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    XenQdiskVdev *vdev = qdev_get_prop_ptr(dev, prop);
    Error *local_err = NULL;
    char *str, *p;
    const char *end;

    if (dev->realized) {
        qdev_prop_set_after_realize(dev, name, errp);
        return;
    }

    visit_type_str(v, name, &str, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    p = strchr(str, 'd');
    if (!p) {
        goto invalid;
    }

    *p++ = '\0';
    if (*str == '\0') {
        vdev->type = XEN_QDISK_VDEV_TYPE_DP;
    } else if (strcmp(str, "xv") == 0) {
        vdev->type = XEN_QDISK_VDEV_TYPE_XVD;
    } else if (strcmp(str, "h") == 0) {
        vdev->type = XEN_QDISK_VDEV_TYPE_HD;
    } else if (strcmp(str, "s") == 0) {
        vdev->type = XEN_QDISK_VDEV_TYPE_SD;
    } else {
        goto invalid;
    }

    if (vdev->type == XEN_QDISK_VDEV_TYPE_DP) {
        if (qemu_strtoul(p, &end, 10, &vdev->disk)) {
            goto invalid;
        }

        if (*end == 'p') {
            p = (char *) ++end;
            if (*end == '\0') {
                goto invalid;
            }
        }
    } else {
        vdev->disk = vbd_name_to_disk(p, &end);
    }

    if (*end != '\0') {
        p = (char *)end;

        if (qemu_strtoul(p, &end, 10, &vdev->partition)) {
            goto invalid;
        }

        if (*end != '\0') {
            goto invalid;
        }
    } else {
        vdev->partition = 0;
    }

    switch (vdev->type) {
    case XEN_QDISK_VDEV_TYPE_DP:
    case XEN_QDISK_VDEV_TYPE_XVD:
        if (vdev->disk < (1 << 4) && vdev->partition < (1 << 4)) {
            vdev->number = (202 << 8) | (vdev->disk << 4) |
                vdev->partition;
        } else if (vdev->disk < (1 << 20) && vdev->partition < (1 << 8)) {
            vdev->number = (1 << 28) | (vdev->disk << 8) |
                vdev->partition;
        } else {
            goto invalid;
        }
        break;

    case XEN_QDISK_VDEV_TYPE_HD:
        if ((vdev->disk == 0 || vdev->disk == 1) &&
            vdev->partition < (1 << 4)) {
            vdev->number = (3 << 8) | (vdev->disk << 6) | vdev->partition;
        } else if ((vdev->disk == 2 || vdev->disk == 3) &&
                   vdev->partition < (1 << 4)) {
            vdev->number = (22 << 8) | ((vdev->disk - 2) << 6) |
                vdev->partition;
        } else {
            goto invalid;
        }
        break;

    case XEN_QDISK_VDEV_TYPE_SD:
        if (vdev->disk < (1 << 4) && vdev->partition < (1 << 4)) {
            vdev->number = (8 << 8) | (vdev->disk << 4) | vdev->partition;
        } else {
            goto invalid;
        }
        break;

    default:
        goto invalid;
    }

    g_free(str);
    vdev->valid = true;
    return;

invalid:
    error_setg(errp, "invalid virtual disk specifier");
    g_free(str);
}

const PropertyInfo xen_qdisk_prop_vdev = {
    .name  = "str",
    .description = "Virtual Disk specifier: d*p*/xvd*/hd*/sd*",
    .get = xen_qdisk_get_vdev,
    .set = xen_qdisk_set_vdev,
};

static Property xen_qdisk_props[] = {
    DEFINE_PROP("vdev", XenQdiskDevice, vdev,
                xen_qdisk_prop_vdev, XenQdiskVdev),
    DEFINE_PROP_END_OF_LIST()
};

static void xen_qdisk_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dev_class = DEVICE_CLASS(class);
    XenDeviceClass *xendev_class = XEN_DEVICE_CLASS(class);

    xendev_class->realize = xen_qdisk_realize;
    xendev_class->unrealize = xen_qdisk_unrealize;

    dev_class->desc = "Xen Qdisk Device";
    dev_class->props = xen_qdisk_props;
}

static const TypeInfo xen_qdisk_type_info = {
    .name = TYPE_XEN_QDISK_DEVICE,
    .parent = TYPE_XEN_DEVICE,
    .instance_size = sizeof(XenQdiskDevice),
    .class_init = xen_qdisk_class_init,
};

static void xen_qdisk_register_types(void)
{
    type_register_static(&xen_qdisk_type_info);
}

type_init(xen_qdisk_register_types)
