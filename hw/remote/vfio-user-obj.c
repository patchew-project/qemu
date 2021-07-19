/*
 * QEMU vfio-user server object
 *
 * Copyright © 2021 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL-v2, version 2 or later.
 *
 * See the COPYING file in the top-level directory.
 *
 */

/**
 * Usage: add options:
 *     -machine x-remote
 *     -device <PCI-device>,id=<pci-dev-id>
 *     -object vfio-user,id=<id>,socket=<socket-path>,devid=<pci-dev-id>
 *
 * Note that vfio-user object must be used with x-remote machine only. This
 * server could only support PCI devices for now.
 *
 * socket is path to a file. This file will be created by the server. It is
 * a required option
 *
 * devid is the id of a PCI device on the server. It is also a required option.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"

#include <errno.h>

#include "qom/object.h"
#include "qom/object_interfaces.h"
#include "qemu/error-report.h"
#include "trace.h"
#include "sysemu/runstate.h"
#include "qemu/notify.h"
#include "qapi/error.h"
#include "sysemu/sysemu.h"
#include "hw/qdev-core.h"
#include "hw/pci/pci.h"

#include "libvfio-user/include/libvfio-user.h"

#define TYPE_VFU_OBJECT "vfio-user"
OBJECT_DECLARE_TYPE(VfuObject, VfuObjectClass, VFU_OBJECT)

struct VfuObjectClass {
    ObjectClass parent_class;

    unsigned int nr_devs;

    /* Maximum number of devices the server could support*/
    unsigned int max_devs;
};

struct VfuObject {
    /* private */
    Object parent;

    char *socket;
    char *devid;

    Notifier machine_done;

    vfu_ctx_t *vfu_ctx;

    PCIDevice *pci_dev;
};

static void vfu_object_set_socket(Object *obj, const char *str, Error **errp)
{
    VfuObject *o = VFU_OBJECT(obj);

    g_free(o->socket);

    o->socket = g_strdup(str);

    trace_vfu_prop("socket", str);
}

static void vfu_object_set_devid(Object *obj, const char *str, Error **errp)
{
    VfuObject *o = VFU_OBJECT(obj);

    g_free(o->devid);

    o->devid = g_strdup(str);

    trace_vfu_prop("devid", str);
}

static void vfu_object_machine_done(Notifier *notifier, void *data)
{
    VfuObject *o = container_of(notifier, VfuObject, machine_done);
    DeviceState *dev = NULL;
    int ret;

    o->vfu_ctx = vfu_create_ctx(VFU_TRANS_SOCK, o->socket, 0,
                                o, VFU_DEV_TYPE_PCI);
    if (o->vfu_ctx == NULL) {
        error_setg(&error_abort, "vfu: Failed to create context - %s",
                   strerror(errno));
        return;
    }

    dev = qdev_find_recursive(sysbus_get_default(), o->devid);
    if (dev == NULL) {
        error_setg(&error_abort, "vfu: Device %s not found", o->devid);
        return;
    }
    o->pci_dev = PCI_DEVICE(dev);

    ret = vfu_pci_init(o->vfu_ctx, VFU_PCI_TYPE_CONVENTIONAL,
                       PCI_HEADER_TYPE_NORMAL, 0);
    if (ret < 0) {
        error_setg(&error_abort,
                   "vfu: Failed to attach PCI device %s to context - %s",
                   o->devid, strerror(errno));
        return;
    }

    vfu_pci_set_id(o->vfu_ctx,
                   pci_get_word(o->pci_dev->config + PCI_VENDOR_ID),
                   pci_get_word(o->pci_dev->config + PCI_DEVICE_ID),
                   pci_get_word(o->pci_dev->config + PCI_SUBSYSTEM_VENDOR_ID),
                   pci_get_word(o->pci_dev->config + PCI_SUBSYSTEM_ID));
}

static void vfu_object_init(Object *obj)
{
    VfuObjectClass *k = VFU_OBJECT_GET_CLASS(obj);
    VfuObject *o = VFU_OBJECT(obj);

    /* Add test for remote machine and PCI device */

    if (k->nr_devs >= k->max_devs) {
        error_report("Reached maximum number of vfio-user devices: %u",
                     k->max_devs);
        return;
    }

    k->nr_devs++;

    o->machine_done.notify = vfu_object_machine_done;
    qemu_add_machine_init_done_notifier(&o->machine_done);
}

static void vfu_object_finalize(Object *obj)
{
    VfuObjectClass *k = VFU_OBJECT_GET_CLASS(obj);
    VfuObject *o = VFU_OBJECT(obj);

    k->nr_devs--;

    vfu_destroy_ctx(o->vfu_ctx);

    g_free(o->socket);
    g_free(o->devid);

    if (k->nr_devs == 0) {
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
    }
}

static void vfu_object_class_init(ObjectClass *klass, void *data)
{
    VfuObjectClass *k = VFU_OBJECT_CLASS(klass);

    /* Limiting maximum number of devices to 1 until IOMMU support is added */
    k->max_devs = 1;
    k->nr_devs = 0;

    object_class_property_add_str(klass, "socket", NULL,
                                  vfu_object_set_socket);
    object_class_property_add_str(klass, "devid", NULL,
                                  vfu_object_set_devid);
}

static const TypeInfo vfu_object_info = {
    .name = TYPE_VFU_OBJECT,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(VfuObject),
    .instance_init = vfu_object_init,
    .instance_finalize = vfu_object_finalize,
    .class_size = sizeof(VfuObjectClass),
    .class_init = vfu_object_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void vfu_register_types(void)
{
    type_register_static(&vfu_object_info);
}

type_init(vfu_register_types);
