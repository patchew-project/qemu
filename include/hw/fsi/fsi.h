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
OBJECT_DECLARE_SIMPLE_TYPE(FSIBus, FSI_BUS)

/* TODO: Figure out what's best with a point-to-point bus */
typedef struct FSISlaveState FSISlaveState;

typedef struct FSIBus {
    BusState bus;

    /* XXX: It's point-to-point, just instantiate the slave directly for now */
    CFAMState slave;
} FSIBus;

#endif
