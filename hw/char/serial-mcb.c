/*
 * QEMU MEN 16z125 UART over MCB emulation
 *
 * Copyright (C) 2023 Johannes Thumshirn <jth@kernel.org>
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/char/serial.h"
#include "hw/mcb/mcb.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "migration/vmstate.h"

struct MCBSerialState {
    MCBDevice dev;
    SerialState state;
};

#define TYPE_MCB_SERIAL "mcb-serial"
OBJECT_DECLARE_SIMPLE_TYPE(MCBSerialState, MCB_SERIAL)

static void serial_mcb_realize(DeviceState *dev, Error **errp)
{
    MCBDevice *mdev = MCB_DEVICE(dev);
    MCBSerialState *mss = DO_UPCAST(MCBSerialState, dev, mdev);
    MCBus *bus = MCB_BUS(qdev_get_parent_bus(DEVICE(dev)));
    SerialState *s = &mss->state;

    mdev->gdd = mcb_new_chameleon_descriptor(bus, 125, mdev->rev,
                                             mdev->var, 0x10);
    if (!mdev->gdd) {
        return;
    }

    s->baudbase = 115200;
    if (!qdev_realize(DEVICE(s), NULL, errp)) {
        return;
    }

    s->irq = mcb_allocate_irq(&mss->dev);
    memory_region_init_io(&s->io, OBJECT(mss), &serial_io_ops, s, "serial", 8);
    memory_region_add_subregion(&bus->mmio_region, mdev->gdd->offset, &s->io);
}

static void serial_mcb_unrealize(DeviceState *dev)
{
    MCBDevice *mdev = MCB_DEVICE(dev);
    MCBSerialState *mss = DO_UPCAST(MCBSerialState, dev, mdev);
    SerialState *s = &mss->state;

    qdev_unrealize(DEVICE(s));
    qemu_free_irq(s->irq);
    g_free(&mdev->gdd);
}

static const VMStateDescription vmstate_mcb_serial = {
    .name = "mcb-serial",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_MCB_DEVICE(dev, MCBSerialState),
        VMSTATE_STRUCT(state, MCBSerialState, 0, vmstate_serial, SerialState),
        VMSTATE_END_OF_LIST()
    }
};

static Property serial_mcb_properties[] = {
    DEFINE_PROP_UINT8("rev", MCBSerialState, dev.rev, 0),
    DEFINE_PROP_UINT8("var", MCBSerialState, dev.var, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void serial_mcb_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    MCBDeviceClass *mc = MCB_DEVICE_CLASS(klass);

    mc->realize = serial_mcb_realize;
    mc->unrealize = serial_mcb_unrealize;

    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
    dc->desc = "MEN 16z125 UART over MCB";
    dc->vmsd = &vmstate_mcb_serial;
    device_class_set_props(dc, serial_mcb_properties);
}

static void serial_mcb_init(Object *o)
{
    MCBSerialState *mss = MCB_SERIAL(o);

    object_initialize_child(o, "serial", &mss->state, TYPE_SERIAL);

    qdev_alias_all_properties(DEVICE(&mss->state), o);
}

static const TypeInfo serial_mcb_info = {
    .name = "mcb-serial",
    .parent = TYPE_MCB_DEVICE,
    .instance_size = sizeof(MCBSerialState),
    .instance_init = serial_mcb_init,
    .class_init = serial_mcb_class_initfn,
};

static void serial_mcb_register_types(void)
{
    type_register_static(&serial_mcb_info);
}

type_init(serial_mcb_register_types);
