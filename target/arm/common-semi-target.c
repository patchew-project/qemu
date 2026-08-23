/*
 * Target-specific parts of semihosting/arm-compat-semi.c.
 *
 * Copyright (c) 2005, 2007 CodeSourcery.
 * Copyright (c) 2019, 2022 Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "semihosting/common-semi.h"
#include "target/arm/cpu-qom.h"

static uint64_t arm_common_semi_arg(CPUState *cs, int argno)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    if (is_a64(env)) {
        return env->xregs[argno];
    } else {
        return env->regs[argno];
    }
}

static void arm_common_semi_set_ret(CPUState *cs, uint64_t ret)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    if (is_a64(env)) {
        env->xregs[0] = ret;
    } else {
        env->regs[0] = ret;
    }
}

static bool arm_common_semi_sys_exit_is_extended(CPUState *cs)
{
    return is_a64(cpu_env(cs));
}

static bool arm_is_64bit_semihosting(CPUArchState *env)
{
    return is_a64(env);
}

static uint64_t arm_common_semi_stack_bottom(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    return is_a64(env) ? env->xregs[31] : env->regs[13];
}

static bool arm_common_semi_has_synccache(CPUArchState *env)
{
    /* Ok for A64, invalid for A32/T32 */
    return is_a64(env);
}

static const CPUSemihostingOps arm_semihosting_ops = {
    .arg = arm_common_semi_arg,
    .set_ret = arm_common_semi_set_ret,
    .is_64bit = arm_is_64bit_semihosting,
    .sys_exit_is_extended = arm_common_semi_sys_exit_is_extended,
    .stack_bottom = arm_common_semi_stack_bottom,
    .has_synccache = arm_common_semi_has_synccache,
};

TARGET_INFO_CPU_OP(CPU_RESOLVING_TYPE, semihosting, arm_semihosting_ops);
