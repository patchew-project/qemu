/*
 *  AArch64 Vector Flags
 *
 *  Copyright (c) 2017 Linaro
 *  Author: Alex Bennée <alex.bennee@linaro.org>
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

/* AdvSIMD element data
 *
 * We pack all the additional information for elements into a single
 * 32 bit constant passed by register. Hopefully for groups of
 * identical operations on different registers this should propergate
 * nicely in the TCG.
 *
 * The following control element iteration:
 *   ADVSIMD_OPR_ELT  - the count of elements affected
 *   ADVSIMD_ALL_ELT  - the total count of elements (e.g. clear all-opr elements)
 *   ADVSIMD_DOFF_ELT - the offset for the destination register (e.g. foo2 ops)
 *
 * We encode immediate data in:
 *   ADVSIMD_DATA
 *
 * Typically this is things like shift counts and the like.
 */

#define ADVSIMD_OPR_ELT_BITS      5
#define ADVSIMD_OPR_ELT_SHIFT     0
#define ADVSIMD_ALL_ELT_BITS      5
#define ADVSIMD_ALL_ELT_SHIFT     5
#define ADVSIMD_DOFF_ELT_BITS     5
#define ADVSIMD_DOFF_ELT_SHIFT   10
#define ADVSIMD_DATA_BITS        16
#define ADVSIMD_DATA_SHIFT       16

#define GET_SIMD_DATA(t, d) extract32(d,    \
        ADVSIMD_ ## t ## _SHIFT,            \
        ADVSIMD_ ## t ## _BITS)
