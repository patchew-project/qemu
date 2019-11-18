/* Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved. */

/*
 * Hexagon helpers
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "tcg-op.h"
#include "cpu.h"
#include "qemu/main-loop.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"

/* Exceptions processing helpers */
void QEMU_NORETURN do_raise_exception_err(CPUHexagonState *env,
                                          uint32_t exception, uintptr_t pc)
{
    CPUState *cs = CPU(hexagon_env_get_cpu(env));
    qemu_log_mask(CPU_LOG_INT, "%s: %d\n", __func__, exception);
    cs->exception_index = exception;
    cpu_loop_exit_restore(cs, pc);
}

void helper_raise_exception(CPUHexagonState *env, uint32_t exception)
{
    do_raise_exception_err(env, exception, 0);
}

