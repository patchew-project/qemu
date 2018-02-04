/*
 * Software floating point for size float16
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
#include "half.h"

/* No point in using a 64-bit type for float16.  */
#undef _FP_W_TYPE_SIZE
#undef _FP_W_TYPE
#undef _FP_WS_TYPE
#define _FP_W_TYPE_SIZE    32
#define _FP_W_TYPE         uint32_t
#define _FP_WS_TYPE        int32_t

#define FLOATXX  float16
#define FS       H
#define WC       1

#define _FP_MUL_MEAT_H(R, X, Y) \
    _FP_MUL_MEAT_1_imm(_FP_WFRACBITS_H, R, X, Y)
#define _FP_MUL_MEAT_DW_H(R, X, Y) \
    _FP_MUL_MEAT_DW_1_imm(_FP_WFRACBITS_H, R, X, Y)
#define _FP_DIV_MEAT_H(R, X, Y) \
    _FP_DIV_MEAT_1_imm(H, R, X, Y, _FP_DIV_HELP_imm)

#include "floatxx.inc.c"
