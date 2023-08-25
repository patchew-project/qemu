/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 IBM Corp.
 *
 * IBM Flexible Service Interface
 */
#include "qemu/osdep.h"

#include "qapi/error.h"

#include "hw/fsi/fsi.h"
#include "hw/fsi/cfam.h"

static void fsi_bus_realize(BusState *bus, Error **errp)
{
    FSIBus *s = FSI_BUS(bus);
    Error *err = NULL;

    /* FIXME: Should be realised elsewhere and added to the bus */
    object_property_set_bool(OBJECT(&s->slave), "realized", true, &err);
    if (err) {
        error_propagate(errp, err);
    }
}

static void fsi_bus_init(Object *o)
{
    FSIBus *s = FSI_BUS(o);

    /* FIXME: Move this elsewhere */
    object_initialize_child(o, TYPE_CFAM, &s->slave, TYPE_CFAM);
    qdev_set_parent_bus(DEVICE(&s->slave), BUS(o), &error_abort);
}

static void fsi_bus_class_init(ObjectClass *klass, void *data)
{
    BusClass *bc = BUS_CLASS(klass);
    bc->realize = fsi_bus_realize;
}

static const TypeInfo fsi_bus_info = {
    .name = TYPE_FSI_BUS,
    .parent = TYPE_BUS,
    .instance_init = fsi_bus_init,
    .instance_size = sizeof(FSIBus),
    .class_init = fsi_bus_class_init,
};

static void fsi_bus_register_types(void)
{
    type_register_static(&fsi_bus_info);
}

type_init(fsi_bus_register_types);
