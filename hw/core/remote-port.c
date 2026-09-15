/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * QEMU remote attach
 *
 * Copyright (c) 2013 Xilinx Inc
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
 *
 * This code is licensed under the GNU GPL.
 */

#include "qemu/osdep.h"
#include "system/system.h"
#include "chardev/char.h"
#include "system/cpus.h"
#include "system/cpu-timers.h"
#include "system/reset.h"
#include "hw/core/sysbus.h"
#include "hw/core/hw-error.h"
#include "qemu/thread.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "migration/vmstate.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "qemu/cutils.h"

#include "hw/core/remote-port-proto.h"
#include "hw/core/remote-port.h"

#define REMOTE_PORT_CLASS(klass)    \
     OBJECT_CLASS_CHECK(RemotePortClass, (klass), TYPE_REMOTE_PORT)

static void rp_reset(DeviceState *dev)
{
    RemotePort *s = REMOTE_PORT(dev);

    if (s->reset_done) {
        return;
    }

    s->reset_done = true;
}

static void rp_realize(DeviceState *dev, Error **errp)
{
    RemotePort *s = REMOTE_PORT(dev);
    Chardev *chr = NULL;

    s->prefix = object_get_canonical_path(OBJECT(dev));

    if (s->chrdev_id) {
        chr = qemu_chr_find(s->chrdev_id);
    }

    if (!chr) {
        error_setg(errp, "Unable to connect to remote-port channel");
        return;
    }

    if (!qemu_chr_fe_init(&s->chr, chr, errp)) {
        return;
    }

    s->chrdev = chr;


}

static void rp_unrealize(DeviceState *dev)
{
    RemotePort *s = REMOTE_PORT(dev);

    s->finalizing = true;

    info_report("%s: Wait for remote-port to disconnect", s->prefix);
    qemu_chr_fe_disconnect(&s->chr);
    qemu_chr_fe_deinit(&s->chr, false);

    object_unparent(OBJECT(s->chrdev));
}

static const VMStateDescription vmstate_rp = {
    .name = TYPE_REMOTE_PORT,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST(),
    }
};

static Property rp_properties[] = {
    DEFINE_PROP_STRING("chrdev-id", RemotePort, chrdev_id),
};

static void rp_prop_allow_set_link(const Object *obj, const char *name,
                                   Object *val, Error **errp)
{
}

static void rp_init(Object *obj)
{
    RemotePort *s = REMOTE_PORT(obj);
    int i;

    for (i = 0; i < REMOTE_PORT_MAX_DEVS; ++i) {
        char *name = g_strdup_printf("remote-port-dev%d", i);
        object_property_add_link(obj, name, TYPE_REMOTE_PORT_DEVICE,
                             (Object **)&s->devs[i],
                             rp_prop_allow_set_link,
                             OBJ_PROP_LINK_STRONG);
        g_free(name);
    }
}

static void rp_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->legacy_reset = rp_reset;
    dc->realize = rp_realize;
    dc->unrealize = rp_unrealize;
    dc->vmsd = &vmstate_rp;
    device_class_set_props_n(dc, rp_properties, ARRAY_SIZE(rp_properties));
}

static const TypeInfo rp_info = {
    .name          = TYPE_REMOTE_PORT,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(RemotePort),
    .instance_init = rp_init,
    .class_init    = rp_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { },
    },
};

static const TypeInfo rp_device_info = {
    .name          = TYPE_REMOTE_PORT_DEVICE,
    .parent        = TYPE_INTERFACE,
    .class_size    = sizeof(RemotePortDeviceClass),
};

static void rp_register_types(void)
{
    type_register_static(&rp_info);
    type_register_static(&rp_device_info);
}

type_init(rp_register_types)
