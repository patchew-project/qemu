/*
 * QEMU Macintosh floppy disk controller emulator (SWIM2)
 *
 * Copyright (c) 2025 Matt Jacobson <mhjacobson@me.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#ifndef HW_BLOCK_SWIM2_H
#define HW_BLOCK_SWIM2_H

#include <stdbool.h>
#include <stdint.h>
#include "hw/qdev-core.h"
#include "hw/sysbus.h"
#include "hw/block/block.h"
#include "hw/block/sony_superdrive.h"
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

typedef struct {
    uint8_t data;
    bool is_mark;
} SWIM2FIFOEntry;

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

    SWIM2FIFOEntry fifo[2];
    uint8_t fifo_head;
    uint8_t fifo_tail;
    uint8_t fifo_count;
};

#endif /* HW_BLOCK_SWIM2_H */
