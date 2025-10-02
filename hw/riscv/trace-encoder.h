/*
 * Emulation of a RISC-V Trace Encoder
 *
 * Copyright (C) 2025 Ventana Micro Systems Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef RISCV_TRACE_ENCODER_H
#define RISCV_TRACE_ENCODER_H

#include "hw/sysbus.h"
#include "hw/register.h"
#include "system/dma.h"
#include "qom/object.h"
#include "cpu.h"

#define TRACE_R_MAX (0xFFF / 4)

struct TraceEncoder {
    /*< private >*/
    SysBusDevice parent_obj;

    RISCVCPU *cpu;
    int cpu_id;

    MemoryRegion reg_mem;
    uint32_t reg_mem_size;

    hwaddr baseaddr;
    hwaddr dest_baseaddr;
    hwaddr ramsink_ramstart;
    hwaddr ramsink_ramlimit;
    uint32_t regs[TRACE_R_MAX];
    RegisterInfo regs_info[TRACE_R_MAX];
};

#define TYPE_TRACE_ENCODER "trace-encoder"

OBJECT_DECLARE_SIMPLE_TYPE(TraceEncoder, TRACE_ENCODER)

#endif
