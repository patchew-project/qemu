/*
 * Target-specific parts of semihosting/arm-compat-semi.c.
 *
 * Copyright (c) 2005, 2007 CodeSourcery.
 * Copyright (c) 2019, 2022 Linaro
 * Copyright © 2020 by Keith Packard <keithp@keithp.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "semihosting/common-semi.h"

static uint64_t riscv_common_semi_arg(CPUState *cs, int argno)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    return env->gpr[xA0 + argno];
}

static void riscv_common_semi_set_ret(CPUState *cs, uint64_t ret)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    env->gpr[xA0] = ret;
}

static bool riscv_is_64bit_semihosting(CPUArchState *env)
{
    return riscv_cpu_mxl(env) != MXL_RV32;
}

static bool riscv_common_semi_sys_exit_is_extended(CPUState *cs)
{
    return riscv_is_64bit_semihosting(cpu_env(cs));
}

static uint64_t riscv_common_semi_stack_bottom(CPUState *cs)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    return env->gpr[xSP];
}

static bool riscv_common_semi_has_synccache(CPUArchState *env)
{
    return true;
}

static const CPUSemihostingOps riscv_semihosting_ops = {
    .arg = riscv_common_semi_arg,
    .set_ret = riscv_common_semi_set_ret,
    .is_64bit = riscv_is_64bit_semihosting,
    .sys_exit_is_extended = riscv_common_semi_sys_exit_is_extended,
    .stack_bottom = riscv_common_semi_stack_bottom,
    .has_synccache = riscv_common_semi_has_synccache,
};

TARGET_INFO_CPU_OP(CPU_RESOLVING_TYPE, semihosting, riscv_semihosting_ops);
