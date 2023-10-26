/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 IBM Corp.
 *
 * IBM Flexible Service Interface
 */
#ifndef FSI_FSI_H
#define FSI_FSI_H

#include "qemu/bitops.h"
#include "hw/qdev-core.h"

/*
 * TODO: Maybe unwind this dependency with const links? Store a
 * pointer in FSIBus?
 */
#include "hw/fsi/cfam.h"

/* Bitwise operations at the word level. */
#define BE_BIT(x)           BIT(31 - (x))
#define BE_GENMASK(hb, lb)  MAKE_64BIT_MASK((lb), ((hb) - (lb) + 1))

#define TYPE_FSI_BUS "fsi.bus"
OBJECT_DECLARE_SIMPLE_TYPE(FSIBus, FSI_BUS)

/* TODO: Figure out what's best with a point-to-point bus */
typedef struct FSISlaveState FSISlaveState;

typedef struct FSIBus {
    BusState bus;

    /* XXX: It's point-to-point, just instantiate the slave directly for now */
    FSICFAMState slave;
} FSIBus;

#endif
