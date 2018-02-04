/*
 * Conversions between floating point types
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
#include "single.h"
#include "double.h"
#include "quad.h"


#define DO_EXTEND(TYPEI, TYPEO, FI, FO, NI, NO)                     \
TYPEO glue(TYPEI, glue(_to_, TYPEO))(TYPEI a, float_status *status) \
{                                                                   \
    FP_DECL_EX;                                                     \
    FP_DECL_##FI(A);                                                \
    FP_DECL_##FO(R);                                                \
    TYPEO r;                                                        \
    FP_INIT_EXCEPTIONS;                                             \
    FP_UNPACK_RAW_##FI(A, a);                                       \
    FP_EXTEND(FO, FI, NO, NI, R, A);                                \
    FP_PACK_RAW_##FO(r, R);                                         \
    FP_HANDLE_EXCEPTIONS;                                           \
    return r;                                                       \
}

DO_EXTEND(float32, float64,  S, D, 1, 1)
DO_EXTEND(float32, float128, S, Q, 1, 2)
DO_EXTEND(float64, float128, D, Q, 1, 2)


#define DO_TRUNC(TYPEI, TYPEO, FI, FO, NI, NO)                      \
TYPEO glue(TYPEI, glue(_to_, TYPEO))(TYPEI a, float_status *status) \
{                                                                   \
    FP_DECL_EX;                                                     \
    FP_DECL_##FI(A);                                                \
    FP_DECL_##FO(R);                                                \
    TYPEO r;                                                        \
    FP_INIT_EXCEPTIONS;                                             \
    FP_UNPACK_SEMIRAW_##FI(A, a);                                   \
    FP_TRUNC(FO, FI, NO, NI, R, A);                                 \
    FP_PACK_SEMIRAW_##FO(r, R);                                     \
    FP_HANDLE_EXCEPTIONS;                                           \
    return r;                                                       \
}

DO_TRUNC(float128, float64,  Q, D, 2, 1)
DO_TRUNC(float128, float32,  Q, S, 2, 1)
DO_TRUNC(float64, float32,   D, S, 1, 1)


/* Half precision floats come in two formats: standard IEEE and "ARM" format.
 * The latter gains extra exponent range by omitting the NaN/Inf encodings.
 */

#define DO_EXTEND_H(TYPEO, FO)                                      \
TYPEO glue(float16_to_, TYPEO)(float16 a, bool ieee, float_status *status) \
{                                                                   \
    FP_DECL_EX;                                                     \
    FP_DECL_H(A);                                                   \
    FP_DECL_##FO(R);                                                \
    TYPEO r;                                                        \
    FP_INIT_EXCEPTIONS;                                             \
    FP_UNPACK_RAW_H(A, a);                                          \
    if (!ieee && A_e == _FP_EXPMAX_H) {                             \
        R_s = A_s;                                                  \
        R_e = A_e + _FP_EXPBIAS_##FO - _FP_EXPBIAS_H;               \
        R_f = A_f;                                                  \
        _FP_FRAC_SLL_1(R, (_FP_FRACBITS_##FO - _FP_FRACBITS_H));    \
    } else {                                                        \
        FP_EXTEND(FO, H, 1, 1, R, A);                               \
    }                                                               \
    FP_PACK_RAW_##FO(r, R);                                         \
    FP_HANDLE_EXCEPTIONS;                                           \
    return r;                                                       \
}

DO_EXTEND_H(float32, S)
DO_EXTEND_H(float64, D)

#define DO_TRUNC_H(TYPEI, FI)                                       \
float16 glue(TYPEI, _to_float16)(TYPEI a, bool ieee, float_status *status) \
{                                                                   \
    FP_DECL_EX;                                                     \
    FP_DECL_##FI(A);                                                \
    FP_DECL_H(R);                                                   \
    float16 r;                                                      \
    FP_INIT_EXCEPTIONS;                                             \
    FP_UNPACK_SEMIRAW_##FI(A, a);                                   \
    if (unlikely(!ieee)) {                                          \
        R_s = A_s;                                                  \
        if (A_e == _FP_EXPMAX_##FI) {                               \
            FP_SET_EXCEPTION(FP_EX_INVALID);                        \
            if (A_f == 0) {                                         \
                /* Inf maps to largest normal.  */                  \
                R_e = _FP_EXPMAX_H;                                 \
                R_f = (1 << _FP_FRACBITS_H) - 1;                    \
            } else {                                                \
                /* NaN maps to zero.  */                            \
                R_e = R_f = 0;                                      \
            }                                                       \
            FP_PACK_RAW_H(r, R);                                    \
            goto done;                                              \
        }                                                           \
        /* ARM format needs different rounding near max exponent. */ \
        R_e = A_e + _FP_EXPBIAS_H - _FP_EXPBIAS_##FI;               \
        if (R_e >= _FP_EXPMAX_H - 1) {                              \
            _FP_FRAC_SRS_1(A, (_FP_WFRACBITS_##FI - _FP_WFRACBITS_H), \
                           _FP_WFRACBITS_##FI);                     \
            R_f = A_f;                                              \
            _FP_ROUND(1, R);                                        \
            if (R_f & (_FP_OVERFLOW_H >> 1)) {                      \
                R_f &= ~(_FP_OVERFLOW_H >> 1);                      \
                R_e++;                                              \
                if (R_e > _FP_EXPMAX_H) {                           \
                    /* Overflow saturates to largest normal.  */    \
                    FP_SET_EXCEPTION(FP_EX_INVALID);                \
                    R_e = _FP_EXPMAX_H;                             \
                    R_f = (1 << _FP_FRACBITS_H) - 1;                \
                } else {                                            \
                    R_f >>= _FP_WORKBITS;                           \
                }                                                   \
            } else {                                                \
                R_f >>= _FP_WORKBITS;                               \
            }                                                       \
            FP_PACK_RAW_H(r, R);                                    \
            goto done;                                              \
        }                                                           \
    }                                                               \
    FP_TRUNC(H, FI, 1, 1, R, A);                                    \
    FP_PACK_SEMIRAW_H(r, R);                                        \
 done:                                                              \
    FP_HANDLE_EXCEPTIONS;                                           \
    return r;                                                       \
}

DO_TRUNC_H(float64, D)
DO_TRUNC_H(float32, S)
