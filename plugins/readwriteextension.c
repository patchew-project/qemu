/**
 * QEMU Plugin read write extension code
 *
 * This is the code that allows the plugin to read and write
 * memory and registers and flush the tb cache. Also allows
 * to set QEMU into singlestep mode from Plugin.
 *
 * Based on plugin interface:
 * Copyright (C) 2017, Emilio G. Cota <cota@braap.org>
 * Copyright (C) 2019, Linaro
 *
 * Copyright (C) 2021 Florian Hauschild <florian.hauschild@tum.de>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */



#include "qemu/osdep.h"
#include "qemu/plugin.h"
#include "hw/core/cpu.h"
#include "cpu.h"
#include "exec/exec-all.h"

void plugin_async_flush_tb(CPUState *cpu, run_on_cpu_data arg);
void plugin_async_flush_tb(CPUState *cpu, run_on_cpu_data arg)
{
    g_assert(cpu_in_exclusive_context(cpu));
    tb_flush(cpu);
}



int plugin_rw_memory_cpu(uint64_t address, uint8_t buffer[], size_t buf_size, char write)
{
    return cpu_memory_rw_debug(current_cpu, address, buffer, buf_size, write);

}


void plugin_flush_tb(void)
{
    async_safe_run_on_cpu(current_cpu, plugin_async_flush_tb, RUN_ON_CPU_NULL);
}

static int plugin_read_register(CPUState *cpu, GByteArray *buf, int reg)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);
    if (reg < cc->gdb_num_core_regs) {
        return cc->gdb_read_register(cpu, buf, reg);
    }
    return 0;
}

uint64_t read_reg(int reg)
{
    GByteArray *val = g_byte_array_new();
    uint64_t reg_ret = 0;
    int ret_bytes = plugin_read_register(current_cpu, val, reg);
    if (ret_bytes == 1) {
        reg_ret = val->data[0];
    }
    if (ret_bytes == 2) {
        reg_ret = *(uint16_t *) &(val->data[0]);
    }
    if (ret_bytes == 4) {
        reg_ret = *(uint32_t *) &(val->data[0]);
    }
    if (ret_bytes == 8) {
        reg_ret = *(uint64_t *) &(val->data[0]);
    }
    return reg_ret;
}

void write_reg(int reg, uint64_t val)
{
    CPUState *cpu = current_cpu;
    CPUClass *cc = CPU_GET_CLASS(cpu);

    if (reg < cc->gdb_num_core_regs) {
        cc->gdb_write_register(cpu, (uint8_t *) &val, reg);
    }
}

void plugin_single_step(int enable)
{
    /* singlestep is set in softmmu/vl.c*/
    static int orig_value;
    static int executed = 1;

    if (unlikely(executed == 1)) {
        orig_value = singlestep;
        executed = 2;
    }

    if (enable == 1) {
        singlestep = 1;
    } else {
        singlestep = orig_value;
    }

    tb_flush(current_cpu);
}
