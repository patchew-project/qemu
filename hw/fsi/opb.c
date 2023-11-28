/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 IBM Corp.
 *
 * IBM On-chip Peripheral Bus
 */

#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qemu/log.h"

#include "hw/fsi/opb.h"

static void fsi_opb_init(Object *o)
{
    OPBus *opb = OP_BUS(o);

    memory_region_init_io(&opb->mr, OBJECT(opb), NULL, opb,
                          NULL, UINT32_MAX);
    address_space_init(&opb->as, &opb->mr, "opb");
}

static const TypeInfo opb_info = {
    .name = TYPE_OP_BUS,
    .parent = TYPE_BUS,
    .instance_init = fsi_opb_init,
    .instance_size = sizeof(OPBus),
};

static void fsi_opb_register_types(void)
{
    type_register_static(&opb_info);
}

type_init(fsi_opb_register_types);
