/*
 *  Misc Sparc helpers
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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
#include "qemu/timer.h"
#include "qemu/host-utils.h"
#include "exec/helper-proto.h"

void cpu_raise_exception_ra(CPUSPARCState *env, int tt, uintptr_t ra)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = tt;
    cpu_loop_exit_restore(cs, ra);
}

void helper_raise_exception(CPUSPARCState *env, int tt)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = tt;
    cpu_loop_exit(cs);
}

void helper_debug(CPUSPARCState *env)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = EXCP_DEBUG;
    cpu_loop_exit(cs);
}

#ifdef TARGET_SPARC64
void helper_tick_set_count(void *opaque, uint64_t count)
{
#if !defined(CONFIG_USER_ONLY)
    cpu_tick_set_count(opaque, count);
#endif
}

uint64_t helper_tick_get_count(CPUSPARCState *env, void *opaque, int mem_idx)
{
#if !defined(CONFIG_USER_ONLY)
    CPUTimer *timer = opaque;

    if (timer->npt && mem_idx < MMU_KERNEL_IDX) {
        cpu_raise_exception_ra(env, TT_PRIV_INSN, GETPC());
    }

    return cpu_tick_get_count(timer);
#else
    /* In user-mode, QEMU_CLOCK_VIRTUAL doesn't exist.
       Just pass through the host cpu clock ticks.  */
    return cpu_get_host_ticks();
#endif
}

void helper_tick_set_limit(void *opaque, uint64_t limit)
{
#if !defined(CONFIG_USER_ONLY)
    cpu_tick_set_limit(opaque, limit);
#endif
}
#endif

static target_ulong do_udiv(CPUSPARCState *env, target_ulong a,
                            target_ulong b, int cc, uintptr_t ra)
{
    target_ulong v, r;
    uint64_t x0 = (uint32_t)a | ((uint64_t)env->y << 32);
    uint32_t x1 = b;

    if (x1 == 0) {
        cpu_raise_exception_ra(env, TT_DIV_ZERO, ra);
    }

    x0 = x0 / x1;
    r = x0;
    v = 0;
    if (unlikely(x0 > UINT32_MAX)) {
        v = r = UINT32_MAX;
    }

    if (cc) {
        env->cc_N = r;
        env->cc_V = v;
        env->cc_icc_Z = r;
        env->cc_icc_C = 0;
#ifdef TARGET_SPARC64
        env->cc_xcc_Z = r;
        env->cc_xcc_C = 0;
#endif
    }
    return r;
}

target_ulong helper_udiv(CPUSPARCState *env, target_ulong a, target_ulong b)
{
    return do_udiv(env, a, b, 0, GETPC());
}

target_ulong helper_udiv_cc(CPUSPARCState *env, target_ulong a, target_ulong b)
{
    return do_udiv(env, a, b, 1, GETPC());
}

static target_ulong do_sdiv(CPUSPARCState *env, target_ulong a,
                            target_ulong b, int cc, uintptr_t ra)
{
    target_ulong v;
    target_long r;
    int64_t x0 = (uint32_t)a | ((uint64_t)env->y << 32);
    int32_t x1 = b;

    if (x1 == 0) {
        cpu_raise_exception_ra(env, TT_DIV_ZERO, ra);
    }
    if (unlikely(x0 == INT64_MIN)) {
        /*
         * Special case INT64_MIN / -1 is required to avoid trap on x86 host.
         * However, with a dividend of INT64_MIN, there is no 32-bit divisor
         * which can yield a 32-bit result:
         *    INT64_MIN / INT32_MIN =  0x1_0000_0000
         *    INT64_MIN / INT32_MAX = -0x1_0000_0002
         * Therefore we know we must overflow and saturate.
         */
        r = x1 < 0 ? INT32_MAX : INT32_MIN;
        v = UINT32_MAX;
    } else {
        x0 = x0 / x1;
        r = (int32_t)x0;
        v = 0;
        if (unlikely(r != x0)) {
            r = x0 < 0 ? INT32_MIN : INT32_MAX;
            v = UINT32_MAX;
        }
    }

    if (cc) {
        env->cc_N = r;
        env->cc_V = v;
        env->cc_icc_Z = r;
        env->cc_icc_C = 0;
#ifdef TARGET_SPARC64
        env->cc_xcc_Z = r;
        env->cc_xcc_C = 0;
#endif
    }
    return r;
}

target_ulong helper_sdiv(CPUSPARCState *env, target_ulong a, target_ulong b)
{
    return do_sdiv(env, a, b, 0, GETPC());
}

target_ulong helper_sdiv_cc(CPUSPARCState *env, target_ulong a, target_ulong b)
{
    return do_sdiv(env, a, b, 1, GETPC());
}

target_ulong helper_taddcctv(CPUSPARCState *env, target_ulong src1,
                             target_ulong src2)
{
    target_ulong dst, v;

    /* Tag overflow occurs if either input has bits 0 or 1 set.  */
    if ((src1 | src2) & 3) {
        goto tag_overflow;
    }

    dst = src1 + src2;

    /* Tag overflow occurs if the addition overflows.  */
    v = ~(src1 ^ src2) & (src1 ^ dst);
    if (v & (1u << 31)) {
        goto tag_overflow;
    }

    /* Only modify the CC after any exceptions have been generated.  */
    env->cc_V = v;
    env->cc_N = dst;
    env->cc_icc_Z = dst;
#ifdef TARGET_SPARC64
    env->cc_xcc_Z = dst;
    env->cc_icc_C = dst ^ src1 ^ src2;
    env->cc_xcc_C = dst < src1;
#else
    env->cc_icc_C = dst < src1;
#endif

    return dst;

 tag_overflow:
    cpu_raise_exception_ra(env, TT_TOVF, GETPC());
}

target_ulong helper_tsubcctv(CPUSPARCState *env, target_ulong src1,
                             target_ulong src2)
{
    target_ulong dst, v;

    /* Tag overflow occurs if either input has bits 0 or 1 set.  */
    if ((src1 | src2) & 3) {
        goto tag_overflow;
    }

    dst = src1 - src2;

    /* Tag overflow occurs if the subtraction overflows.  */
    v = (src1 ^ src2) & (src1 ^ dst);
    if (v & (1u << 31)) {
        goto tag_overflow;
    }

    /* Only modify the CC after any exceptions have been generated.  */
    env->cc_V = v;
    env->cc_N = dst;
    env->cc_icc_Z = dst;
#ifdef TARGET_SPARC64
    env->cc_xcc_Z = dst;
    env->cc_icc_C = dst ^ src1 ^ src2;
    env->cc_xcc_C = src1 < src2;
#else
    env->cc_icc_C = src1 < src2;
#endif

    return dst;

 tag_overflow:
    cpu_raise_exception_ra(env, TT_TOVF, GETPC());
}

#ifndef TARGET_SPARC64
void helper_power_down(CPUSPARCState *env)
{
    CPUState *cs = env_cpu(env);

    cs->halted = 1;
    cs->exception_index = EXCP_HLT;
    env->pc = env->npc;
    env->npc = env->pc + 4;
    cpu_loop_exit(cs);
}
#endif
