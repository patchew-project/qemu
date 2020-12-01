/*
 * Copyright © 2020 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL-v2, version 2 or later.
 *
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu-common.h"

#include "hw/remote/remote-obj.h"
#include "qemu/error-report.h"
#include "qom/object_interfaces.h"
#include "hw/qdev-core.h"
#include "io/channel.h"
#include "hw/qdev-core.h"
#include "hw/remote/machine.h"
#include "io/channel-util.h"
#include "qapi/error.h"
#include "sysemu/sysemu.h"
#include "hw/pci/pci.h"

static void remote_object_set_fd(Object *obj, const char *str, Error **errp)
{
    RemoteObject *o = REMOTE_OBJECT(obj);

    o->fd = atoi(str);
}

static void remote_object_set_devid(Object *obj, const char *str, Error **errp)
{
    RemoteObject *o = REMOTE_OBJECT(obj);

    g_free(o->devid);

    o->devid = g_strdup(str);
}

static void property_release_remote_object(Object *obj, const char *name,
                                           void *opaque)
{
    Object *remote_object = OBJECT(opaque);

    object_unref(remote_object);
}

static void remote_object_machine_done(Notifier *notifier, void *data)
{
    RemoteObject *o = container_of(notifier, RemoteObject, machine_done);
    DeviceState *dev = NULL;
    QIOChannel *ioc = NULL;
    Coroutine *co = NULL;
    RemoteCommDev *comdev = NULL;
    Error *err = NULL;

    dev = qdev_find_recursive(sysbus_get_default(), o->devid);
    if (!dev || !object_dynamic_cast(OBJECT(dev), TYPE_PCI_DEVICE)) {
        error_report("%s is not a PCI device", o->devid);
        return;
    }

    ioc = qio_channel_new_fd(o->fd, &err);
    if (!ioc) {
        error_report_err(err);
        return;
    }
    qio_channel_set_blocking(ioc, false, NULL);

    object_property_add(OBJECT(dev), "remote-object", "object", NULL, NULL,
                        property_release_remote_object, (void *)OBJECT(o));
    /* co-routine should free this. */
    comdev = g_new0(RemoteCommDev, 1);
    *comdev = (RemoteCommDev) {
        .ioc = ioc,
        .dev = PCI_DEVICE(dev),
    };

    co = qemu_coroutine_create(mpqemu_remote_msg_loop_co, comdev);
    qemu_coroutine_enter(co);
}

static void remote_object_init(Object *obj)
{
    RemoteObjectClass *k = REMOTE_OBJECT_GET_CLASS(obj);
    RemoteObject *o = REMOTE_OBJECT(obj);

    if (k->nr_devs >= k->max_devs) {
        error_report("Reached maximum number of devices: %u", k->max_devs);
        return;
    }

    o->ioc = NULL;
    o->fd = -1;
    o->devid = NULL;

    k->nr_devs++;

    object_property_add_str(obj, "fd", NULL, remote_object_set_fd);
    object_property_set_description(obj, "fd",
                                    "file descriptor for the object");
    object_property_add_str(obj, "devid", NULL, remote_object_set_devid);
    object_property_set_description(obj, "devid",
                                    "id of device to associate");

    o->machine_done.notify = remote_object_machine_done;
    qemu_add_machine_init_done_notifier(&o->machine_done);
}

static void remote_object_finalize(Object *obj)
{
    RemoteObjectClass *k = REMOTE_OBJECT_GET_CLASS(obj);
    RemoteObject *o = REMOTE_OBJECT(obj);

    if (o->ioc) {
        qio_channel_shutdown(o->ioc, QIO_CHANNEL_SHUTDOWN_BOTH, NULL);
        qio_channel_close(o->ioc, NULL);
    }

    object_unref(OBJECT(o->ioc));

    k->nr_devs--;
    g_free(o->devid);
}

static void remote_object_class_init(ObjectClass *klass, void *data)
{
    RemoteObjectClass *k = REMOTE_OBJECT_CLASS(klass);

    k->max_devs = 1;
    k->nr_devs = 0;
}

static const TypeInfo remote_object_info = {
    .name = TYPE_REMOTE_OBJECT,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(RemoteObject),
    .instance_init = remote_object_init,
    .instance_finalize = remote_object_finalize,
    .class_size = sizeof(RemoteObjectClass),
    .class_init = remote_object_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void register_types(void)
{
    type_register_static(&remote_object_info);
}

type_init(register_types);
