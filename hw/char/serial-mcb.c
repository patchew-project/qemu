/*
 * QEMU MEN 16z125 UART over MCB emulation
 *
 * Copyright (C) 2016 Johannes Thumshirn <jthumshirn@suse.de>
 *
 * This code is licensed under the GNU GPL v2 or (at your opinion) any
 * later version
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/char/serial.h"
#include "hw/mcb/mcb.h"

typedef struct {
    MCBDevice dev;
    SerialState state;
} MCBSerialState;

static void serial_mcb_realize(DeviceState *dev, Error **errp)
{
    MCBDevice *mdev = MCB_DEVICE(dev);
    MCBSerialState *mss = DO_UPCAST(MCBSerialState, dev, mdev);
    MCBus *bus = MCB_BUS(qdev_get_parent_bus(DEVICE(dev)));
    SerialState *s = &mss->state;
    Error *err = 0;

    mdev->gdd = mcb_new_chameleon_descriptor(bus, 125, mdev->rev,
                                             mdev->var, 0x10);
    if (!mdev->gdd) {
        return;
    }

    s->baudbase = 115200;
    serial_realize_core(s, &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }

    s->irq = mcb_allocate_irq(&mss->dev);
    memory_region_init_io(&s->io, OBJECT(mss), &serial_io_ops, s, "serial", 8);
    memory_region_add_subregion(&bus->mmio_region, mdev->gdd->offset, &s->io);
}

static void serial_mcb_unrealize(DeviceState *dev, Error **errp)
{
    MCBDevice *mdev = MCB_DEVICE(dev);

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
    DEFINE_PROP_CHR("chardev", MCBSerialState, state.chr),
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
    dc->props = serial_mcb_properties;
}

static const TypeInfo serial_mcb_info = {
    .name = "mcb-serial",
    .parent = TYPE_MCB_DEVICE,
    .instance_size = sizeof(MCBSerialState),
    .class_init = serial_mcb_class_initfn,
};

static void serial_mcb_register_types(void)
{
    type_register_static(&serial_mcb_info);
}

type_init(serial_mcb_register_types);
