/*
 *  x86 FPU, MMX/3DNow!/SSE/SSE2/SSE3/SSSE3/SSE4/PNI helpers (user-mode)
 *
 *  Copyright (c) 2003 Fabrice Bellard
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

/*
 * XXX in helper_fsave() we reference GETPC(). Which is only valid when
 * directly called from code_gen_buffer.
 *
 * The signature of cpu_x86_foo should be changed to add a "uintptr_t retaddr"
 * argument, the callers from linux-user/i386/signal.c must pass 0 for this
 * new argument (no unwind required), and the helpers must do
 *
 * void helper_fsave(CPUX86State *env, target_ulong ptr, int data32)
 * {
 *    cpu_x86_fsave(env, ptr, data32, GETPC());
 * }
 *
 * etc.
 */

void cpu_x86_fsave(CPUX86State *env, target_ulong ptr, int data32)
{
    helper_fsave(env, ptr, data32);
}

void cpu_x86_frstor(CPUX86State *env, target_ulong ptr, int data32)
{
    helper_frstor(env, ptr, data32);
}

void cpu_x86_fxsave(CPUX86State *env, target_ulong ptr)
{
    helper_fxsave(env, ptr);
}

void cpu_x86_fxrstor(CPUX86State *env, target_ulong ptr)
{
    helper_fxrstor(env, ptr);
}
