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
#define FP_ADD_INTERNAL(fs, wc, R, A, B, OP) \
    _FP_ADD_INTERNAL(fs, wc, R, A, B, '-')

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
