/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 IBM Corp.
 *
 * IBM Common FRU Access Macro
 */
#ifndef FSI_CFAM_H
#define FSI_CFAM_H

#include "qemu/units.h"
#include "system/memory.h"

#include "hw/fsi/fsi.h"
#include "hw/fsi/lbus.h"

/*
 * All CFAM flavors present a register slot holding a config table, an FSI
 * responder and a local bus carrying the engines the config table describes.
 * That is the common model; each flavor supplies its own table, slot layout
 * and engines.
 */
#define TYPE_FSI_CFAM_COMMON "cfam-common"
OBJECT_DECLARE_TYPE(FSICFAMCommonState, FSICFAMCommonClass, FSI_CFAM_COMMON)

#define TYPE_FSI_CFAM "cfam"
OBJECT_DECLARE_SIMPLE_TYPE(FSICFAMState, FSI_CFAM)

/* P9-ism */
#define CFAM_CONFIG_NR_REGS 0x28

#define FSI_CFAM_SLOT_SIZE   (2 * MiB)
#define FSI_CFAM_CONFIG_SIZE 0x400

#define ENGINE_CONFIG_NEXT            BIT(31)
#define ENGINE_CONFIG_TYPE_PEEK       (0x02 << 4)
#define ENGINE_CONFIG_TYPE_FSI        (0x03 << 4)
#define ENGINE_CONFIG_TYPE_SCRATCHPAD (0x06 << 4)
#define ENGINE_CONFIG_TYPE_MBOX_V1    (0x14 << 4)

/* Valid, slots, version, type, crc */
#define CFAM_CONFIG_REG(__VER, __TYPE, __CRC)   \
    (ENGINE_CONFIG_NEXT       |   \
     0x00010000               |   \
     (__VER)                  |   \
     (__TYPE)                 |   \
     (__CRC))

/* As above, for the last entry in a table: NEXT is clear */
#define CFAM_CONFIG_LAST(__VER, __TYPE, __CRC)  \
    (0x00010000               |   \
     (__VER)                  |   \
     (__TYPE)                 |   \
     (__CRC))

#define CFAM_CONFIG_CHIP_ID_MAJOR(__MAJOR) (((__MAJOR) & 0xf) << 8)

struct FSICFAMCommonState {
    /* < private > */
    FSISlaveState parent;

    /* CFAM config address space */
    MemoryRegion config_iomem;

    MemoryRegion mr;

    FSILBus lbus;
};

struct FSICFAMCommonClass {
    /* < private > */
    DeviceClass parent_class;

    /* < public > */
    /* Config table served by the common ops, one word per 4-byte offset */
    const uint32_t *config;
    unsigned config_nr;

    /* Layout of the register slot */
    hwaddr responder_offset;
    hwaddr lbus_offset;

    /* Realize and map this flavor's local bus engines */
    bool (*realize_engines)(FSICFAMCommonState *cfam, Error **errp);
};

struct FSICFAMState {
    /* < private > */
    FSICFAMCommonState parent;

    FSIScratchPad scratchpad;
};

bool fsi_cfam_add_engine(FSICFAMCommonState *cfam, DeviceState *engine,
                         hwaddr offset, Error **errp);

#endif /* FSI_CFAM_H */
