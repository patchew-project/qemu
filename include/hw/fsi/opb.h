/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 IBM Corp.
 *
 * IBM On-Chip Peripheral Bus
 */
#ifndef FSI_OPB_H
#define FSI_OPB_H

#include "exec/memory.h"
#include "hw/fsi/fsi-master.h"

#define TYPE_OP_BUS "opb"
OBJECT_DECLARE_SIMPLE_TYPE(OPBus, OP_BUS)

typedef struct OPBus {
        /*< private >*/
        BusState bus;

        /*< public >*/
        MemoryRegion mr;
        AddressSpace as;

        /* Model OPB as dumb enough just to provide an address-space */
        /* TODO: Maybe don't store device state in the bus? */
        FSIMasterState fsi;
} OPBus;

typedef struct OPBusClass {
        BusClass parent_class;
} OPBusClass;

#endif /* FSI_OPB_H */
