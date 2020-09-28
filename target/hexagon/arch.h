/*
 *  Copyright(c) 2019-2020 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HEXAGON_ARCH_H
#define HEXAGON_ARCH_H

#include "cpu.h"
#include "hex_arch_types.h"

extern uint64_t interleave(uint32_t odd, uint32_t even);
extern uint64_t deinterleave(uint64_t src);
extern uint32_t carry_from_add64(uint64_t a, uint64_t b, uint32_t c);
extern int32_t conv_round(int32_t a, int n);
extern size16s_t cast8s_to_16s(int64_t a);
extern int64_t cast16s_to_8s(size16s_t a);
extern size16s_t add128(size16s_t a, size16s_t b);
extern size16s_t sub128(size16s_t a, size16s_t b);
extern size16s_t shiftr128(size16s_t a, uint32_t n);
extern size16s_t shiftl128(size16s_t a, uint32_t n);
extern size16s_t and128(size16s_t a, size16s_t b);
extern void arch_fpop_start(CPUHexagonState *env);
extern void arch_fpop_end(CPUHexagonState *env);
extern void arch_raise_fpflag(unsigned int flags);
extern int arch_sf_recip_common(int32_t *Rs, int32_t *Rt, int32_t *Rd,
                                int *adjust);
extern int arch_sf_invsqrt_common(int32_t *Rs, int32_t *Rd, int *adjust);

#endif
