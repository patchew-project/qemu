/*
 * Copyright (c) Citrix Systems Inc.
 * All rights reserved.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/option.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qapi/qmp/qdict.h"
#include "hw/hw.h"
#include "hw/xen/xen-backend.h"
#include "hw/xen/xen-qdisk.h"
#include "sysemu/blockdev.h"
#include "sysemu/block-backend.h"
#include "sysemu/iothread.h"
#include "dataplane/xen-qdisk.h"
#include "trace.h"

static char *xen_qdisk_get_name(XenDevice *xendev, Error **errp)
{
    XenQdiskDevice *qdiskdev = XEN_QDISK_DEVICE(xendev);
    XenQdiskVdev *vdev = &qdiskdev->vdev;

    return g_strdup_printf("%lu", vdev->number);
}

static void xen_qdisk_realize(XenDevice *xendev, Error **errp)
{
    XenQdiskDevice *qdiskdev = XEN_QDISK_DEVICE(xendev);
    XenQdiskVdev *vdev = &qdiskdev->vdev;
    BlockConf *conf = &qdiskdev->conf;
    IOThread *iothread = qdiskdev->auto_iothread ?
        qdiskdev->auto_iothread : qdiskdev->iothread;
    DriveInfo *dinfo;
    bool is_cdrom;
    unsigned int info;
    int64_t size;

    if (!vdev->valid) {
        error_setg(errp, "vdev property not set");
        return;
    }

    trace_xen_qdisk_realize(vdev->disk, vdev->partition);

    if (!conf->blk) {
        error_setg(errp, "drive property not set");
        return;
    }

    if (!blk_is_inserted(conf->blk)) {
        error_setg(errp, "device needs media, but drive is empty");
        return;
    }

    if (!blkconf_apply_backend_options(conf, blk_is_read_only(conf->blk),
                                       false, errp)) {
        return;
    }

    if (!blkconf_geometry(conf, NULL, 65535, 255, 255, errp)) {
        return;
    }

    dinfo = blk_legacy_dinfo(conf->blk);
    is_cdrom = (dinfo && dinfo->media_cd);

    blkconf_blocksizes(conf);

    if (conf->logical_block_size > conf->physical_block_size) {
        error_setg(
            errp, "logical_block_size > physical_block_size not supported");
        return;
    }

    blk_set_guest_block_size(conf->blk, conf->logical_block_size);

    if (conf->discard_granularity > 0) {
        xen_device_backend_printf(xendev, "feature-discard", "%u", 1);
    }

    xen_device_backend_printf(xendev, "feature-flush-cache", "%u", 1);
    xen_device_backend_printf(xendev, "max-ring-page-order", "%u",
                              qdiskdev->max_ring_page_order);

    info = blk_is_read_only(conf->blk) ? VDISK_READONLY : 0;
    info |= is_cdrom ? VDISK_CDROM : 0;

    xen_device_backend_printf(xendev, "info", "%u", info);

    xen_device_frontend_printf(xendev, "virtual-device", "%u",
                               vdev->number);
    xen_device_frontend_printf(xendev, "device-type", "%s",
                               is_cdrom ? "cdrom" : "disk");

    size = blk_getlength(conf->blk);
    xen_device_backend_printf(xendev, "sector-size", "%u",
                              conf->logical_block_size);
    xen_device_backend_printf(xendev, "sectors", "%lu",
                              size / conf->logical_block_size);

    qdiskdev->dataplane = xen_qdisk_dataplane_create(xendev, conf,
                                                     iothread);
}

static void xen_qdisk_connect(XenQdiskDevice *qdiskdev, Error **errp)
{
    XenQdiskVdev *vdev = &qdiskdev->vdev;
    XenDevice *xendev = XEN_DEVICE(qdiskdev);
    unsigned int order, nr_ring_ref, *ring_ref, event_channel, protocol;
    char *str;

    trace_xen_qdisk_connect(vdev->disk, vdev->partition);

    if (xen_device_frontend_scanf(xendev, "ring-page-order", "%u",
                                  &order) != 1) {
        nr_ring_ref = 1;
        ring_ref = g_new(unsigned int, nr_ring_ref);

        if (xen_device_frontend_scanf(xendev, "ring-ref", "%u",
                                      &ring_ref[0]) != 1) {
            error_setg(errp, "failed to read ring-ref");
            return;
        }
    } else if (order <= qdiskdev->max_ring_page_order) {
        unsigned int i;

        nr_ring_ref = 1 << order;
        ring_ref = g_new(unsigned int, nr_ring_ref);

        for (i = 0; i < nr_ring_ref; i++) {
            const char *key = g_strdup_printf("ring-ref%u", i);

            if (xen_device_frontend_scanf(xendev, key, "%u",
                                          &ring_ref[i]) != 1) {
                error_setg(errp, "failed to read %s", key);
                g_free((gpointer)key);
                return;
            }

            g_free((gpointer)key);
        }
    } else {
        error_setg(errp, "invalid ring-page-order (%d)", order);
        return;
    }

    if (xen_device_frontend_scanf(xendev, "event-channel", "%u",
                                  &event_channel) != 1) {
        error_setg(errp, "failed to read event-channel");
        return;
    }

    if (xen_device_frontend_scanf(xendev, "protocol", "%ms",
                                  &str) != 1) {
        protocol = BLKIF_PROTOCOL_NATIVE;
    } else {
        if (strcmp(str, XEN_IO_PROTO_ABI_X86_32) == 0) {
            protocol = BLKIF_PROTOCOL_X86_32;
        } else if (strcmp(str, XEN_IO_PROTO_ABI_X86_64) == 0) {
            protocol = BLKIF_PROTOCOL_X86_64;
        } else {
            protocol = BLKIF_PROTOCOL_NATIVE;
        }

        free(str);
    }

    xen_qdisk_dataplane_start(qdiskdev->dataplane, ring_ref, nr_ring_ref,
                              event_channel, protocol);

    g_free(ring_ref);
}

static void xen_qdisk_disconnect(XenQdiskDevice *qdiskdev, Error **errp)
{
    XenQdiskVdev *vdev = &qdiskdev->vdev;

    trace_xen_qdisk_disconnect(vdev->disk, vdev->partition);

    xen_qdisk_dataplane_stop(qdiskdev->dataplane);
}

static void xen_qdisk_frontend_changed(XenDevice *xendev,
                                       enum xenbus_state frontend_state,
                                       Error **errp)
{
    XenQdiskDevice *qdiskdev = XEN_QDISK_DEVICE(xendev);
    enum xenbus_state backend_state = xen_device_backend_get_state(xendev);
    Error *local_err = NULL;

    switch (frontend_state) {
    case XenbusStateInitialised:
    case XenbusStateConnected:
        if (backend_state == XenbusStateConnected) {
            break;
        }

        xen_qdisk_disconnect(qdiskdev, &error_fatal);
        xen_qdisk_connect(qdiskdev, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            break;
        }

        xen_device_backend_set_state(xendev, XenbusStateConnected);
        break;

    case XenbusStateClosing:
        xen_device_backend_set_state(xendev, XenbusStateClosing);
        break;

    case XenbusStateClosed:
        xen_qdisk_disconnect(qdiskdev, &error_fatal);
        xen_device_backend_set_state(xendev, XenbusStateClosed);
        break;

    default:
        break;
    }
}

static void xen_qdisk_unrealize(XenDevice *xendev, Error **errp)
{
    XenQdiskDevice *qdiskdev = XEN_QDISK_DEVICE(xendev);
    XenQdiskVdev *vdev = &qdiskdev->vdev;

    trace_xen_qdisk_unrealize(vdev->disk, vdev->partition);

    xen_qdisk_disconnect(qdiskdev, &error_fatal);

    xen_qdisk_dataplane_destroy(qdiskdev->dataplane);
    qdiskdev->dataplane = NULL;

    if (qdiskdev->auto_iothread) {
        iothread_destroy(qdiskdev->auto_iothread);
        qdiskdev->auto_iothread = NULL;
    }
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
    DEFINE_BLOCK_PROPERTIES(XenQdiskDevice, conf),
    DEFINE_PROP_UINT32("max-ring-page-order", XenQdiskDevice,
                       max_ring_page_order, 4),
    DEFINE_PROP_LINK("iothread", XenQdiskDevice, iothread, TYPE_IOTHREAD,
                     IOThread *),
    DEFINE_PROP_END_OF_LIST()
};

static void xen_qdisk_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dev_class = DEVICE_CLASS(class);
    XenDeviceClass *xendev_class = XEN_DEVICE_CLASS(class);

    xendev_class->backend = "qdisk";
    xendev_class->device = "vbd";
    xendev_class->get_name = xen_qdisk_get_name;
    xendev_class->realize = xen_qdisk_realize;
    xendev_class->frontend_changed = xen_qdisk_frontend_changed;
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

static void xen_qdisk_drive_create(const char *id, QDict *opts,
                                   Error **errp)
{
    const char *params, *device_type, *mode, *direct_io_safe,
        *discard_enable;
    char *format = NULL;
    char *file = NULL;
    char *drive_optstr = NULL;
    QemuOpts *drive_opts;
    Error *local_err = NULL;

    params = qdict_get_try_str(opts, "params");
    if (params) {
        char **v = g_strsplit(params, ":", 2);

        if (v[1] == NULL) {
            file = g_strdup(v[0]);
        } else {
            if (strcmp(v[0], "aio") == 0) {
                format = g_strdup("raw");
            } else if (strcmp(v[0], "vhd") == 0) {
                format = g_strdup("vpc");
            } else {
                format = g_strdup(v[0]);
            }
            file = g_strdup(v[1]);
        }

        g_strfreev(v);
    }

    if (!file) {
        error_setg(errp, "no file parameter");
        return;
    }

    drive_optstr = g_strdup_printf("id=%s", id);
    drive_opts = drive_def(drive_optstr);
    if (!drive_opts) {
        error_setg(errp, "failed to create drive options");
        goto done;
    }

    qemu_opt_set(drive_opts, "file", file, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        error_prepend(errp, "failed to set 'file': ");
        goto done;
    }

    if (format) {
        qemu_opt_set(drive_opts, "format", format, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            error_prepend(errp, "failed to set 'format': ");
            goto done;
        }
    }

    device_type = qdict_get_try_str(opts, "device-type");
    if (device_type) {
        qemu_opt_set(drive_opts, "media", device_type, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            error_prepend(errp, "failed to set 'media': ");
            goto done;
        }
    }

    mode = qdict_get_try_str(opts, "mode");
    if (mode && *mode != 'w') {
        qemu_opt_set_bool(drive_opts, BDRV_OPT_READ_ONLY, true, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            error_prepend(errp, "failed to set '%s': ", BDRV_OPT_READ_ONLY);
            goto done;
        }
    }

    qemu_opt_set(drive_opts, "file.locking", "off", &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        error_prepend(errp, "failed to set 'file.locking': ");
        goto done;
    }

    qemu_opt_set_bool(drive_opts, BDRV_OPT_CACHE_WB, true, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        error_prepend(errp, "failed to set '%s': ", BDRV_OPT_CACHE_WB);
        goto done;
    }

    direct_io_safe = qdict_get_try_str(opts, "direct-io-safe");
    if (direct_io_safe) {
        qemu_opt_set_bool(drive_opts, BDRV_OPT_CACHE_DIRECT, true,
                          &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            error_prepend(errp, "failed to set '%s': ",
                          BDRV_OPT_CACHE_DIRECT);
            goto done;
        }

        qemu_opt_set(drive_opts, "aio", "native", &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            error_prepend(errp, "failed to set 'aio': ");
            goto done;
        }
    }

    discard_enable = qdict_get_try_str(opts, "discard-enable");
    if (discard_enable) {
        unsigned long value;

        if (!qemu_strtoul(discard_enable, NULL, 2, &value)) {
            qemu_opt_set_bool(drive_opts, BDRV_OPT_DISCARD, !!value,
                              &local_err);
            if (local_err) {
                error_propagate(errp, local_err);
                error_prepend(errp, "failed to set '%s': ",
                              BDRV_OPT_DISCARD);
                goto done;
            }
        }
    }

    drive_new(drive_opts, IF_NONE, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        error_prepend(errp, "failed to create drive: ");
        goto done;
    }

done:
    g_free(drive_optstr);
    g_free(format);
    g_free(file);
}

static void xen_qdisk_device_create(BusState *bus, const char *name,
                                    QDict *opts, Error **errp)
{
    unsigned long number;
    const char *vdev;
    BlockBackend *blk = NULL;
    IOThread *iothread = NULL;
    DeviceState *dev = NULL;
    Error *local_err = NULL;

    trace_xen_qdisk_device_create(name);

    if (qemu_strtoul(name, NULL, 10, &number)) {
        error_setg(errp, "failed to parse name '%s'", name);
        return;
    }

    vdev = qdict_get_try_str(opts, "dev");
    if (!vdev) {
        error_setg(errp, "no dev parameter");
        return;
    }

    xen_qdisk_drive_create(vdev, opts, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    blk = blk_by_name(vdev);
    g_assert(blk);

    iothread = iothread_create(vdev, &error_abort);

    dev = qdev_create(bus, TYPE_XEN_QDISK_DEVICE);

    qdev_prop_set_string(dev, "vdev", vdev);

    if (XEN_QDISK_DEVICE(dev)->vdev.number != number) {
        error_setg(errp, "invalid dev parameter '%s'", vdev);
        goto unref;
    }

    qdev_prop_set_drive(dev, "drive", blk, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        error_prepend(errp, "failed to set 'drive': ");
        goto unref;
    }

    XEN_QDISK_DEVICE(dev)->auto_iothread = iothread;

    qdev_init_nofail(dev);

    blockdev_mark_auto_del(blk);
    return;

unref:
    if (dev) {
        object_unparent(OBJECT(dev));
    }

    if (iothread) {
        iothread_destroy(iothread);
    }

    if (blk) {
        monitor_remove_blk(blk);
        blk_unref(blk);
    }
}

static const XenBackendInfo xen_qdisk_backend_info = {
    .type = "qdisk",
    .create = xen_qdisk_device_create,
};

static void xen_qdisk_register_backend(void)
{
    xen_backend_register(&xen_qdisk_backend_info);
}

xen_backend_init(xen_qdisk_register_backend);
