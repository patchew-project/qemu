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

    uint64_t first_pc;

    hwaddr baseaddr;
    hwaddr dest_baseaddr;
    hwaddr ramsink_ramstart;
    hwaddr ramsink_ramlimit;
    uint32_t regs[TRACE_R_MAX];
    RegisterInfo regs_info[TRACE_R_MAX];

    bool updiscon_pending;

    bool enabled;
    bool trace_running;
    bool trace_next_insn;
};

#define TYPE_TRACE_ENCODER "trace-encoder"

OBJECT_DECLARE_SIMPLE_TYPE(TraceEncoder, TRACE_ENCODER)

void trencoder_set_first_trace_insn(Object *trencoder_obj, uint64_t pc);
void trencoder_trace_trap_insn(Object *trencoder_obj,
                               uint64_t pc, uint32_t ecause,
                               bool is_interrupt,
                               uint64_t tval);
void trencoder_trace_ppccd(Object *trencoder_obj, uint64_t pc);
void trencoder_report_updiscon(Object *trencoder_obj);

#endif
