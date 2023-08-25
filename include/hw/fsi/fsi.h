/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 IBM Corp.
 *
 * IBM Flexible Service Interface
 */
#ifndef FSI_FSI_H
#define FSI_FSI_H

#include "hw/qdev-core.h"

/*
 * TODO: Maybe unwind this dependency with const links? Store a
 * pointer in FSIBus?
 */
#include "hw/fsi/cfam.h"

#define TYPE_FSI_BUS "fsi.bus"
#define FSI_BUS(obj) OBJECT_CHECK(FSIBus, (obj), TYPE_FSI_BUS)
#define FSI_BUS_CLASS(klass) \
    OBJECT_CLASS_CHECK(FSIBusClass, (klass), TYPE_FSI_BUS)
#define FSI_BUS_GET_CLASS(obj) \
    OBJECT_GET_CLASS(FSIBusClass, (obj), TYPE_FSI_BUS)

/* TODO: Figure out what's best with a point-to-point bus */
typedef struct FSISlaveState FSISlaveState;

typedef struct FSIBus {
    BusState bus;

    /* XXX: It's point-to-point, just instantiate the slave directly for now */
    CFAMState slave;
} FSIBus;

#endif
