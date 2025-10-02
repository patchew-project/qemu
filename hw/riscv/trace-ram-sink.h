/*
 * Emulation of a RISC-V Trace RAM Sink
 *
 * Copyright (C) 2025 Ventana Micro Systems Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef RISCV_TRACE_RAM_SINK_H
#define RISCV_TRACE_RAM_SINK_H

#include "hw/register.h"
#include "hw/sysbus.h"
#include "system/dma.h"
#include "qom/object.h"

#define TRACE_R_MAX (0xFFF / 4)
#define TR_DEV_REGMAP_SIZE 0x1000

/*
 * The Trace Encoder will read/write those regs so put their
 * declaration in this header.
 */
REG32(TR_RAM_CONTROL, 0x0)
    FIELD(TR_RAM_CONTROL, ACTIVE, 0, 1)
    FIELD(TR_RAM_CONTROL, ENABLE, 1, 1)
    FIELD(TR_RAM_CONTROL, EMPTY, 3, 1)
    FIELD(TR_RAM_CONTROL, MODE, 4, 1)
    FIELD(TR_RAM_CONTROL, STOP_ON_WRAP, 8, 1)
    FIELD(TR_RAM_CONTROL, MEM_FORMAT, 9, 2)
    /* reserved bits */
    FIELD(TR_RAM_CONTROL, RSVP1, 2, 1)
    FIELD(TR_RAM_CONTROL, RSVP2, 5, 2)
    FIELD(TR_RAM_CONTROL, RSVP3, 11, 1)
    FIELD(TR_RAM_CONTROL, RSVP4, 15, 16)

REG32(TR_RAM_IMPL, 0x4)
    FIELD(TR_RAM_IMPL, VER_MAJOR, 0, 4)
    FIELD(TR_RAM_IMPL, VER_MINOR, 4, 4)
    FIELD(TR_RAM_IMPL, COMP_TYPE, 8, 4)
    FIELD(TR_RAM_IMPL, HAS_SRAM, 12, 1)
    FIELD(TR_RAM_IMPL, HAS_SMEM, 13, 1)
    /* reserved bits */
    FIELD(TR_RAM_IMPL, RSVP1, 14, 18)

REG32(TR_RAM_START_LOW, 0x010)
    FIELD(TR_RAM_START_LOW, ADDR, 2, 30)
REG32(TR_RAM_START_HIGH, 0x014)
    FIELD(TR_RAM_START_HIGH, ADDR, 0, 32)

REG32(TR_RAM_LIMIT_LOW, 0x018)
    FIELD(TR_RAM_LIMIT_LOW, ADDR, 2, 30)
REG32(TR_RAM_LIMIT_HIGH, 0x01C)
    FIELD(TR_RAM_LIMIT_HIGH, ADDR, 0, 32)

REG32(TR_RAM_WP_LOW, 0x020)
    FIELD(TR_RAM_WP_LOW, WRAP, 0, 1)
    FIELD(TR_RAM_WP_LOW, ADDR, 2, 30)
REG32(TR_RAM_WP_HIGH, 0x024)
    FIELD(TR_RAM_WP_HIGH, ADDR, 0, 32)

struct TraceRamSink {
    /*< private >*/
    SysBusDevice parent_obj;

    MemoryRegion reg_mem;
    uint32_t reg_mem_size;

    hwaddr baseaddr;
    uint32_t regs[TRACE_R_MAX];
    RegisterInfo regs_info[TRACE_R_MAX];

    hwaddr smemaddr;
    MemoryRegion smem;
    uint32_t smemsize;
    uint8_t *msgs;
};

#define TYPE_TRACE_RAM_SINK "trace-ram-sink"

OBJECT_DECLARE_SIMPLE_TYPE(TraceRamSink, TRACE_RAM_SINK)

#endif
