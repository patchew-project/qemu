/*
 *  Helpers for system instructions.
 *
 *  Copyright (c) 2007 Jocelyn Mayer
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "sysemu/sysemu.h"
#include "qemu/timer.h"


uint64_t helper_load_pcc(CPUAlphaState *env)
{
#ifndef CONFIG_USER_ONLY
    uint64_t pcc;
    /* In system mode we have access to a decent high-resolution clock.
       In order to make OS-level time accounting work with the RPCC,
       present it with a well-timed clock fixed at 250MHz.  */
    qemu_mutex_lock_iothread();
    pcc = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) >> 2;
    qemu_mutex_unlock_iothread();
    return deposit64(pcc, 32, 32, env->pcc_ofs);
#else
    /* In user-mode, QEMU_CLOCK_VIRTUAL doesn't exist.  Just pass through the host cpu
       clock ticks.  Also, don't bother taking PCC_OFS into account.  */
    return (uint32_t)cpu_get_host_ticks();
#endif
}

/* PALcode support special instructions */
#ifndef CONFIG_USER_ONLY
void helper_tbia(CPUAlphaState *env)
{
    tlb_flush(CPU(alpha_env_get_cpu(env)));
}

void helper_tbis(CPUAlphaState *env, uint64_t p)
{
    tlb_flush_page(CPU(alpha_env_get_cpu(env)), p);
}

void helper_tb_flush(CPUAlphaState *env)
{
    tb_flush(CPU(alpha_env_get_cpu(env)));
}

void helper_halt(uint64_t restart)
{
    if (restart) {
        qemu_system_reset_request();
    } else {
        qemu_system_shutdown_request();
    }
}

uint64_t helper_get_vmtime(void)
{
    uint64_t ret;
    qemu_mutex_lock_iothread();
    ret = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    qemu_mutex_unlock_iothread();
    return ret;
}

uint64_t helper_get_walltime(void)
{
    uint64_t ret;
    qemu_mutex_lock_iothread();
    ret = qemu_clock_get_ns(rtc_clock);
    qemu_mutex_unlock_iothread();
    return ret;
}

void helper_set_alarm(CPUAlphaState *env, uint64_t expire)
{
    AlphaCPU *cpu = alpha_env_get_cpu(env);

    qemu_mutex_lock_iothread();
    if (expire) {
        env->alarm_expire = expire;
        timer_mod(cpu->alarm_timer, expire);
    } else {
        timer_del(cpu->alarm_timer);
    }
    qemu_mutex_unlock_iothread();
}

#endif /* CONFIG_USER_ONLY */
