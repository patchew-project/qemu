/*
 * Software floating point for size float64
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
#include "fpu/softfloat.h"
#include "soft-fp.h"
#include "soft-fp-specialize.h"
#include "quad.h"

#define FLOATXX  float128
#define FS       Q
#define WC       2

#define _FP_MUL_MEAT_Q(R, X, Y) \
    _FP_MUL_MEAT_2_wide(_FP_WFRACBITS_Q, R, X, Y, umul_ppmm)
#define _FP_MUL_MEAT_DW_Q(R, X, Y) \
    _FP_MUL_MEAT_DW_2_wide(_FP_WFRACBITS_Q, R, X, Y, umul_ppmm)
#define _FP_DIV_MEAT_Q(R, X, Y) \
    _FP_DIV_MEAT_2_udiv(Q, R, X, Y)

#include "floatxx.inc.c"
