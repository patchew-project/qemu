/*
 * QEMU Macintosh floppy disk controller emulator (SWIM2)
 *
 * Copyright (c) 2025 Matt Jacobson <mhjacobson@me.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_BLOCK_SWIM2_H
#define HW_BLOCK_SWIM2_H

#include <stdbool.h>
#include <stdint.h>
#include "hw/core/qdev.h"
#include "hw/core/sysbus.h"
#include "hw/block/block.h"
#include "hw/block/sony_superdrive.h"
#include "qemu/fifo32.h"
#include "qom/object.h"

#define TYPE_SWIM2 "swim2"
#define TYPE_SWIM2_BUS "swim2-bus"
#define TYPE_SWIM2_DRIVE "swim2-drive"

OBJECT_DECLARE_SIMPLE_TYPE(SWIM2State, SWIM2)
OBJECT_DECLARE_SIMPLE_TYPE(SWIM2Drive, SWIM2_DRIVE)

#define SWIM2_MAX_FD 2

struct SWIM2Drive {
    DeviceState parent_obj;

    BlockConf conf;
    SonyDrive sony;
    int32_t unit;
};

struct SWIM2State {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    BusState *bus;

    SWIM2Drive *drives[SWIM2_MAX_FD];

    uint8_t parameter_data[4];
    uint8_t parameter_index;
    uint8_t phase_reg;
    uint8_t setup_reg;
    uint8_t mode_reg;
    uint8_t error_reg;
    bool did_handshake;
    bool wait_for_mark;

    Fifo32 fifo;
};

void swim2_set_sel(SWIM2State *ctrl, bool sel);

#endif /* HW_BLOCK_SWIM2_H */
