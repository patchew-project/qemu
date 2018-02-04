/*
 * Software floating point for size float32
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
#include "single.h"

#define FLOATXX  float32
#define FS       S
#define WC       1

#define _FP_MUL_MEAT_S(R, X, Y) \
    _FP_MUL_MEAT_1_imm(_FP_WFRACBITS_S, R, X, Y)
#define _FP_MUL_MEAT_DW_S(R, X, Y) \
    _FP_MUL_MEAT_DW_1_imm(_FP_WFRACBITS_S, R, X, Y)
#define _FP_DIV_MEAT_S(R, X, Y) \
    _FP_DIV_MEAT_1_imm(S, R, X, Y, _FP_DIV_HELP_imm)

#include "floatxx.inc.c"
