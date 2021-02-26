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
#include "tcg/helper-tcg.h"

void cpu_x86_fsave(CPUX86State *env, target_ulong ptr, int data32)
{
    do_fsave(env, ptr, data32, 0);
}

void cpu_x86_frstor(CPUX86State *env, target_ulong ptr, int data32)
{
    do_frstor(env, ptr, data32, 0);
}

void cpu_x86_fxsave(CPUX86State *env, target_ulong ptr)
{
    do_fxsave(env, ptr, 0);
}

void cpu_x86_fxrstor(CPUX86State *env, target_ulong ptr)
{
    do_fxrstor(env, ptr, 0);
}
