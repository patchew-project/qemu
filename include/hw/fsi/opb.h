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

uint8_t opb_read8(OPBus *opb, hwaddr addr);
uint16_t opb_read16(OPBus *opb, hwaddr addr);
uint32_t opb_read32(OPBus *opb, hwaddr addr);
void opb_write8(OPBus *opb, hwaddr addr, uint8_t data);
void opb_write16(OPBus *opb, hwaddr addr, uint16_t data);
void opb_write32(OPBus *opb, hwaddr addr, uint32_t data);

void opb_fsi_master_address(OPBus *opb, hwaddr addr);
void opb_opb2fsi_address(OPBus *opb, hwaddr addr);

#endif /* FSI_OPB_H */
