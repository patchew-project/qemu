/*
 *  Microblaze FPU helper routines.
 *
 *  Copyright (c) 2009 Edgar E. Iglesias <edgar.iglesias@gmail.com>.
 *  Copyright (c) 2009-2012 PetaLogix Qld Pty Ltd.
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
#include "exec/helper-proto.h"
#include "exec/exec-all.h"
#include "fpu/softfloat.h"

static bool check_divz(CPUMBState *env, uint32_t a, uint32_t b, uintptr_t ra)
{
    if (unlikely(b == 0)) {
        env->msr |= MSR_DZ;

        if ((env->msr & MSR_EE) &&
            env_archcpu(env)->cfg.div_zero_exception) {
            CPUState *cs = env_cpu(env);

            env->esr = ESR_EC_DIVZERO;
            cs->exception_index = EXCP_HW_EXCP;
            cpu_loop_exit_restore(cs, ra);
        }
        return false;
    }
    return true;
}

uint32_t helper_divs(CPUMBState *env, uint32_t a, uint32_t b)
{
    if (!check_divz(env, a, b, GETPC())) {
        return 0;
    }
    return (int32_t)a / (int32_t)b;
}

uint32_t helper_divu(CPUMBState *env, uint32_t a, uint32_t b)
{
    if (!check_divz(env, a, b, GETPC())) {
        return 0;
    }
    return a / b;
}

/* raise FPU exception.  */
static void raise_fpu_exception(CPUMBState *env, uintptr_t ra)
{
    CPUState *cs = env_cpu(env);

    env->esr = ESR_EC_FPU;
    cs->exception_index = EXCP_HW_EXCP;
    cpu_loop_exit_restore(cs, ra);
}

static void update_fpu_flags(CPUMBState *env, int flags, uintptr_t ra)
{
    int raise = 0;

    if (flags & float_flag_invalid) {
        env->fsr |= FSR_IO;
        raise = 1;
    }
    if (flags & float_flag_divbyzero) {
        env->fsr |= FSR_DZ;
        raise = 1;
    }
    if (flags & float_flag_overflow) {
        env->fsr |= FSR_OF;
        raise = 1;
    }
    if (flags & float_flag_underflow) {
        env->fsr |= FSR_UF;
        raise = 1;
    }
    if (raise
        && (env_archcpu(env)->cfg.pvr_regs[2] & PVR2_FPU_EXC_MASK)
        && (env->msr & MSR_EE)) {
        raise_fpu_exception(env, ra);
    }
}

uint32_t helper_fadd(CPUMBState *env, uint32_t a, uint32_t b)
{
    CPU_FloatU fd, fa, fb;
    int flags;

    set_float_exception_flags(0, &env->fp_status);
    fa.l = a;
    fb.l = b;
    fd.f = float32_add(fa.f, fb.f, &env->fp_status);

    flags = get_float_exception_flags(&env->fp_status);
    update_fpu_flags(env, flags, GETPC());
    return fd.l;
}

uint32_t helper_frsub(CPUMBState *env, uint32_t a, uint32_t b)
{
    CPU_FloatU fd, fa, fb;
    int flags;

    set_float_exception_flags(0, &env->fp_status);
    fa.l = a;
    fb.l = b;
    fd.f = float32_sub(fb.f, fa.f, &env->fp_status);
    flags = get_float_exception_flags(&env->fp_status);
    update_fpu_flags(env, flags, GETPC());
    return fd.l;
}

uint32_t helper_fmul(CPUMBState *env, uint32_t a, uint32_t b)
{
    CPU_FloatU fd, fa, fb;
    int flags;

    set_float_exception_flags(0, &env->fp_status);
    fa.l = a;
    fb.l = b;
    fd.f = float32_mul(fa.f, fb.f, &env->fp_status);
    flags = get_float_exception_flags(&env->fp_status);
    update_fpu_flags(env, flags, GETPC());

    return fd.l;
}

uint32_t helper_fdiv(CPUMBState *env, uint32_t a, uint32_t b)
{
    CPU_FloatU fd, fa, fb;
    int flags;

    set_float_exception_flags(0, &env->fp_status);
    fa.l = a;
    fb.l = b;
    fd.f = float32_div(fb.f, fa.f, &env->fp_status);
    flags = get_float_exception_flags(&env->fp_status);
    update_fpu_flags(env, flags, GETPC());

    return fd.l;
}

uint32_t helper_fcmp_un(CPUMBState *env, uint32_t a, uint32_t b)
{
    CPU_FloatU fa, fb;
    uint32_t r = 0;

    fa.l = a;
    fb.l = b;

    if (float32_is_signaling_nan(fa.f, &env->fp_status) ||
        float32_is_signaling_nan(fb.f, &env->fp_status)) {
        update_fpu_flags(env, float_flag_invalid, GETPC());
        r = 1;
    }

    if (float32_is_quiet_nan(fa.f, &env->fp_status) ||
        float32_is_quiet_nan(fb.f, &env->fp_status)) {
        r = 1;
    }

    return r;
}

uint32_t helper_fcmp_lt(CPUMBState *env, uint32_t a, uint32_t b)
{
    CPU_FloatU fa, fb;
    int r;
    int flags;

    set_float_exception_flags(0, &env->fp_status);
    fa.l = a;
    fb.l = b;
    r = float32_lt(fb.f, fa.f, &env->fp_status);
    flags = get_float_exception_flags(&env->fp_status);
    update_fpu_flags(env, flags & float_flag_invalid, GETPC());

    return r;
}

uint32_t helper_fcmp_eq(CPUMBState *env, uint32_t a, uint32_t b)
{
    CPU_FloatU fa, fb;
    int flags;
    int r;

    set_float_exception_flags(0, &env->fp_status);
    fa.l = a;
    fb.l = b;
    r = float32_eq_quiet(fa.f, fb.f, &env->fp_status);
    flags = get_float_exception_flags(&env->fp_status);
    update_fpu_flags(env, flags & float_flag_invalid, GETPC());

    return r;
}

uint32_t helper_fcmp_le(CPUMBState *env, uint32_t a, uint32_t b)
{
    CPU_FloatU fa, fb;
    int flags;
    int r;

    fa.l = a;
    fb.l = b;
    set_float_exception_flags(0, &env->fp_status);
    r = float32_le(fa.f, fb.f, &env->fp_status);
    flags = get_float_exception_flags(&env->fp_status);
    update_fpu_flags(env, flags & float_flag_invalid, GETPC());


    return r;
}

uint32_t helper_fcmp_gt(CPUMBState *env, uint32_t a, uint32_t b)
{
    CPU_FloatU fa, fb;
    int flags, r;

    fa.l = a;
    fb.l = b;
    set_float_exception_flags(0, &env->fp_status);
    r = float32_lt(fa.f, fb.f, &env->fp_status);
    flags = get_float_exception_flags(&env->fp_status);
    update_fpu_flags(env, flags & float_flag_invalid, GETPC());
    return r;
}

uint32_t helper_fcmp_ne(CPUMBState *env, uint32_t a, uint32_t b)
{
    CPU_FloatU fa, fb;
    int flags, r;

    fa.l = a;
    fb.l = b;
    set_float_exception_flags(0, &env->fp_status);
    r = !float32_eq_quiet(fa.f, fb.f, &env->fp_status);
    flags = get_float_exception_flags(&env->fp_status);
    update_fpu_flags(env, flags & float_flag_invalid, GETPC());

    return r;
}

uint32_t helper_fcmp_ge(CPUMBState *env, uint32_t a, uint32_t b)
{
    CPU_FloatU fa, fb;
    int flags, r;

    fa.l = a;
    fb.l = b;
    set_float_exception_flags(0, &env->fp_status);
    r = !float32_lt(fa.f, fb.f, &env->fp_status);
    flags = get_float_exception_flags(&env->fp_status);
    update_fpu_flags(env, flags & float_flag_invalid, GETPC());

    return r;
}

uint32_t helper_flt(CPUMBState *env, uint32_t a)
{
    CPU_FloatU fd, fa;

    fa.l = a;
    fd.f = int32_to_float32(fa.l, &env->fp_status);
    return fd.l;
}

uint32_t helper_fint(CPUMBState *env, uint32_t a)
{
    CPU_FloatU fa;
    uint32_t r;
    int flags;

    set_float_exception_flags(0, &env->fp_status);
    fa.l = a;
    r = float32_to_int32(fa.f, &env->fp_status);
    flags = get_float_exception_flags(&env->fp_status);
    update_fpu_flags(env, flags, GETPC());

    return r;
}

uint32_t helper_fsqrt(CPUMBState *env, uint32_t a)
{
    CPU_FloatU fd, fa;
    int flags;

    set_float_exception_flags(0, &env->fp_status);
    fa.l = a;
    fd.l = float32_sqrt(fa.f, &env->fp_status);
    flags = get_float_exception_flags(&env->fp_status);
    update_fpu_flags(env, flags, GETPC());

    return fd.l;
}
