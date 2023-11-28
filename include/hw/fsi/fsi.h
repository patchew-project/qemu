/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 IBM Corp.
 *
 * IBM Flexible Service Interface
 */
#ifndef FSI_FSI_H
#define FSI_FSI_H

#include "hw/qdev-core.h"
#include "qemu/bitops.h"

/* Bitwise operations at the word level. */
#define BE_BIT(x)           BIT(31 - (x))
#define BE_GENMASK(hb, lb)  MAKE_64BIT_MASK((lb), ((hb) - (lb) + 1))

#define TYPE_FSI_BUS "fsi.bus"
OBJECT_DECLARE_SIMPLE_TYPE(FSIBus, FSI_BUS)

typedef struct FSIBus {
    BusState bus;
} FSIBus;

#endif
