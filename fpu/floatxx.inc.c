/*
 * Software floating point for a given type.
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

/* Before including this file, define:
 *   FLOATXX   the floating-point type
 *   FS        the type letter (e.g. S, D, Q)
 *   WC        the word count required
 * and include all of the relevant files.
 */

#define _FP_ISNAN(fs, wc, X) \
    (X##_e == _FP_EXPMAX_##fs && !_FP_FRAC_ZEROP_##wc (X))
#define FP_ISNAN(fs, wc, X) \
    _FP_ISNAN(fs, wc, X)
#define FP_CHOOSENAN(fs, wc, R, A, B, OP) \
    _FP_CHOOSENAN(fs, wc, R, A, B, OP)
#define FP_SETQNAN(fs, wc, X) \
    _FP_SETQNAN(fs, wc, X)
#define FP_FRAC_SNANP(fs, X) \
    _FP_FRAC_SNANP(fs, X)
#define FP_ADD_INTERNAL(fs, wc, R, A, B, OP) \
    _FP_ADD_INTERNAL(fs, wc, R, A, B, '-')
#define FP_ROUND(wc, X) \
    _FP_ROUND(wc, X)

static FLOATXX addsub_internal(FLOATXX a, FLOATXX b, float_status *status,
                               bool subtract)
{
    FP_DECL_EX;
    glue(FP_DECL_, FS)(A);
    glue(FP_DECL_, FS)(B);
    glue(FP_DECL_, FS)(R);
    FLOATXX r;

    FP_INIT_ROUNDMODE;
    glue(FP_UNPACK_SEMIRAW_, FS)(A, a);
    glue(FP_UNPACK_SEMIRAW_, FS)(B, b);

    B_s ^= (subtract & !FP_ISNAN(FS, WC, B));
    FP_ADD_INTERNAL(FS, WC, R, A, B, '+');

    glue(FP_PACK_SEMIRAW_, FS)(r, R);
    FP_HANDLE_EXCEPTIONS;

    return r;
}

FLOATXX glue(FLOATXX,_add)(FLOATXX a, FLOATXX b, float_status *status)
{
    return addsub_internal(a, b, status, false);
}

FLOATXX glue(FLOATXX,_sub)(FLOATXX a, FLOATXX b, float_status *status)
{
    return addsub_internal(a, b, status, true);
}

FLOATXX glue(FLOATXX,_mul)(FLOATXX a, FLOATXX b, float_status *status)
{
    FP_DECL_EX;
    glue(FP_DECL_, FS)(A);
    glue(FP_DECL_, FS)(B);
    glue(FP_DECL_, FS)(R);
    FLOATXX r;

    FP_INIT_ROUNDMODE;
    glue(FP_UNPACK_, FS)(A, a);
    glue(FP_UNPACK_, FS)(B, b);
    glue(FP_MUL_, FS)(R, A, B);
    glue(FP_PACK_, FS)(r, R);
    FP_HANDLE_EXCEPTIONS;

    return r;
}

FLOATXX glue(FLOATXX,_div)(FLOATXX a, FLOATXX b, float_status *status)
{
    FP_DECL_EX;
    glue(FP_DECL_, FS)(A);
    glue(FP_DECL_, FS)(B);
    glue(FP_DECL_, FS)(R);
    FLOATXX r;

    FP_INIT_ROUNDMODE;
    glue(FP_UNPACK_, FS)(A, a);
    glue(FP_UNPACK_, FS)(B, b);
    glue(FP_DIV_, FS)(R, A, B);
    glue(FP_PACK_, FS)(r, R);
    FP_HANDLE_EXCEPTIONS;

    return r;
}

FLOATXX glue(FLOATXX,_sqrt)(FLOATXX a, float_status *status)
{
    FP_DECL_EX;
    glue(FP_DECL_, FS)(A);
    glue(FP_DECL_, FS)(R);
    FLOATXX r;

    FP_INIT_ROUNDMODE;
    glue(FP_UNPACK_, FS)(A, a);
    glue(FP_SQRT_, FS)(R, A);
    glue(FP_PACK_, FS)(r, R);
    FP_HANDLE_EXCEPTIONS;

    return r;
}

FLOATXX glue(FLOATXX,_scalbn)(FLOATXX a, int n, float_status *status)
{
    FP_DECL_EX;
    glue(FP_DECL_, FS)(A);

    FP_INIT_ROUNDMODE;
    glue(FP_UNPACK_, FS)(A, a);

    if (likely(A_c == FP_CLS_NORMAL)) {
        /* Bound N such that the exponent can safely adjusted without
           overflowing.  The maximum is large enough to take the smallest
           denormal up beyond the largest normal, which will overflow
           to infinity when we repack.  */
        int max = glue(_FP_EXPMAX_, FS) + glue(_FP_FRACBITS_, FS);
        if (n > max) {
            n = max;
        } else if (n < -max) {
            n = -max;
        }
        A_e += n;
    }

    glue(FP_PACK_, FS)(a, A);
    FP_HANDLE_EXCEPTIONS;

    return a;
}

FLOATXX glue(FLOATXX,_muladd)(FLOATXX a, FLOATXX b, FLOATXX c, int flags,
                              float_status *status)
{
    FP_DECL_EX;
    glue(FP_DECL_, FS)(A);
    glue(FP_DECL_, FS)(B);
    glue(FP_DECL_, FS)(C);
    glue(FP_DECL_, FS)(R);
    FLOATXX r;

    FP_INIT_ROUNDMODE;
    glue(FP_UNPACK_, FS)(A, a);
    glue(FP_UNPACK_, FS)(B, b);
    glue(FP_UNPACK_, FS)(C, c);

    /* R_e is not set in cases where it is not used in packing, but the
     * compiler does not see that it is set in all cases where it is used,
     * resulting in warnings that it may be used uninitialized.
     * For QEMU, we will usually read it before packing, for halve_result.
     */
    R_e = 0;

    /* _FP_FMA does pair-wise calls to _FP_CHOOSENAN.  For proper
       emulation of the target cpu we need to do better than that.  */
    if (A_c == FP_CLS_NAN || B_c == FP_CLS_NAN || C_c == FP_CLS_NAN) {
        bool a_snan = A_c == FP_CLS_NAN && FP_FRAC_SNANP(FS, A);
        bool b_snan = B_c == FP_CLS_NAN && FP_FRAC_SNANP(FS, B);
        bool c_snan = C_c == FP_CLS_NAN && FP_FRAC_SNANP(FS, C);
        int p = pick_nan_muladd(A_c, a_snan, B_c, b_snan, C_c, c_snan, status);

        R_c = FP_CLS_NAN;
        switch (p) {
        case 0:
            R_s = A_s;
            glue(_FP_FRAC_COPY_, WC)(R, A);
            break;
        case 1:
            R_s = B_s;
            glue(_FP_FRAC_COPY_, WC)(R, B);
            break;
        case 2:
            R_s = C_s;
            glue(_FP_FRAC_COPY_, WC)(R, C);
            break;
        default:
            R_s = glue(_FP_NANSIGN_, FS);
            glue(_FP_FRAC_SET_, WC)(R, glue(_FP_NANFRAC_, FS));
            break;
        }
        /* Any SNaN result will be silenced during _FP_PACK_CANONICAL.  */
    } else {
        C_s ^= (flags & float_muladd_negate_c) != 0;
        B_s ^= (flags & float_muladd_negate_product) != 0;

        glue(FP_FMA_, FS)(R, A, B, C);

        R_s ^= ((flags & float_muladd_negate_result) && R_c != FP_CLS_NAN);
        R_e -= ((flags & float_muladd_halve_result) && R_c == FP_CLS_NORMAL);
    }

    glue(FP_PACK_, FS)(r, R);
    FP_HANDLE_EXCEPTIONS;

    return r;
}

#define DO_FLOAT_TO_INT(NAME, SZ, FP_TO_INT_WHICH)   \
int##SZ##_t NAME(FLOATXX a, float_status *status) \
{                                                 \
    FP_DECL_EX;                                   \
    glue(FP_DECL_, FS)(A);                        \
    uint##SZ##_t r;                               \
    FP_INIT_ROUNDMODE;                            \
    glue(FP_UNPACK_RAW_, FS)(A, a);               \
    glue(FP_TO_INT_WHICH, FS)(r, A, SZ, 1);       \
    FP_HANDLE_EXCEPTIONS;                         \
    return r;                                     \
}

#define DO_FLOAT_TO_UINT(NAME, SZ, FP_TO_INT_WHICH)   \
uint##SZ##_t NAME(FLOATXX a, float_status *status) \
{                                                  \
    FP_DECL_EX;                                    \
    glue(FP_DECL_, FS)(A);                         \
    uint##SZ##_t r;                                \
    FP_INIT_ROUNDMODE;                             \
    glue(FP_UNPACK_RAW_, FS)(A, a);                \
    glue(FP_TO_INT_WHICH, FS)(r, A, SZ, 0);        \
    FP_HANDLE_EXCEPTIONS;                          \
    return r;                                      \
}

DO_FLOAT_TO_INT(glue(FLOATXX,_to_int16), 16, FP_TO_INT_ROUND_)
DO_FLOAT_TO_INT(glue(FLOATXX,_to_int32), 32, FP_TO_INT_ROUND_)
DO_FLOAT_TO_INT(glue(FLOATXX,_to_int64), 64, FP_TO_INT_ROUND_)

DO_FLOAT_TO_INT(glue(FLOATXX,_to_int16_round_to_zero), 16, FP_TO_INT_)
DO_FLOAT_TO_INT(glue(FLOATXX,_to_int32_round_to_zero), 32, FP_TO_INT_)
DO_FLOAT_TO_INT(glue(FLOATXX,_to_int64_round_to_zero), 64, FP_TO_INT_)

DO_FLOAT_TO_UINT(glue(FLOATXX,_to_uint16), 16, FP_TO_INT_ROUND_)
DO_FLOAT_TO_UINT(glue(FLOATXX,_to_uint32), 32, FP_TO_INT_ROUND_)
DO_FLOAT_TO_UINT(glue(FLOATXX,_to_uint64), 64, FP_TO_INT_ROUND_)

DO_FLOAT_TO_UINT(glue(FLOATXX,_to_uint16_round_to_zero), 16, FP_TO_INT_)
DO_FLOAT_TO_UINT(glue(FLOATXX,_to_uint32_round_to_zero), 32, FP_TO_INT_)
DO_FLOAT_TO_UINT(glue(FLOATXX,_to_uint64_round_to_zero), 64, FP_TO_INT_)

#undef DO_FLOAT_TO_INT
#undef DO_FLOAT_TO_UINT

FLOATXX glue(int64_to_,FLOATXX)(int64_t a, float_status *status)
{
    FP_DECL_EX;
    glue(FP_DECL_, FS)(R);
    FLOATXX r;

    FP_INIT_ROUNDMODE;
    glue(FP_FROM_INT_, FS)(R, a, 64, uint64_t);
    glue(FP_PACK_RAW_, FS)(r, R);
    FP_HANDLE_EXCEPTIONS;
    return r;
}

FLOATXX glue(int16_to_,FLOATXX)(int16_t a, float_status *status)
{
    return glue(int64_to_,FLOATXX)(a, status);
}

FLOATXX glue(int32_to_,FLOATXX)(int32_t a, float_status *status)
{
    return glue(int64_to_,FLOATXX)(a, status);
}

/* The code within _FP_FROM_INT always tests A against 0.  For the
   unsigned conversion, this may result in a compiler warning.
   For -Werror, we need to suppress this.  */

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtype-limits"

FLOATXX glue(uint64_to_,FLOATXX)(uint64_t a, float_status *status)
{
    FP_DECL_EX;
    glue(FP_DECL_, FS)(R);
    FLOATXX r;

    FP_INIT_ROUNDMODE;
    glue(FP_FROM_INT_, FS)(R, a, 64, uint64_t);
    glue(FP_PACK_RAW_, FS)(r, R);
    FP_HANDLE_EXCEPTIONS;
    return r;
}

#pragma GCC diagnostic pop

FLOATXX glue(uint16_to_,FLOATXX)(uint16_t a, float_status *status)
{
    return glue(uint64_to_,FLOATXX)(a, status);
}

FLOATXX glue(uint32_to_,FLOATXX)(uint32_t a, float_status *status)
{
    return glue(uint64_to_,FLOATXX)(a, status);
}

static int compare_internal(FLOATXX a, FLOATXX b,
                            float_status *status, bool quiet)
{
    FP_DECL_EX;
    glue(FP_DECL_, FS)(A);
    glue(FP_DECL_, FS)(B);
    int r;

    FP_INIT_EXCEPTIONS;
    glue(FP_UNPACK_RAW_, FS)(A, a);
    glue(FP_UNPACK_RAW_, FS)(B, b);
    glue(FP_CMP_, FS)(r, A, B, float_relation_unordered, (quiet ? 1 : 2));
    FP_HANDLE_EXCEPTIONS;

    return r;
}

int glue(FLOATXX,_compare)(FLOATXX a, FLOATXX b, float_status *status)
{
    return compare_internal(a, b, status, false);
}

int glue(FLOATXX,_compare_quiet)(FLOATXX a, FLOATXX b, float_status *status)
{
    return compare_internal(a, b, status, true);
}

int glue(FLOATXX,_eq)(FLOATXX a, FLOATXX b, float_status *status)
{
    return compare_internal(a, b, status, false) == 0;
}

int glue(FLOATXX,_le)(FLOATXX a, FLOATXX b, float_status *status)
{
    return compare_internal(a, b, status, false) <= 0;
}

int glue(FLOATXX,_lt)(FLOATXX a, FLOATXX b, float_status *status)
{
    return compare_internal(a, b, status, false) < 0;
}

int glue(FLOATXX,_unordered)(FLOATXX a, FLOATXX b, float_status *status)
{
    return compare_internal(a, b, status, false) == float_relation_unordered;
}

int glue(FLOATXX,_eq_quiet)(FLOATXX a, FLOATXX b, float_status *status)
{
    return compare_internal(a, b, status, true) == 0;
}

int glue(FLOATXX,_le_quiet)(FLOATXX a, FLOATXX b, float_status *status)
{
    return compare_internal(a, b, status, true) <= 0;
}

int glue(FLOATXX,_lt_quiet)(FLOATXX a, FLOATXX b, float_status *status)
{
    return compare_internal(a, b, status, true) < 0;
}

int glue(FLOATXX,_unordered_quiet)(FLOATXX a, FLOATXX b, float_status *status)
{
    return compare_internal(a, b, status, true) == float_relation_unordered;
}

#define MINMAX_MAX  0
#define MINMAX_MIN  1
#define MINMAX_IEEE 2
#define MINMAX_MAG  4

static FLOATXX minmax_internal(FLOATXX a, FLOATXX b,
                               float_status *status, int flags)
{
    FP_DECL_EX;
    glue(FP_DECL_, FS)(A);
    glue(FP_DECL_, FS)(B);
    bool save_A_s;
    int cmp;

    FP_INIT_EXCEPTIONS;
    glue(FP_UNPACK_RAW_, FS)(A, a);
    glue(FP_UNPACK_RAW_, FS)(B, b);

    /* When comparing magnitudes, squish the signs.  */
    save_A_s = A_s;
    if (flags & MINMAX_MAG) {
        A_s = B_s = 0;
    }

    glue(FP_CMP_, FS)(cmp, A, B, float_relation_unordered, 1);
    FP_HANDLE_EXCEPTIONS;

    if (unlikely(cmp == float_relation_unordered)) {
        glue(FP_DECL_, FS)(R);
        FLOATXX r;

        if (flags & MINMAX_IEEE) {
            if (glue(FP_ISSIGNAN_, FS)(A) || glue(FP_ISSIGNAN_, FS)(B)) {
                /* fall through to FP_CHOOSENAN */
            } else if (!FP_ISNAN(FS, WC, A)) {
                return a;
            } else if (!FP_ISNAN(FS, WC, B)) {
                return b;
            }
        }

        FP_CHOOSENAN(FS, WC, R, A, B, 'm');
        FP_SETQNAN(FS, WC, R);
        glue(FP_PACK_RAW_, FS)(r, R);
        return r;
    }

    /* Specially handle min(+0.0, -0.0) = -0.0, which compare as equal. */
    cmp = (cmp == 0 ? save_A_s : cmp < 0);
    cmp ^= flags & MINMAX_MIN;
    return cmp ? b : a;
}

FLOATXX glue(FLOATXX,_max)(FLOATXX a, FLOATXX b, float_status *status)
{
    return minmax_internal(a, b, status, MINMAX_MAX);
}

FLOATXX glue(FLOATXX,_min)(FLOATXX a, FLOATXX b, float_status *status)
{
    return minmax_internal(a, b, status, MINMAX_MIN);
}

FLOATXX glue(FLOATXX,_maxnum)(FLOATXX a, FLOATXX b, float_status *status)
{
    return minmax_internal(a, b, status, MINMAX_MAX | MINMAX_IEEE);
}

FLOATXX glue(FLOATXX,_minnum)(FLOATXX a, FLOATXX b, float_status *status)
{
    return minmax_internal(a, b, status, MINMAX_MIN | MINMAX_IEEE);
}

FLOATXX glue(FLOATXX,_maxnummag)(FLOATXX a, FLOATXX b, float_status *status)
{
    return minmax_internal(a, b, status,
                           MINMAX_MAX | MINMAX_IEEE | MINMAX_MAG);
}

FLOATXX glue(FLOATXX,_minnummag)(FLOATXX a, FLOATXX b, float_status *status)
{
    return minmax_internal(a, b, status,
                           MINMAX_MIN | MINMAX_IEEE | MINMAX_MAG);
}

FLOATXX glue(FLOATXX,_round_to_int)(FLOATXX a, float_status *status)
{
    const int fracbits = glue(_FP_FRACBITS_, FS);
    const int wfracbits = glue(_FP_WFRACBITS_, FS);
    FP_DECL_EX;
    glue(FP_DECL_, FS)(A);
    int rshift, lshift;

    FP_INIT_ROUNDMODE;
    glue(FP_UNPACK_, FS)(A, a);

    switch (A_c) {
    case FP_CLS_INF:
        /* No fractional part, never any exceptions, return unchanged.  */
        return a;

    case FP_CLS_ZERO:
    case FP_CLS_NAN:
        /* No fractional part, but maybe exceptions.  In the cases of
           denormal-flush-to-zero and SNaN, we will have raised an
           exception during unpack.  For those, we need to go through
           repack in order to generate zero or silence the NaN.  */
        if (!FP_CUR_EXCEPTIONS) {
            return a;
        }
        break;

    case FP_CLS_NORMAL:
        /* Position the 2**0 bit at _FP_WORKBIT,
           where _FP_ROUND expects to work.  */
        rshift = fracbits - 1 - A_e;
        if (rshift <= 0) {
            /* Already integral, never any exceptions, return unchanged.  */
            return a;
        }
        if (rshift < wfracbits) {
            glue(_FP_FRAC_SRS_, WC)(A, rshift, wfracbits);
        } else {
            glue(_FP_FRAC_SET_, WC)(A, glue(_FP_MINFRAC_, WC));
        }
        FP_ROUND(WC, A);

        /* Drop the rounding bits.  Normally this is done via right-shift
           during the re-packing stage, but we need to put the rest of the
           fraction back into place.  */
        glue(_FP_FRAC_LOW_, WC)(A) &= ~(_FP_WORK_LSB - 1);

        /* Notice rounding to zero.  */
        if (glue(_FP_FRAC_ZEROP_, WC)(A)) {
            A_c = FP_CLS_ZERO;
            break;
        }

        /* Renormalize the fraction.  This takes care of both overflow
           and fixing up the fraction after the rshift.  */
        glue(_FP_FRAC_CLZ_, WC)(lshift, A);
        lshift -= glue(_FP_WFRACXBITS_, FS);
        assert(lshift >= 0);
        glue(_FP_FRAC_SLL_, WC)(A, lshift);

        A_e += rshift - lshift;
        break;

    default:
        _FP_UNREACHABLE;
    }

    glue(FP_PACK_, FS)(a, A);
    FP_HANDLE_EXCEPTIONS;

    return a;
}
