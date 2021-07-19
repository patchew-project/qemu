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

#include "qom/object.h"
#include "qom/object_interfaces.h"
#include "qemu/error-report.h"
#include "trace.h"
#include "sysemu/runstate.h"

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

static void vfu_object_init(Object *obj)
{
    VfuObjectClass *k = VFU_OBJECT_GET_CLASS(obj);

    /* Add test for remote machine and PCI device */

    if (k->nr_devs >= k->max_devs) {
        error_report("Reached maximum number of vfio-user devices: %u",
                     k->max_devs);
        return;
    }

    k->nr_devs++;
}

static void vfu_object_finalize(Object *obj)
{
    VfuObjectClass *k = VFU_OBJECT_GET_CLASS(obj);
    VfuObject *o = VFU_OBJECT(obj);

    k->nr_devs--;

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
