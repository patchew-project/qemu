/*
 * QEMU MEN Chameleon Bus emulation
 *
 * Copyright (C) 2023 Johannes Thumshirn <jth@kernel.org>
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/mcb/mcb.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

ChameleonDeviceDescriptor *mcb_new_chameleon_descriptor(MCBus *bus, uint8_t id,
                                                        uint8_t rev,
                                                        uint8_t var,
                                                        uint32_t size)
{
    BusChild *kid;
    ChameleonDeviceDescriptor *gdd;
    uint32_t reg1 = 0;
    uint32_t offset = 0x200;
    uint32_t end = 0;

    gdd =  g_new0(ChameleonDeviceDescriptor, 1);
    if (!gdd) {
        return NULL;
    }

    reg1 |= GDD_DEV(id);
    reg1 |= GDD_DTY(CHAMELEON_DTYPE_GENERAL);
    reg1 |= GDD_REV(rev);
    reg1 |= GDD_VAR(var);
    gdd->reg1 = cpu_to_le32(reg1);

    QTAILQ_FOREACH(kid, &BUS(bus)->children, sibling) {
        DeviceState *qdev = kid->child;
        MCBDevice *mdev = MCB_DEVICE(qdev);

        if (mdev->gdd) {
            offset = mdev->gdd->offset;
            end = offset + mdev->gdd->size;
        }
    }

    gdd->offset = offset + end;
    gdd->size = size;

    return gdd;
}

static void mcb_irq_handler(void *opaque, int irq_num, int level)
{
    MCBDevice *dev = opaque;
    MCBus *bus = MCB_BUS(qdev_get_parent_bus(DEVICE(dev)));

    if (bus->set_irq) {
        bus->set_irq(dev, irq_num, level);
    }
}

qemu_irq mcb_allocate_irq(MCBDevice *dev)
{
    int irq = 0;
    return qemu_allocate_irq(mcb_irq_handler, dev, irq);
}

MCBDevice *mcb_device_find(MCBus *bus, hwaddr addr)
{
    BusChild *kid;
    uint32_t start;
    uint32_t end;

    QTAILQ_FOREACH(kid, &BUS(bus)->children, sibling) {
        DeviceState *qdev = kid->child;
        MCBDevice *mdev = MCB_DEVICE(qdev);

        start = mdev->gdd->offset;
        end = start + mdev->gdd->size;

        if (addr >= start && addr <= end) {
            return mdev;
        }
    }
    return NULL;
}

void mcb_bus_init(MCBus *bus, size_t bus_size,
                  DeviceState *parent,
                  uint8_t n_slots,
                  qemu_irq_handler handler)
{
    qbus_init(bus, bus_size, TYPE_MCB_BUS, parent, NULL);
    bus->n_slots = n_slots;
    bus->set_irq = handler;
}

static void mcb_device_realize(DeviceState *dev, Error **errp)
{
    MCBDevice *mdev = MCB_DEVICE(dev);
    MCBus *bus = MCB_BUS(qdev_get_parent_bus(dev));
    MCBDeviceClass *k = MCB_DEVICE_GET_CLASS(dev);

    if (mdev->slot < 0) {
        mdev->slot = bus->free_slot;
    }

    if (mdev->slot >= bus->n_slots) {
        error_setg(errp, "Only %" PRIu8 " slots available.", bus->n_slots);
        return;
    }
    bus->free_slot = mdev->slot + 1;

    mdev->irq = qemu_allocate_irqs(bus->set_irq, mdev, 1);

    k->realize(dev, errp);
}

static void mcb_device_unrealize(DeviceState *dev)
{
    MCBDevice *mdev = MCB_DEVICE(dev);
    MCBDeviceClass *k = MCB_DEVICE_GET_CLASS(dev);

    if (k->unrealize) {
        k->unrealize(dev);
        return;
    }

    qemu_free_irqs(mdev->irq, 1);
}

static Property mcb_device_props[] = {
    DEFINE_PROP_INT32("slot", MCBDevice, slot, -1),
    DEFINE_PROP_END_OF_LIST()
};

static void mcb_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_INPUT, k->categories);
    k->bus_type = TYPE_MCB_BUS;
    k->realize = mcb_device_realize;
    k->unrealize = mcb_device_unrealize;
    device_class_set_props(k, mcb_device_props);
}

const VMStateDescription vmstate_mcb_device = {
    .name = "mcb_device",
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(slot, MCBDevice),
        VMSTATE_END_OF_LIST()
    }
};

static const TypeInfo mcb_device_info = {
    .name = TYPE_MCB_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(MCBDevice),
    .class_size = sizeof(MCBDeviceClass),
    .class_init = mcb_device_class_init,
    .abstract = true,
};

static const TypeInfo mcb_bus_info = {
    .name = TYPE_MCB_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(MCBus),
};

static void mcb_register_types(void)
{
    type_register_static(&mcb_device_info);
    type_register_static(&mcb_bus_info);
}

type_init(mcb_register_types);
