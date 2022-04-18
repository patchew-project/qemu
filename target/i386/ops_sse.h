/*
 *  MMX/3DNow!/SSE/SSE2/SSE3/SSSE3/SSE4/PNI support
 *
 *  Copyright (c) 2005 Fabrice Bellard
 *  Copyright (c) 2008 Intel Corporation  <andrew.zaborowski@intel.com>
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

#include "crypto/aes.h"

#if SHIFT == 0
#define Reg MMXReg
#define XMM_ONLY(...)
#define YMM_ONLY(...)
#define B(n) MMX_B(n)
#define W(n) MMX_W(n)
#define L(n) MMX_L(n)
#define Q(n) MMX_Q(n)
#define SUFFIX _mmx
#else
#define Reg ZMMReg
#define XMM_ONLY(...) __VA_ARGS__
#define B(n) ZMM_B(n)
#define W(n) ZMM_W(n)
#define L(n) ZMM_L(n)
#define Q(n) ZMM_Q(n)
#if SHIFT == 1
#define SUFFIX _xmm
#define YMM_ONLY(...)
#else
#define SUFFIX _ymm
#define YMM_ONLY(...) __VA_ARGS__
#endif
#endif

#if SHIFT == 0
#define SHIFT_HELPER_BODY(n, elem, F) do {      \
    d->elem(0) = F(s->elem(0), shift);          \
    if ((n) > 1) {                              \
        d->elem(1) = F(s->elem(1), shift);      \
    }                                           \
    if ((n) > 2) {                              \
        d->elem(2) = F(s->elem(2), shift);      \
        d->elem(3) = F(s->elem(3), shift);      \
    }                                           \
    if ((n) > 4) {                              \
        d->elem(4) = F(s->elem(4), shift);      \
        d->elem(5) = F(s->elem(5), shift);      \
        d->elem(6) = F(s->elem(6), shift);      \
        d->elem(7) = F(s->elem(7), shift);      \
    }                                           \
    if ((n) > 8) {                              \
        d->elem(8) = F(s->elem(8), shift);      \
        d->elem(9) = F(s->elem(9), shift);      \
        d->elem(10) = F(s->elem(10), shift);    \
        d->elem(11) = F(s->elem(11), shift);    \
        d->elem(12) = F(s->elem(12), shift);    \
        d->elem(13) = F(s->elem(13), shift);    \
        d->elem(14) = F(s->elem(14), shift);    \
        d->elem(15) = F(s->elem(15), shift);    \
    }                                           \
    } while (0)

#define FPSRL(x, c) ((x) >> shift)
#define FPSRAW(x, c) ((int16_t)(x) >> shift)
#define FPSRAL(x, c) ((int32_t)(x) >> shift)
#define FPSLL(x, c) ((x) << shift)
#endif

void glue(helper_psrlw, SUFFIX)(CPUX86State *env, Reg *d, Reg *s, Reg *c)
{
    int shift;
    if (c->Q(0) > 15) {
        d->Q(0) = 0;
        XMM_ONLY(d->Q(1) = 0;)
        YMM_ONLY(
                d->Q(2) = 0;
                d->Q(3) = 0;
                )
    } else {
        shift = c->B(0);
        SHIFT_HELPER_BODY(4 << SHIFT, W, FPSRL);
    }
}

void glue(helper_psllw, SUFFIX)(CPUX86State *env, Reg *d, Reg *s, Reg *c)
{
    int shift;
    if (c->Q(0) > 15) {
        d->Q(0) = 0;
        XMM_ONLY(d->Q(1) = 0;)
        YMM_ONLY(
                d->Q(2) = 0;
                d->Q(3) = 0;
                )
    } else {
        shift = c->B(0);
        SHIFT_HELPER_BODY(4 << SHIFT, W, FPSLL);
    }
}

void glue(helper_psraw, SUFFIX)(CPUX86State *env, Reg *d, Reg *s, Reg *c)
{
    int shift;
    if (c->Q(0) > 15) {
        shift = 15;
    } else {
        shift = c->B(0);
    }
    SHIFT_HELPER_BODY(4 << SHIFT, W, FPSRAW);
}

void glue(helper_psrld, SUFFIX)(CPUX86State *env, Reg *d, Reg *s, Reg *c)
{
    int shift;
    if (c->Q(0) > 31) {
        d->Q(0) = 0;
        XMM_ONLY(d->Q(1) = 0;)
        YMM_ONLY(
                d->Q(2) = 0;
                d->Q(3) = 0;
                )
    } else {
        shift = c->B(0);
        SHIFT_HELPER_BODY(2 << SHIFT, L, FPSRL);
    }
}

void glue(helper_pslld, SUFFIX)(CPUX86State *env, Reg *d, Reg *s, Reg *c)
{
    int shift;
    if (c->Q(0) > 31) {
        d->Q(0) = 0;
        XMM_ONLY(d->Q(1) = 0;)
        YMM_ONLY(
                d->Q(2) = 0;
                d->Q(3) = 0;
                )
    } else {
        shift = c->B(0);
        SHIFT_HELPER_BODY(2 << SHIFT, L, FPSLL);
    }
}

void glue(helper_psrad, SUFFIX)(CPUX86State *env, Reg *d, Reg *s, Reg *c)
{
    int shift;
    if (c->Q(0) > 31) {
        shift = 31;
    } else {
        shift = c->B(0);
    }
    SHIFT_HELPER_BODY(2 << SHIFT, L, FPSRAL);
}

void glue(helper_psrlq, SUFFIX)(CPUX86State *env, Reg *d, Reg *s, Reg *c)
{
    int shift;
    if (c->Q(0) > 63) {
        d->Q(0) = 0;
        XMM_ONLY(d->Q(1) = 0;)
        YMM_ONLY(
                d->Q(2) = 0;
                d->Q(3) = 0;
                )
    } else {
        shift = c->B(0);
        SHIFT_HELPER_BODY(1 << SHIFT, Q, FPSRL);
    }
}

void glue(helper_psllq, SUFFIX)(CPUX86State *env, Reg *d, Reg *s, Reg *c)
{
    int shift;
    if (c->Q(0) > 63) {
        d->Q(0) = 0;
        XMM_ONLY(d->Q(1) = 0;)
        YMM_ONLY(
                d->Q(2) = 0;
                d->Q(3) = 0;
                )
    } else {
        shift = c->B(0);
        SHIFT_HELPER_BODY(1 << SHIFT, Q, FPSLL);
    }
}

#if SHIFT >= 1
void glue(helper_psrldq, SUFFIX)(CPUX86State *env, Reg *d, Reg *s, Reg *c)
{
    int shift, i;

    shift = c->L(0);
    if (shift > 16) {
        shift = 16;
    }
    for (i = 0; i < 16 - shift; i++) {
        d->B(i) = s->B(i + shift);
    }
    for (i = 16 - shift; i < 16; i++) {
        d->B(i) = 0;
    }
#if SHIFT == 2
    for (i = 0; i < 16 - shift; i++) {
        d->B(i + 16) = s->B(i + 16 + shift);
    }
    for (i = 16 - shift; i < 16; i++) {
        d->B(i + 16) = 0;
    }
#endif
}

void glue(helper_pslldq, SUFFIX)(CPUX86State *env, Reg *d, Reg *s, Reg *c)
{
    int shift, i;

    shift = c->L(0);
    if (shift > 16) {
        shift = 16;
    }
    for (i = 15; i >= shift; i--) {
        d->B(i) = s->B(i - shift);
    }
    for (i = 0; i < shift; i++) {
        d->B(i) = 0;
    }
#if SHIFT == 2
    for (i = 15; i >= shift; i--) {
        d->B(i + 16) = s->B(i + 16 - shift);
    }
    for (i = 0; i < shift; i++) {
        d->B(i + 16) = 0;
    }
#endif
}
#endif

#define SSE_HELPER_1(name, elem, num, F)                                   \
    void glue(name, SUFFIX)(CPUX86State *env, Reg *d, Reg *s)   \
    {                                                           \
        d->elem(0) = F(s->elem(0));                             \
        d->elem(1) = F(s->elem(1));                             \
        if ((num << SHIFT) > 2) {                               \
            d->elem(2) = F(s->elem(2));                         \
            d->elem(3) = F(s->elem(3));                         \
        }                                                       \
        if ((num << SHIFT) > 4) {                               \
            d->elem(4) = F(s->elem(4));                         \
            d->elem(5) = F(s->elem(5));                         \
            d->elem(6) = F(s->elem(6));                         \
            d->elem(7) = F(s->elem(7));                         \
        }                                                       \
        if ((num << SHIFT) > 8) {                               \
            d->elem(8) = F(s->elem(8));                         \
            d->elem(9) = F(s->elem(9));                         \
            d->elem(10) = F(s->elem(10));                       \
            d->elem(11) = F(s->elem(11));                       \
            d->elem(12) = F(s->elem(12));                       \
            d->elem(13) = F(s->elem(13));                       \
            d->elem(14) = F(s->elem(14));                       \
            d->elem(15) = F(s->elem(15));                       \
        }                                                       \
        if ((num << SHIFT) > 16) {                              \
            d->elem(16) = F(s->elem(16));                       \
            d->elem(17) = F(s->elem(17));                       \
            d->elem(18) = F(s->elem(18));                       \
            d->elem(19) = F(s->elem(19));                       \
            d->elem(20) = F(s->elem(20));                       \
            d->elem(21) = F(s->elem(21));                       \
            d->elem(22) = F(s->elem(22));                       \
            d->elem(23) = F(s->elem(23));                       \
            d->elem(24) = F(s->elem(24));                       \
            d->elem(25) = F(s->elem(25));                       \
            d->elem(26) = F(s->elem(26));                       \
            d->elem(27) = F(s->elem(27));                       \
            d->elem(28) = F(s->elem(28));                       \
            d->elem(29) = F(s->elem(29));                       \
            d->elem(30) = F(s->elem(30));                       \
            d->elem(31) = F(s->elem(31));                       \
        }                                                       \
    }

#define SSE_HELPER_B(name, F)                                   \
    void glue(name, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s) \
    {                                                           \
        d->B(0) = F(v->B(0), s->B(0));                          \
        d->B(1) = F(v->B(1), s->B(1));                          \
        d->B(2) = F(v->B(2), s->B(2));                          \
        d->B(3) = F(v->B(3), s->B(3));                          \
        d->B(4) = F(v->B(4), s->B(4));                          \
        d->B(5) = F(v->B(5), s->B(5));                          \
        d->B(6) = F(v->B(6), s->B(6));                          \
        d->B(7) = F(v->B(7), s->B(7));                          \
        XMM_ONLY(                                               \
                 d->B(8) = F(v->B(8), s->B(8));                 \
                 d->B(9) = F(v->B(9), s->B(9));                 \
                 d->B(10) = F(v->B(10), s->B(10));              \
                 d->B(11) = F(v->B(11), s->B(11));              \
                 d->B(12) = F(v->B(12), s->B(12));              \
                 d->B(13) = F(v->B(13), s->B(13));              \
                 d->B(14) = F(v->B(14), s->B(14));              \
                 d->B(15) = F(v->B(15), s->B(15));              \
                                                        )       \
        YMM_ONLY(                                               \
                 d->B(16) = F(v->B(16), s->B(16));              \
                 d->B(17) = F(v->B(17), s->B(17));              \
                 d->B(18) = F(v->B(18), s->B(18));              \
                 d->B(19) = F(v->B(19), s->B(19));              \
                 d->B(20) = F(v->B(20), s->B(20));              \
                 d->B(21) = F(v->B(21), s->B(21));              \
                 d->B(22) = F(v->B(22), s->B(22));              \
                 d->B(23) = F(v->B(23), s->B(23));              \
                 d->B(24) = F(v->B(24), s->B(24));              \
                 d->B(25) = F(v->B(25), s->B(25));              \
                 d->B(26) = F(v->B(26), s->B(26));              \
                 d->B(27) = F(v->B(27), s->B(27));              \
                 d->B(28) = F(v->B(28), s->B(28));              \
                 d->B(29) = F(v->B(29), s->B(29));              \
                 d->B(30) = F(v->B(30), s->B(30));              \
                 d->B(31) = F(v->B(31), s->B(31));              \
                                                        )       \
            }

#define SSE_HELPER_W(name, F)                                   \
    void glue(name, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s) \
    {                                                           \
        d->W(0) = F(v->W(0), s->W(0));                          \
        d->W(1) = F(v->W(1), s->W(1));                          \
        d->W(2) = F(v->W(2), s->W(2));                          \
        d->W(3) = F(v->W(3), s->W(3));                          \
        XMM_ONLY(                                               \
                 d->W(4) = F(v->W(4), s->W(4));                 \
                 d->W(5) = F(v->W(5), s->W(5));                 \
                 d->W(6) = F(v->W(6), s->W(6));                 \
                 d->W(7) = F(v->W(7), s->W(7));                 \
                                                        )       \
        YMM_ONLY(                                               \
                 d->W(8) = F(v->W(8), s->W(8));                 \
                 d->W(9) = F(v->W(9), s->W(9));                 \
                 d->W(10) = F(v->W(10), s->W(10));              \
                 d->W(11) = F(v->W(11), s->W(11));              \
                 d->W(12) = F(v->W(12), s->W(12));              \
                 d->W(13) = F(v->W(13), s->W(13));              \
                 d->W(14) = F(v->W(14), s->W(14));              \
                 d->W(15) = F(v->W(15), s->W(15));              \
                                                        )       \
            }

#define SSE_HELPER_L(name, F)                                   \
    void glue(name, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s) \
    {                                                           \
        d->L(0) = F(v->L(0), s->L(0));                          \
        d->L(1) = F(v->L(1), s->L(1));                          \
        XMM_ONLY(                                               \
                 d->L(2) = F(v->L(2), s->L(2));                 \
                 d->L(3) = F(v->L(3), s->L(3));                 \
                                                        )       \
        YMM_ONLY(                                               \
                 d->L(4) = F(v->L(4), s->L(4));                 \
                 d->L(5) = F(v->L(5), s->L(5));                 \
                 d->L(6) = F(v->L(6), s->L(6));                 \
                 d->L(7) = F(v->L(7), s->L(7));                 \
                                                        )       \
            }

#define SSE_HELPER_Q(name, F)                                   \
    void glue(name, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s) \
    {                                                           \
        d->Q(0) = F(v->Q(0), s->Q(0));                          \
        XMM_ONLY(                                               \
                 d->Q(1) = F(v->Q(1), s->Q(1));                 \
                                                        )       \
        YMM_ONLY(                                               \
                 d->Q(2) = F(v->Q(2), s->Q(2));                 \
                 d->Q(3) = F(v->Q(3), s->Q(3));                 \
                                                        )       \
            }

#if SHIFT == 0
static inline int satub(int x)
{
    if (x < 0) {
        return 0;
    } else if (x > 255) {
        return 255;
    } else {
        return x;
    }
}

static inline int satuw(int x)
{
    if (x < 0) {
        return 0;
    } else if (x > 65535) {
        return 65535;
    } else {
        return x;
    }
}

static inline int satsb(int x)
{
    if (x < -128) {
        return -128;
    } else if (x > 127) {
        return 127;
    } else {
        return x;
    }
}

static inline int satsw(int x)
{
    if (x < -32768) {
        return -32768;
    } else if (x > 32767) {
        return 32767;
    } else {
        return x;
    }
}

#define FADD(a, b) ((a) + (b))
#define FADDUB(a, b) satub((a) + (b))
#define FADDUW(a, b) satuw((a) + (b))
#define FADDSB(a, b) satsb((int8_t)(a) + (int8_t)(b))
#define FADDSW(a, b) satsw((int16_t)(a) + (int16_t)(b))

#define FSUB(a, b) ((a) - (b))
#define FSUBUB(a, b) satub((a) - (b))
#define FSUBUW(a, b) satuw((a) - (b))
#define FSUBSB(a, b) satsb((int8_t)(a) - (int8_t)(b))
#define FSUBSW(a, b) satsw((int16_t)(a) - (int16_t)(b))
#define FMINUB(a, b) ((a) < (b)) ? (a) : (b)
#define FMINSW(a, b) ((int16_t)(a) < (int16_t)(b)) ? (a) : (b)
#define FMAXUB(a, b) ((a) > (b)) ? (a) : (b)
#define FMAXSW(a, b) ((int16_t)(a) > (int16_t)(b)) ? (a) : (b)

#define FAND(a, b) ((a) & (b))
#define FANDN(a, b) ((~(a)) & (b))
#define FOR(a, b) ((a) | (b))
#define FXOR(a, b) ((a) ^ (b))

#define FCMPGTB(a, b) ((int8_t)(a) > (int8_t)(b) ? -1 : 0)
#define FCMPGTW(a, b) ((int16_t)(a) > (int16_t)(b) ? -1 : 0)
#define FCMPGTL(a, b) ((int32_t)(a) > (int32_t)(b) ? -1 : 0)
#define FCMPEQ(a, b) ((a) == (b) ? -1 : 0)

#define FMULLW(a, b) ((a) * (b))
#define FMULHRW(a, b) (((int16_t)(a) * (int16_t)(b) + 0x8000) >> 16)
#define FMULHUW(a, b) ((a) * (b) >> 16)
#define FMULHW(a, b) ((int16_t)(a) * (int16_t)(b) >> 16)

#define FAVG(a, b) (((a) + (b) + 1) >> 1)
#endif

SSE_HELPER_B(helper_paddb, FADD)
SSE_HELPER_W(helper_paddw, FADD)
SSE_HELPER_L(helper_paddl, FADD)
SSE_HELPER_Q(helper_paddq, FADD)

SSE_HELPER_B(helper_psubb, FSUB)
SSE_HELPER_W(helper_psubw, FSUB)
SSE_HELPER_L(helper_psubl, FSUB)
SSE_HELPER_Q(helper_psubq, FSUB)

SSE_HELPER_B(helper_paddusb, FADDUB)
SSE_HELPER_B(helper_paddsb, FADDSB)
SSE_HELPER_B(helper_psubusb, FSUBUB)
SSE_HELPER_B(helper_psubsb, FSUBSB)

SSE_HELPER_W(helper_paddusw, FADDUW)
SSE_HELPER_W(helper_paddsw, FADDSW)
SSE_HELPER_W(helper_psubusw, FSUBUW)
SSE_HELPER_W(helper_psubsw, FSUBSW)

SSE_HELPER_B(helper_pminub, FMINUB)
SSE_HELPER_B(helper_pmaxub, FMAXUB)

SSE_HELPER_W(helper_pminsw, FMINSW)
SSE_HELPER_W(helper_pmaxsw, FMAXSW)

SSE_HELPER_Q(helper_pand, FAND)
SSE_HELPER_Q(helper_pandn, FANDN)
SSE_HELPER_Q(helper_por, FOR)
SSE_HELPER_Q(helper_pxor, FXOR)

SSE_HELPER_B(helper_pcmpgtb, FCMPGTB)
SSE_HELPER_W(helper_pcmpgtw, FCMPGTW)
SSE_HELPER_L(helper_pcmpgtl, FCMPGTL)

SSE_HELPER_B(helper_pcmpeqb, FCMPEQ)
SSE_HELPER_W(helper_pcmpeqw, FCMPEQ)
SSE_HELPER_L(helper_pcmpeql, FCMPEQ)

SSE_HELPER_W(helper_pmullw, FMULLW)
SSE_HELPER_W(helper_pmulhuw, FMULHUW)
SSE_HELPER_W(helper_pmulhw, FMULHW)

#if SHIFT == 0
void glue(helper_pmulhrw, SUFFIX)(CPUX86State *env, Reg *d, Reg *s)
{
    d->W(0) = FMULHRW(d->W(0), s->W(0));
    d->W(1) = FMULHRW(d->W(1), s->W(1));
    d->W(2) = FMULHRW(d->W(2), s->W(2));
    d->W(3) = FMULHRW(d->W(3), s->W(3));
}
#endif

SSE_HELPER_B(helper_pavgb, FAVG)
SSE_HELPER_W(helper_pavgw, FAVG)

void glue(helper_pmuludq, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    d->Q(0) = (uint64_t)s->L(0) * (uint64_t)v->L(0);
#if SHIFT >= 1
    d->Q(1) = (uint64_t)s->L(2) * (uint64_t)v->L(2);
#if SHIFT == 2
    d->Q(2) = (uint64_t)s->L(4) * (uint64_t)v->L(4);
    d->Q(3) = (uint64_t)s->L(6) * (uint64_t)v->L(6);
#endif
#endif
}

void glue(helper_pmaddwd, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    int i;

    for (i = 0; i < (2 << SHIFT); i++) {
        d->L(i) = (int16_t)s->W(2 * i) * (int16_t)v->W(2 * i) +
            (int16_t)s->W(2 * i + 1) * (int16_t)v->W(2 * i + 1);
    }
}

#if SHIFT == 0
static inline int abs1(int a)
{
    if (a < 0) {
        return -a;
    } else {
        return a;
    }
}
#endif
void glue(helper_psadbw, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    unsigned int val;

    val = 0;
    val += abs1(v->B(0) - s->B(0));
    val += abs1(v->B(1) - s->B(1));
    val += abs1(v->B(2) - s->B(2));
    val += abs1(v->B(3) - s->B(3));
    val += abs1(v->B(4) - s->B(4));
    val += abs1(v->B(5) - s->B(5));
    val += abs1(v->B(6) - s->B(6));
    val += abs1(v->B(7) - s->B(7));
    d->Q(0) = val;
#if SHIFT >= 1
    val = 0;
    val += abs1(v->B(8) - s->B(8));
    val += abs1(v->B(9) - s->B(9));
    val += abs1(v->B(10) - s->B(10));
    val += abs1(v->B(11) - s->B(11));
    val += abs1(v->B(12) - s->B(12));
    val += abs1(v->B(13) - s->B(13));
    val += abs1(v->B(14) - s->B(14));
    val += abs1(v->B(15) - s->B(15));
    d->Q(1) = val;
#if SHIFT == 2
    val = 0;
    val += abs1(v->B(16) - s->B(16));
    val += abs1(v->B(17) - s->B(17));
    val += abs1(v->B(18) - s->B(18));
    val += abs1(v->B(19) - s->B(19));
    val += abs1(v->B(20) - s->B(20));
    val += abs1(v->B(21) - s->B(21));
    val += abs1(v->B(22) - s->B(22));
    val += abs1(v->B(23) - s->B(23));
    d->Q(2) = val;
    val = 0;
    val += abs1(v->B(24) - s->B(24));
    val += abs1(v->B(25) - s->B(25));
    val += abs1(v->B(26) - s->B(26));
    val += abs1(v->B(27) - s->B(27));
    val += abs1(v->B(28) - s->B(28));
    val += abs1(v->B(29) - s->B(29));
    val += abs1(v->B(30) - s->B(30));
    val += abs1(v->B(31) - s->B(31));
    d->Q(3) = val;
#endif
#endif
}

#if SHIFT < 2
void glue(helper_maskmov, SUFFIX)(CPUX86State *env, Reg *d, Reg *s,
                                  target_ulong a0)
{
    int i;

    for (i = 0; i < (8 << SHIFT); i++) {
        if (s->B(i) & 0x80) {
            cpu_stb_data_ra(env, a0 + i, d->B(i), GETPC());
        }
    }
}
#endif

void glue(helper_movl_mm_T0, SUFFIX)(Reg *d, uint32_t val)
{
    d->L(0) = val;
    d->L(1) = 0;
#if SHIFT >= 1
    d->Q(1) = 0;
#if SHIFT == 2
    d->Q(2) = 0;
    d->Q(3) = 0;
#endif
#endif
}

#ifdef TARGET_X86_64
void glue(helper_movq_mm_T0, SUFFIX)(Reg *d, uint64_t val)
{
    d->Q(0) = val;
#if SHIFT >= 1
    d->Q(1) = 0;
#if SHIFT == 2
    d->Q(2) = 0;
    d->Q(3) = 0;
#endif
#endif
}
#endif

#define SHUFFLE4(F, a, b, offset) do {      \
    r0 = a->F((order & 3) + offset);        \
    r1 = a->F(((order >> 2) & 3) + offset); \
    r2 = b->F(((order >> 4) & 3) + offset); \
    r3 = b->F(((order >> 6) & 3) + offset); \
    d->F(offset) = r0;                      \
    d->F(offset + 1) = r1;                  \
    d->F(offset + 2) = r2;                  \
    d->F(offset + 3) = r3;                  \
    } while (0)

#if SHIFT == 0
void glue(helper_pshufw, SUFFIX)(Reg *d, Reg *s, int order)
{
    uint16_t r0, r1, r2, r3;

    SHUFFLE4(W, s, s, 0);
}
#else
void glue(helper_shufps, SUFFIX)(Reg *d, Reg *v, Reg *s, int order)
{
    uint32_t r0, r1, r2, r3;

    SHUFFLE4(L, v, s, 0);
#if SHIFT == 2
    SHUFFLE4(L, v, s, 4);
#endif
}

void glue(helper_shufpd, SUFFIX)(Reg *d, Reg *v, Reg *s, int order)
{
    uint64_t r0, r1;

    r0 = v->Q(order & 1);
    r1 = s->Q((order >> 1) & 1);
    d->Q(0) = r0;
    d->Q(1) = r1;
#if SHIFT == 2
    r0 = v->Q(((order >> 2) & 1) + 2);
    r1 = s->Q(((order >> 3) & 1) + 2);
    d->Q(2) = r0;
    d->Q(3) = r1;
#endif
}

void glue(helper_pshufd, SUFFIX)(Reg *d, Reg *s, int order)
{
    uint32_t r0, r1, r2, r3;

    SHUFFLE4(L, s, s, 0);
#if SHIFT ==  2
    SHUFFLE4(L, s, s, 4);
#endif
}

void glue(helper_pshuflw, SUFFIX)(Reg *d, Reg *s, int order)
{
    uint16_t r0, r1, r2, r3;

    SHUFFLE4(W, s, s, 0);
    d->Q(1) = s->Q(1);
#if SHIFT == 2
    SHUFFLE4(W, s, s, 8);
    d->Q(3) = s->Q(3);
#endif
}

void glue(helper_pshufhw, SUFFIX)(Reg *d, Reg *s, int order)
{
    uint16_t r0, r1, r2, r3;

    d->Q(0) = s->Q(0);
    SHUFFLE4(W, s, s, 4);
#if SHIFT == 2
    d->Q(2) = s->Q(2);
    SHUFFLE4(W, s, s, 12);
#endif
}
#endif

#if SHIFT >= 1
/* FPU ops */
/* XXX: not accurate */

#define SSE_HELPER_P(name, F)                                           \
    void glue(helper_ ## name ## ps, SUFFIX)(CPUX86State *env,          \
            Reg *d, Reg *v, Reg *s)                                     \
    {                                                                   \
        d->ZMM_S(0) = F(32, v->ZMM_S(0), s->ZMM_S(0));                  \
        d->ZMM_S(1) = F(32, v->ZMM_S(1), s->ZMM_S(1));                  \
        d->ZMM_S(2) = F(32, v->ZMM_S(2), s->ZMM_S(2));                  \
        d->ZMM_S(3) = F(32, v->ZMM_S(3), s->ZMM_S(3));                  \
        YMM_ONLY(                                                       \
        d->ZMM_S(4) = F(32, v->ZMM_S(4), s->ZMM_S(4));                  \
        d->ZMM_S(5) = F(32, v->ZMM_S(5), s->ZMM_S(5));                  \
        d->ZMM_S(6) = F(32, v->ZMM_S(6), s->ZMM_S(6));                  \
        d->ZMM_S(7) = F(32, v->ZMM_S(7), s->ZMM_S(7));                  \
        )                                                               \
    }                                                                   \
                                                                        \
    void glue(helper_ ## name ## pd, SUFFIX)(CPUX86State *env,          \
            Reg *d, Reg *v, Reg *s)                                     \
    {                                                                   \
        d->ZMM_D(0) = F(64, v->ZMM_D(0), s->ZMM_D(0));                  \
        d->ZMM_D(1) = F(64, v->ZMM_D(1), s->ZMM_D(1));                  \
        YMM_ONLY(                                                       \
        d->ZMM_D(2) = F(64, v->ZMM_D(2), s->ZMM_D(2));                  \
        d->ZMM_D(3) = F(64, v->ZMM_D(3), s->ZMM_D(3));                  \
        )                                                               \
    }

#if SHIFT == 1

#define SSE_HELPER_S(name, F)                                           \
    SSE_HELPER_P(name, F)                                               \
                                                                        \
    void helper_ ## name ## ss(CPUX86State *env, Reg *d, Reg *v, Reg *s)\
    {                                                                   \
        d->ZMM_S(0) = F(32, v->ZMM_S(0), s->ZMM_S(0));                  \
    }                                                                   \
                                                                        \
    void helper_ ## name ## sd(CPUX86State *env, Reg *d, Reg *v, Reg *s)\
    {                                                                   \
        d->ZMM_D(0) = F(64, v->ZMM_D(0), s->ZMM_D(0));                  \
    }

#else

#define SSE_HELPER_S(name, F) SSE_HELPER_P(name, F)

#endif

#define FPU_ADD(size, a, b) float ## size ## _add(a, b, &env->sse_status)
#define FPU_SUB(size, a, b) float ## size ## _sub(a, b, &env->sse_status)
#define FPU_MUL(size, a, b) float ## size ## _mul(a, b, &env->sse_status)
#define FPU_DIV(size, a, b) float ## size ## _div(a, b, &env->sse_status)

/* Note that the choice of comparison op here is important to get the
 * special cases right: for min and max Intel specifies that (-0,0),
 * (NaN, anything) and (anything, NaN) return the second argument.
 */
#define FPU_MIN(size, a, b)                                     \
    (float ## size ## _lt(a, b, &env->sse_status) ? (a) : (b))
#define FPU_MAX(size, a, b)                                     \
    (float ## size ## _lt(b, a, &env->sse_status) ? (a) : (b))

SSE_HELPER_S(add, FPU_ADD)
SSE_HELPER_S(sub, FPU_SUB)
SSE_HELPER_S(mul, FPU_MUL)
SSE_HELPER_S(div, FPU_DIV)
SSE_HELPER_S(min, FPU_MIN)
SSE_HELPER_S(max, FPU_MAX)

void glue(helper_sqrtps, SUFFIX)(CPUX86State *env, Reg *d, Reg *s)
{
    d->ZMM_S(0) = float32_sqrt(s->ZMM_S(0), &env->sse_status);
    d->ZMM_S(1) = float32_sqrt(s->ZMM_S(1), &env->sse_status);
    d->ZMM_S(2) = float32_sqrt(s->ZMM_S(2), &env->sse_status);
    d->ZMM_S(3) = float32_sqrt(s->ZMM_S(3), &env->sse_status);
#if SHIFT == 2
    d->ZMM_S(4) = float32_sqrt(s->ZMM_S(4), &env->sse_status);
    d->ZMM_S(5) = float32_sqrt(s->ZMM_S(5), &env->sse_status);
    d->ZMM_S(6) = float32_sqrt(s->ZMM_S(6), &env->sse_status);
    d->ZMM_S(7) = float32_sqrt(s->ZMM_S(7), &env->sse_status);
#endif
}

void glue(helper_sqrtpd, SUFFIX)(CPUX86State *env, Reg *d, Reg *s)
{
    d->ZMM_D(0) = float64_sqrt(s->ZMM_D(0), &env->sse_status);
    d->ZMM_D(1) = float64_sqrt(s->ZMM_D(1), &env->sse_status);
#if SHIFT == 2
    d->ZMM_D(2) = float64_sqrt(s->ZMM_D(2), &env->sse_status);
    d->ZMM_D(3) = float64_sqrt(s->ZMM_D(3), &env->sse_status);
#endif
}

#if SHIFT == 1
void helper_sqrtss(CPUX86State *env, Reg *d, Reg *s)
{
    d->ZMM_S(0) = float32_sqrt(s->ZMM_S(0), &env->sse_status);
}

void helper_sqrtsd(CPUX86State *env, Reg *d, Reg *s)
{
    d->ZMM_D(0) = float64_sqrt(s->ZMM_D(0), &env->sse_status);
}
#endif

/* float to float conversions */
void glue(helper_cvtps2pd, SUFFIX)(CPUX86State *env, Reg *d, Reg *s)
{
    float32 s0, s1;

    s0 = s->ZMM_S(0);
    s1 = s->ZMM_S(1);
#if SHIFT == 2
    float32 s2, s3;
    s2 = s->ZMM_S(2);
    s3 = s->ZMM_S(3);
    d->ZMM_D(2) = float32_to_float64(s2, &env->sse_status);
    d->ZMM_D(3) = float32_to_float64(s3, &env->sse_status);
#endif
    d->ZMM_D(0) = float32_to_float64(s0, &env->sse_status);
    d->ZMM_D(1) = float32_to_float64(s1, &env->sse_status);
}

void glue(helper_cvtpd2ps, SUFFIX)(CPUX86State *env, Reg *d, Reg *s)
{
    d->ZMM_S(0) = float64_to_float32(s->ZMM_D(0), &env->sse_status);
    d->ZMM_S(1) = float64_to_float32(s->ZMM_D(1), &env->sse_status);
#if SHIFT == 2
    d->ZMM_S(2) = float64_to_float32(s->ZMM_D(2), &env->sse_status);
    d->ZMM_S(3) = float64_to_float32(s->ZMM_D(3), &env->sse_status);
    d->Q(2) = 0;
    d->Q(3) = 0;
#else
    d->Q(1) = 0;
#endif
}

#if SHIFT == 1
void helper_cvtss2sd(CPUX86State *env, Reg *d, Reg *s)
{
    d->ZMM_D(0) = float32_to_float64(s->ZMM_S(0), &env->sse_status);
}

void helper_cvtsd2ss(CPUX86State *env, Reg *d, Reg *s)
{
    d->ZMM_S(0) = float64_to_float32(s->ZMM_D(0), &env->sse_status);
}
#endif

/* integer to float */
void glue(helper_cvtdq2ps, SUFFIX)(CPUX86State *env, Reg *d, Reg *s)
{
    d->ZMM_S(0) = int32_to_float32(s->ZMM_L(0), &env->sse_status);
    d->ZMM_S(1) = int32_to_float32(s->ZMM_L(1), &env->sse_status);
    d->ZMM_S(2) = int32_to_float32(s->ZMM_L(2), &env->sse_status);
    d->ZMM_S(3) = int32_to_float32(s->ZMM_L(3), &env->sse_status);
#if SHIFT == 2
    d->ZMM_S(4) = int32_to_float32(s->ZMM_L(4), &env->sse_status);
    d->ZMM_S(5) = int32_to_float32(s->ZMM_L(5), &env->sse_status);
    d->ZMM_S(6) = int32_to_float32(s->ZMM_L(6), &env->sse_status);
    d->ZMM_S(7) = int32_to_float32(s->ZMM_L(7), &env->sse_status);
#endif
}

void glue(helper_cvtdq2pd, SUFFIX)(CPUX86State *env, Reg *d, Reg *s)
{
    int32_t l0, l1;

    l0 = (int32_t)s->ZMM_L(0);
    l1 = (int32_t)s->ZMM_L(1);
#if SHIFT == 2
    int32_t l2, l3;
    l2 = (int32_t)s->ZMM_L(2);
    l3 = (int32_t)s->ZMM_L(3);
    d->ZMM_D(2) = int32_to_float64(l2, &env->sse_status);
    d->ZMM_D(3) = int32_to_float64(l3, &env->sse_status);
#endif
    d->ZMM_D(0) = int32_to_float64(l0, &env->sse_status);
    d->ZMM_D(1) = int32_to_float64(l1, &env->sse_status);
}

#if SHIFT == 1
void helper_cvtpi2ps(CPUX86State *env, ZMMReg *d, MMXReg *s)
{
    d->ZMM_S(0) = int32_to_float32(s->MMX_L(0), &env->sse_status);
    d->ZMM_S(1) = int32_to_float32(s->MMX_L(1), &env->sse_status);
}

void helper_cvtpi2pd(CPUX86State *env, ZMMReg *d, MMXReg *s)
{
    d->ZMM_D(0) = int32_to_float64(s->MMX_L(0), &env->sse_status);
    d->ZMM_D(1) = int32_to_float64(s->MMX_L(1), &env->sse_status);
}

void helper_cvtsi2ss(CPUX86State *env, ZMMReg *d, uint32_t val)
{
    d->ZMM_S(0) = int32_to_float32(val, &env->sse_status);
}

void helper_cvtsi2sd(CPUX86State *env, ZMMReg *d, uint32_t val)
{
    d->ZMM_D(0) = int32_to_float64(val, &env->sse_status);
}

#ifdef TARGET_X86_64
void helper_cvtsq2ss(CPUX86State *env, ZMMReg *d, uint64_t val)
{
    d->ZMM_S(0) = int64_to_float32(val, &env->sse_status);
}

void helper_cvtsq2sd(CPUX86State *env, ZMMReg *d, uint64_t val)
{
    d->ZMM_D(0) = int64_to_float64(val, &env->sse_status);
}
#endif

#endif

/* float to integer */

#if SHIFT == 1
/*
 * x86 mandates that we return the indefinite integer value for the result
 * of any float-to-integer conversion that raises the 'invalid' exception.
 * Wrap the softfloat functions to get this behaviour.
 */
#define WRAP_FLOATCONV(RETTYPE, FN, FLOATTYPE, INDEFVALUE)              \
    static inline RETTYPE x86_##FN(FLOATTYPE a, float_status *s)        \
    {                                                                   \
        int oldflags, newflags;                                         \
        RETTYPE r;                                                      \
                                                                        \
        oldflags = get_float_exception_flags(s);                        \
        set_float_exception_flags(0, s);                                \
        r = FN(a, s);                                                   \
        newflags = get_float_exception_flags(s);                        \
        if (newflags & float_flag_invalid) {                            \
            r = INDEFVALUE;                                             \
        }                                                               \
        set_float_exception_flags(newflags | oldflags, s);              \
        return r;                                                       \
    }

WRAP_FLOATCONV(int32_t, float32_to_int32, float32, INT32_MIN)
WRAP_FLOATCONV(int32_t, float32_to_int32_round_to_zero, float32, INT32_MIN)
WRAP_FLOATCONV(int32_t, float64_to_int32, float64, INT32_MIN)
WRAP_FLOATCONV(int32_t, float64_to_int32_round_to_zero, float64, INT32_MIN)
WRAP_FLOATCONV(int64_t, float32_to_int64, float32, INT64_MIN)
WRAP_FLOATCONV(int64_t, float32_to_int64_round_to_zero, float32, INT64_MIN)
WRAP_FLOATCONV(int64_t, float64_to_int64, float64, INT64_MIN)
WRAP_FLOATCONV(int64_t, float64_to_int64_round_to_zero, float64, INT64_MIN)
#endif

void glue(helper_cvtps2dq, SUFFIX)(CPUX86State *env, ZMMReg *d, ZMMReg *s)
{
    d->ZMM_L(0) = x86_float32_to_int32(s->ZMM_S(0), &env->sse_status);
    d->ZMM_L(1) = x86_float32_to_int32(s->ZMM_S(1), &env->sse_status);
    d->ZMM_L(2) = x86_float32_to_int32(s->ZMM_S(2), &env->sse_status);
    d->ZMM_L(3) = x86_float32_to_int32(s->ZMM_S(3), &env->sse_status);
#if SHIFT == 2
    d->ZMM_L(4) = x86_float32_to_int32(s->ZMM_S(4), &env->sse_status);
    d->ZMM_L(5) = x86_float32_to_int32(s->ZMM_S(5), &env->sse_status);
    d->ZMM_L(6) = x86_float32_to_int32(s->ZMM_S(6), &env->sse_status);
    d->ZMM_L(7) = x86_float32_to_int32(s->ZMM_S(7), &env->sse_status);
#endif
}

void glue(helper_cvtpd2dq, SUFFIX)(CPUX86State *env, ZMMReg *d, ZMMReg *s)
{
    d->ZMM_L(0) = x86_float64_to_int32(s->ZMM_D(0), &env->sse_status);
    d->ZMM_L(1) = x86_float64_to_int32(s->ZMM_D(1), &env->sse_status);
#if SHIFT == 2
    d->ZMM_L(2) = x86_float64_to_int32(s->ZMM_D(2), &env->sse_status);
    d->ZMM_L(3) = x86_float64_to_int32(s->ZMM_D(3), &env->sse_status);
    d->Q(2) = 0;
    d->Q(3) = 0;
#else
    d->ZMM_Q(1) = 0;
#endif
}

#if SHIFT == 1
void helper_cvtps2pi(CPUX86State *env, MMXReg *d, ZMMReg *s)
{
    d->MMX_L(0) = x86_float32_to_int32(s->ZMM_S(0), &env->sse_status);
    d->MMX_L(1) = x86_float32_to_int32(s->ZMM_S(1), &env->sse_status);
}

void helper_cvtpd2pi(CPUX86State *env, MMXReg *d, ZMMReg *s)
{
    d->MMX_L(0) = x86_float64_to_int32(s->ZMM_D(0), &env->sse_status);
    d->MMX_L(1) = x86_float64_to_int32(s->ZMM_D(1), &env->sse_status);
}

int32_t helper_cvtss2si(CPUX86State *env, ZMMReg *s)
{
    return x86_float32_to_int32(s->ZMM_S(0), &env->sse_status);
}

int32_t helper_cvtsd2si(CPUX86State *env, ZMMReg *s)
{
    return x86_float64_to_int32(s->ZMM_D(0), &env->sse_status);
}

#ifdef TARGET_X86_64
int64_t helper_cvtss2sq(CPUX86State *env, ZMMReg *s)
{
    return x86_float32_to_int64(s->ZMM_S(0), &env->sse_status);
}

int64_t helper_cvtsd2sq(CPUX86State *env, ZMMReg *s)
{
    return x86_float64_to_int64(s->ZMM_D(0), &env->sse_status);
}
#endif
#endif

/* float to integer truncated */
void glue(helper_cvttps2dq, SUFFIX)(CPUX86State *env, ZMMReg *d, ZMMReg *s)
{
    d->ZMM_L(0) = x86_float32_to_int32_round_to_zero(s->ZMM_S(0),
                                                     &env->sse_status);
    d->ZMM_L(1) = x86_float32_to_int32_round_to_zero(s->ZMM_S(1),
                                                     &env->sse_status);
    d->ZMM_L(2) = x86_float32_to_int32_round_to_zero(s->ZMM_S(2),
                                                     &env->sse_status);
    d->ZMM_L(3) = x86_float32_to_int32_round_to_zero(s->ZMM_S(3),
                                                     &env->sse_status);
#if SHIFT == 2
    d->ZMM_L(4) = x86_float32_to_int32_round_to_zero(s->ZMM_S(4),
                                                     &env->sse_status);
    d->ZMM_L(5) = x86_float32_to_int32_round_to_zero(s->ZMM_S(5),
                                                     &env->sse_status);
    d->ZMM_L(6) = x86_float32_to_int32_round_to_zero(s->ZMM_S(6),
                                                     &env->sse_status);
    d->ZMM_L(7) = x86_float32_to_int32_round_to_zero(s->ZMM_S(7),
                                                     &env->sse_status);
#endif
}

void glue(helper_cvttpd2dq, SUFFIX)(CPUX86State *env, ZMMReg *d, ZMMReg *s)
{
    d->ZMM_L(0) = x86_float64_to_int32_round_to_zero(s->ZMM_D(0),
                                                     &env->sse_status);
    d->ZMM_L(1) = x86_float64_to_int32_round_to_zero(s->ZMM_D(1),
                                                     &env->sse_status);
#if SHIFT == 2
    d->ZMM_L(2) = x86_float64_to_int32_round_to_zero(s->ZMM_D(2),
                                                     &env->sse_status);
    d->ZMM_L(3) = x86_float64_to_int32_round_to_zero(s->ZMM_D(3),
                                                     &env->sse_status);
    d->ZMM_Q(2) = 0;
    d->ZMM_Q(3) = 0;
#else
    d->ZMM_Q(1) = 0;
#endif
}

#if SHIFT == 1
void helper_cvttps2pi(CPUX86State *env, MMXReg *d, ZMMReg *s)
{
    d->MMX_L(0) = x86_float32_to_int32_round_to_zero(s->ZMM_S(0),
                                                     &env->sse_status);
    d->MMX_L(1) = x86_float32_to_int32_round_to_zero(s->ZMM_S(1),
                                                     &env->sse_status);
}

void helper_cvttpd2pi(CPUX86State *env, MMXReg *d, ZMMReg *s)
{
    d->MMX_L(0) = x86_float64_to_int32_round_to_zero(s->ZMM_D(0),
                                                     &env->sse_status);
    d->MMX_L(1) = x86_float64_to_int32_round_to_zero(s->ZMM_D(1),
                                                     &env->sse_status);
}

int32_t helper_cvttss2si(CPUX86State *env, ZMMReg *s)
{
    return x86_float32_to_int32_round_to_zero(s->ZMM_S(0), &env->sse_status);
}

int32_t helper_cvttsd2si(CPUX86State *env, ZMMReg *s)
{
    return x86_float64_to_int32_round_to_zero(s->ZMM_D(0), &env->sse_status);
}

#ifdef TARGET_X86_64
int64_t helper_cvttss2sq(CPUX86State *env, ZMMReg *s)
{
    return x86_float32_to_int64_round_to_zero(s->ZMM_S(0), &env->sse_status);
}

int64_t helper_cvttsd2sq(CPUX86State *env, ZMMReg *s)
{
    return x86_float64_to_int64_round_to_zero(s->ZMM_D(0), &env->sse_status);
}
#endif
#endif

void glue(helper_rsqrtps, SUFFIX)(CPUX86State *env, ZMMReg *d, ZMMReg *s)
{
    uint8_t old_flags = get_float_exception_flags(&env->sse_status);
    d->ZMM_S(0) = float32_div(float32_one,
                              float32_sqrt(s->ZMM_S(0), &env->sse_status),
                              &env->sse_status);
    d->ZMM_S(1) = float32_div(float32_one,
                              float32_sqrt(s->ZMM_S(1), &env->sse_status),
                              &env->sse_status);
    d->ZMM_S(2) = float32_div(float32_one,
                              float32_sqrt(s->ZMM_S(2), &env->sse_status),
                              &env->sse_status);
    d->ZMM_S(3) = float32_div(float32_one,
                              float32_sqrt(s->ZMM_S(3), &env->sse_status),
                              &env->sse_status);
#if SHIFT == 2
    d->ZMM_S(4) = float32_div(float32_one,
                              float32_sqrt(s->ZMM_S(4), &env->sse_status),
                              &env->sse_status);
    d->ZMM_S(5) = float32_div(float32_one,
                              float32_sqrt(s->ZMM_S(5), &env->sse_status),
                              &env->sse_status);
    d->ZMM_S(6) = float32_div(float32_one,
                              float32_sqrt(s->ZMM_S(6), &env->sse_status),
                              &env->sse_status);
    d->ZMM_S(7) = float32_div(float32_one,
                              float32_sqrt(s->ZMM_S(7), &env->sse_status),
                              &env->sse_status);
#endif
    set_float_exception_flags(old_flags, &env->sse_status);
}

#if SHIFT == 1
void helper_rsqrtss(CPUX86State *env, ZMMReg *d, ZMMReg *s)
{
    uint8_t old_flags = get_float_exception_flags(&env->sse_status);
    d->ZMM_S(0) = float32_div(float32_one,
                              float32_sqrt(s->ZMM_S(0), &env->sse_status),
                              &env->sse_status);
    set_float_exception_flags(old_flags, &env->sse_status);
}
#endif

void glue(helper_rcpps, SUFFIX)(CPUX86State *env, ZMMReg *d, ZMMReg *s)
{
    uint8_t old_flags = get_float_exception_flags(&env->sse_status);
    d->ZMM_S(0) = float32_div(float32_one, s->ZMM_S(0), &env->sse_status);
    d->ZMM_S(1) = float32_div(float32_one, s->ZMM_S(1), &env->sse_status);
    d->ZMM_S(2) = float32_div(float32_one, s->ZMM_S(2), &env->sse_status);
    d->ZMM_S(3) = float32_div(float32_one, s->ZMM_S(3), &env->sse_status);
#if SHIFT == 2
    d->ZMM_S(4) = float32_div(float32_one, s->ZMM_S(4), &env->sse_status);
    d->ZMM_S(5) = float32_div(float32_one, s->ZMM_S(5), &env->sse_status);
    d->ZMM_S(6) = float32_div(float32_one, s->ZMM_S(6), &env->sse_status);
    d->ZMM_S(7) = float32_div(float32_one, s->ZMM_S(7), &env->sse_status);
#endif
    set_float_exception_flags(old_flags, &env->sse_status);
}

#if SHIFT == 1
void helper_rcpss(CPUX86State *env, ZMMReg *d, ZMMReg *s)
{
    uint8_t old_flags = get_float_exception_flags(&env->sse_status);
    d->ZMM_S(0) = float32_div(float32_one, s->ZMM_S(0), &env->sse_status);
    set_float_exception_flags(old_flags, &env->sse_status);
}
#endif

#if SHIFT == 1
static inline uint64_t helper_extrq(uint64_t src, int shift, int len)
{
    uint64_t mask;

    if (len == 0) {
        mask = ~0LL;
    } else {
        mask = (1ULL << len) - 1;
    }
    return (src >> shift) & mask;
}

void helper_extrq_r(CPUX86State *env, ZMMReg *d, ZMMReg *s)
{
    d->ZMM_Q(0) = helper_extrq(d->ZMM_Q(0), s->ZMM_B(1), s->ZMM_B(0));
}

void helper_extrq_i(CPUX86State *env, ZMMReg *d, int index, int length)
{
    d->ZMM_Q(0) = helper_extrq(d->ZMM_Q(0), index, length);
}

static inline uint64_t helper_insertq(uint64_t src, int shift, int len)
{
    uint64_t mask;

    if (len == 0) {
        mask = ~0ULL;
    } else {
        mask = (1ULL << len) - 1;
    }
    return (src & ~(mask << shift)) | ((src & mask) << shift);
}

void helper_insertq_r(CPUX86State *env, ZMMReg *d, ZMMReg *s)
{
    d->ZMM_Q(0) = helper_insertq(s->ZMM_Q(0), s->ZMM_B(9), s->ZMM_B(8));
}

void helper_insertq_i(CPUX86State *env, ZMMReg *d, int index, int length)
{
    d->ZMM_Q(0) = helper_insertq(d->ZMM_Q(0), index, length);
}
#endif

void glue(helper_haddps, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    float32 r0, r1, r2, r3;

    r0 = float32_add(v->ZMM_S(0), v->ZMM_S(1), &env->sse_status);
    r1 = float32_add(v->ZMM_S(2), v->ZMM_S(3), &env->sse_status);
    r2 = float32_add(s->ZMM_S(0), s->ZMM_S(1), &env->sse_status);
    r3 = float32_add(s->ZMM_S(2), s->ZMM_S(3), &env->sse_status);
    d->ZMM_S(0) = r0;
    d->ZMM_S(1) = r1;
    d->ZMM_S(2) = r2;
    d->ZMM_S(3) = r3;
#if SHIFT == 2
    r0 = float32_add(v->ZMM_S(4), v->ZMM_S(5), &env->sse_status);
    r1 = float32_add(v->ZMM_S(6), v->ZMM_S(7), &env->sse_status);
    r2 = float32_add(s->ZMM_S(4), s->ZMM_S(5), &env->sse_status);
    r3 = float32_add(s->ZMM_S(6), s->ZMM_S(7), &env->sse_status);
    d->ZMM_S(4) = r0;
    d->ZMM_S(5) = r1;
    d->ZMM_S(6) = r2;
    d->ZMM_S(7) = r3;
#endif
}

void glue(helper_haddpd, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    float64 r0, r1;

    r0 = float64_add(v->ZMM_D(0), v->ZMM_D(1), &env->sse_status);
    r1 = float64_add(s->ZMM_D(0), s->ZMM_D(1), &env->sse_status);
    d->ZMM_D(0) = r0;
    d->ZMM_D(1) = r1;
#if SHIFT == 2
    r0 = float64_add(v->ZMM_D(2), v->ZMM_D(3), &env->sse_status);
    r1 = float64_add(s->ZMM_D(2), s->ZMM_D(3), &env->sse_status);
    d->ZMM_D(2) = r0;
    d->ZMM_D(3) = r1;
#endif
}

void glue(helper_hsubps, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    float32 r0, r1, r2, r3;

    r0 = float32_sub(v->ZMM_S(0), v->ZMM_S(1), &env->sse_status);
    r1 = float32_sub(v->ZMM_S(2), v->ZMM_S(3), &env->sse_status);
    r2 = float32_sub(s->ZMM_S(0), s->ZMM_S(1), &env->sse_status);
    r3 = float32_sub(s->ZMM_S(2), s->ZMM_S(3), &env->sse_status);
    d->ZMM_S(0) = r0;
    d->ZMM_S(1) = r1;
    d->ZMM_S(2) = r2;
    d->ZMM_S(3) = r3;
#if SHIFT == 2
    r0 = float32_sub(v->ZMM_S(4), v->ZMM_S(5), &env->sse_status);
    r1 = float32_sub(v->ZMM_S(6), v->ZMM_S(7), &env->sse_status);
    r2 = float32_sub(s->ZMM_S(4), s->ZMM_S(5), &env->sse_status);
    r3 = float32_sub(s->ZMM_S(6), s->ZMM_S(7), &env->sse_status);
    d->ZMM_S(4) = r0;
    d->ZMM_S(5) = r1;
    d->ZMM_S(6) = r2;
    d->ZMM_S(7) = r3;
#endif
}

void glue(helper_hsubpd, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    float64 r0, r1;

    r0 = float64_sub(v->ZMM_D(0), v->ZMM_D(1), &env->sse_status);
    r1 = float64_sub(s->ZMM_D(0), s->ZMM_D(1), &env->sse_status);
    d->ZMM_D(0) = r0;
    d->ZMM_D(1) = r1;
#if SHIFT == 2
    r0 = float64_sub(v->ZMM_D(2), v->ZMM_D(3), &env->sse_status);
    r1 = float64_sub(s->ZMM_D(2), s->ZMM_D(3), &env->sse_status);
    d->ZMM_D(2) = r0;
    d->ZMM_D(3) = r1;
#endif
}

void glue(helper_addsubps, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    d->ZMM_S(0) = float32_sub(v->ZMM_S(0), s->ZMM_S(0), &env->sse_status);
    d->ZMM_S(1) = float32_add(v->ZMM_S(1), s->ZMM_S(1), &env->sse_status);
    d->ZMM_S(2) = float32_sub(v->ZMM_S(2), s->ZMM_S(2), &env->sse_status);
    d->ZMM_S(3) = float32_add(v->ZMM_S(3), s->ZMM_S(3), &env->sse_status);
#if SHIFT == 2
    d->ZMM_S(4) = float32_sub(v->ZMM_S(4), s->ZMM_S(4), &env->sse_status);
    d->ZMM_S(5) = float32_add(v->ZMM_S(5), s->ZMM_S(5), &env->sse_status);
    d->ZMM_S(6) = float32_sub(v->ZMM_S(6), s->ZMM_S(6), &env->sse_status);
    d->ZMM_S(7) = float32_add(v->ZMM_S(7), s->ZMM_S(7), &env->sse_status);
#endif
}

void glue(helper_addsubpd, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    d->ZMM_D(0) = float64_sub(v->ZMM_D(0), s->ZMM_D(0), &env->sse_status);
    d->ZMM_D(1) = float64_add(v->ZMM_D(1), s->ZMM_D(1), &env->sse_status);
#if SHIFT == 2
    d->ZMM_D(2) = float64_sub(v->ZMM_D(2), s->ZMM_D(2), &env->sse_status);
    d->ZMM_D(3) = float64_add(v->ZMM_D(3), s->ZMM_D(3), &env->sse_status);
#endif
}

#define SSE_HELPER_CMP_P(name, F, C)                                    \
    void glue(helper_ ## name ## ps, SUFFIX)(CPUX86State *env,          \
                                             Reg *d, Reg *v, Reg *s)    \
    {                                                                   \
        d->ZMM_L(0) = F(32, C, v->ZMM_S(0), s->ZMM_S(0));               \
        d->ZMM_L(1) = F(32, C, v->ZMM_S(1), s->ZMM_S(1));               \
        d->ZMM_L(2) = F(32, C, v->ZMM_S(2), s->ZMM_S(2));               \
        d->ZMM_L(3) = F(32, C, v->ZMM_S(3), s->ZMM_S(3));               \
        YMM_ONLY(                                                       \
        d->ZMM_L(4) = F(32, C, v->ZMM_S(4), s->ZMM_S(4));               \
        d->ZMM_L(5) = F(32, C, v->ZMM_S(5), s->ZMM_S(5));               \
        d->ZMM_L(6) = F(32, C, v->ZMM_S(6), s->ZMM_S(6));               \
        d->ZMM_L(7) = F(32, C, v->ZMM_S(7), s->ZMM_S(7));               \
        )                                                               \
    }                                                                   \
                                                                        \
    void glue(helper_ ## name ## pd, SUFFIX)(CPUX86State *env,          \
                                             Reg *d, Reg *v, Reg *s)    \
    {                                                                   \
        d->ZMM_Q(0) = F(64, C, v->ZMM_D(0), s->ZMM_D(0));               \
        d->ZMM_Q(1) = F(64, C, v->ZMM_D(1), s->ZMM_D(1));               \
        YMM_ONLY(                                                       \
        d->ZMM_Q(2) = F(64, C, v->ZMM_D(2), s->ZMM_D(2));               \
        d->ZMM_Q(3) = F(64, C, v->ZMM_D(3), s->ZMM_D(3));               \
        )                                                               \
    }

#if SHIFT == 1
#define SSE_HELPER_CMP(name, F, C)                                          \
    SSE_HELPER_CMP_P(name, F, C)                                            \
    void helper_ ## name ## ss(CPUX86State *env, Reg *d, Reg *v, Reg *s)    \
    {                                                                       \
        d->ZMM_L(0) = F(32, C, v->ZMM_S(0), s->ZMM_S(0));                   \
    }                                                                       \
                                                                            \
    void helper_ ## name ## sd(CPUX86State *env, Reg *d, Reg *v, Reg *s)    \
    {                                                                       \
        d->ZMM_Q(0) = F(64, C, v->ZMM_D(0), s->ZMM_D(0));                   \
    }

static inline bool FPU_EQU(FloatRelation x)
{
    return (x == float_relation_equal || x == float_relation_unordered);
}
static inline bool FPU_GE(FloatRelation x)
{
    return (x == float_relation_equal || x == float_relation_greater);
}
#define FPU_EQ(x) (x == float_relation_equal)
#define FPU_LT(x) (x == float_relation_less)
#define FPU_LE(x) (x <= float_relation_equal)
#define FPU_GT(x) (x == float_relation_greater)
#define FPU_UNORD(x) (x == float_relation_unordered)
#define FPU_FALSE(x) 0

#define FPU_CMPQ(size, COND, a, b) \
    (COND(float ## size ## _compare_quiet(a, b, &env->sse_status)) ? -1 : 0)
#define FPU_CMPS(size, COND, a, b) \
    (COND(float ## size ## _compare(a, b, &env->sse_status)) ? -1 : 0)

#else
#define SSE_HELPER_CMP(name, F, C) SSE_HELPER_CMP_P(name, F, C)
#endif

SSE_HELPER_CMP(cmpeq, FPU_CMPQ, FPU_EQ)
SSE_HELPER_CMP(cmplt, FPU_CMPS, FPU_LT)
SSE_HELPER_CMP(cmple, FPU_CMPS, FPU_LE)
SSE_HELPER_CMP(cmpunord, FPU_CMPQ,  FPU_UNORD)
SSE_HELPER_CMP(cmpneq, FPU_CMPQ, !FPU_EQ)
SSE_HELPER_CMP(cmpnlt, FPU_CMPS, !FPU_LT)
SSE_HELPER_CMP(cmpnle, FPU_CMPS, !FPU_LE)
SSE_HELPER_CMP(cmpord, FPU_CMPQ, !FPU_UNORD)

SSE_HELPER_CMP(cmpequ, FPU_CMPQ, FPU_EQU)
SSE_HELPER_CMP(cmpnge, FPU_CMPS, !FPU_GE)
SSE_HELPER_CMP(cmpngt, FPU_CMPS, !FPU_GT)
SSE_HELPER_CMP(cmpfalse, FPU_CMPQ,  FPU_FALSE)
SSE_HELPER_CMP(cmpnequ, FPU_CMPQ, !FPU_EQU)
SSE_HELPER_CMP(cmpge, FPU_CMPS, FPU_GE)
SSE_HELPER_CMP(cmpgt, FPU_CMPS, FPU_GT)
SSE_HELPER_CMP(cmptrue, FPU_CMPQ,  !FPU_FALSE)

SSE_HELPER_CMP(cmpeqs, FPU_CMPS, FPU_EQ)
SSE_HELPER_CMP(cmpltq, FPU_CMPQ, FPU_LT)
SSE_HELPER_CMP(cmpleq, FPU_CMPQ, FPU_LE)
SSE_HELPER_CMP(cmpunords, FPU_CMPS,  FPU_UNORD)
SSE_HELPER_CMP(cmpneqq, FPU_CMPS, !FPU_EQ)
SSE_HELPER_CMP(cmpnltq, FPU_CMPQ, !FPU_LT)
SSE_HELPER_CMP(cmpnleq, FPU_CMPQ, !FPU_LE)
SSE_HELPER_CMP(cmpords, FPU_CMPS, !FPU_UNORD)

SSE_HELPER_CMP(cmpequs, FPU_CMPS, FPU_EQU)
SSE_HELPER_CMP(cmpngeq, FPU_CMPQ, !FPU_GE)
SSE_HELPER_CMP(cmpngtq, FPU_CMPQ, !FPU_GT)
SSE_HELPER_CMP(cmpfalses, FPU_CMPS,  FPU_FALSE)
SSE_HELPER_CMP(cmpnequs, FPU_CMPS, !FPU_EQU)
SSE_HELPER_CMP(cmpgeq, FPU_CMPQ, FPU_GE)
SSE_HELPER_CMP(cmpgtq, FPU_CMPQ, FPU_GT)
SSE_HELPER_CMP(cmptrues, FPU_CMPS,  !FPU_FALSE)

#if SHIFT == 1
static const int comis_eflags[4] = {CC_C, CC_Z, 0, CC_Z | CC_P | CC_C};

void helper_ucomiss(CPUX86State *env, Reg *d, Reg *s)
{
    FloatRelation ret;
    float32 s0, s1;

    s0 = d->ZMM_S(0);
    s1 = s->ZMM_S(0);
    ret = float32_compare_quiet(s0, s1, &env->sse_status);
    CC_SRC = comis_eflags[ret + 1];
}

void helper_comiss(CPUX86State *env, Reg *d, Reg *s)
{
    FloatRelation ret;
    float32 s0, s1;

    s0 = d->ZMM_S(0);
    s1 = s->ZMM_S(0);
    ret = float32_compare(s0, s1, &env->sse_status);
    CC_SRC = comis_eflags[ret + 1];
}

void helper_ucomisd(CPUX86State *env, Reg *d, Reg *s)
{
    FloatRelation ret;
    float64 d0, d1;

    d0 = d->ZMM_D(0);
    d1 = s->ZMM_D(0);
    ret = float64_compare_quiet(d0, d1, &env->sse_status);
    CC_SRC = comis_eflags[ret + 1];
}

void helper_comisd(CPUX86State *env, Reg *d, Reg *s)
{
    FloatRelation ret;
    float64 d0, d1;

    d0 = d->ZMM_D(0);
    d1 = s->ZMM_D(0);
    ret = float64_compare(d0, d1, &env->sse_status);
    CC_SRC = comis_eflags[ret + 1];
}
#endif

uint32_t glue(helper_movmskps, SUFFIX)(CPUX86State *env, Reg *s)
{
    uint32_t mask;

    mask = 0;
    mask |= (s->ZMM_L(0) >> (31 - 0)) & (1 << 0);
    mask |= (s->ZMM_L(1) >> (31 - 1)) & (1 << 1);
    mask |= (s->ZMM_L(2) >> (31 - 2)) & (1 << 2);
    mask |= (s->ZMM_L(3) >> (31 - 3)) & (1 << 3);
#if SHIFT == 2
    mask |= (s->ZMM_L(4) >> (31 - 4)) & (1 << 4);
    mask |= (s->ZMM_L(5) >> (31 - 5)) & (1 << 5);
    mask |= (s->ZMM_L(6) >> (31 - 6)) & (1 << 6);
    mask |= (s->ZMM_L(7) >> (31 - 7)) & (1 << 7);
#endif
    return mask;
}

uint32_t glue(helper_movmskpd, SUFFIX)(CPUX86State *env, Reg *s)
{
    uint32_t mask;

    mask = 0;
    mask |= (s->ZMM_L(1) >> (31 - 0)) & (1 << 0);
    mask |= (s->ZMM_L(3) >> (31 - 1)) & (1 << 1);
#if SHIFT == 2
    mask |= (s->ZMM_L(5) >> (31 - 2)) & (1 << 2);
    mask |= (s->ZMM_L(7) >> (31 - 3)) & (1 << 3);
#endif
    return mask;
}

#endif

uint32_t glue(helper_pmovmskb, SUFFIX)(CPUX86State *env, Reg *s)
{
    uint32_t val;

    val = 0;
    val |= (s->B(0) >> 7);
    val |= (s->B(1) >> 6) & 0x02;
    val |= (s->B(2) >> 5) & 0x04;
    val |= (s->B(3) >> 4) & 0x08;
    val |= (s->B(4) >> 3) & 0x10;
    val |= (s->B(5) >> 2) & 0x20;
    val |= (s->B(6) >> 1) & 0x40;
    val |= (s->B(7)) & 0x80;
#if SHIFT >= 1
    val |= (s->B(8) << 1) & 0x0100;
    val |= (s->B(9) << 2) & 0x0200;
    val |= (s->B(10) << 3) & 0x0400;
    val |= (s->B(11) << 4) & 0x0800;
    val |= (s->B(12) << 5) & 0x1000;
    val |= (s->B(13) << 6) & 0x2000;
    val |= (s->B(14) << 7) & 0x4000;
    val |= (s->B(15) << 8) & 0x8000;
#if SHIFT == 2
    val |= ((uint32_t)s->B(16) << 9) & 0x00010000;
    val |= ((uint32_t)s->B(17) << 10) & 0x00020000;
    val |= ((uint32_t)s->B(18) << 11) & 0x00040000;
    val |= ((uint32_t)s->B(19) << 12) & 0x00080000;
    val |= ((uint32_t)s->B(20) << 13) & 0x00100000;
    val |= ((uint32_t)s->B(21) << 14) & 0x00200000;
    val |= ((uint32_t)s->B(22) << 15) & 0x00400000;
    val |= ((uint32_t)s->B(23) << 16) & 0x00800000;
    val |= ((uint32_t)s->B(24) << 17) & 0x01000000;
    val |= ((uint32_t)s->B(25) << 18) & 0x02000000;
    val |= ((uint32_t)s->B(26) << 19) & 0x04000000;
    val |= ((uint32_t)s->B(27) << 20) & 0x08000000;
    val |= ((uint32_t)s->B(28) << 21) & 0x10000000;
    val |= ((uint32_t)s->B(29) << 22) & 0x20000000;
    val |= ((uint32_t)s->B(30) << 23) & 0x40000000;
    val |= ((uint32_t)s->B(31) << 24) & 0x80000000;
#endif
#endif
    return val;
}

#if SHIFT == 0
#define PACK_WIDTH 4
#else
#define PACK_WIDTH 8
#endif

#define PACK4(F, to, reg, from) do {        \
    r[to + 0] = F((int16_t)reg->W(from + 0));   \
    r[to + 1] = F((int16_t)reg->W(from + 1));   \
    r[to + 2] = F((int16_t)reg->W(from + 2));   \
    r[to + 3] = F((int16_t)reg->W(from + 3));   \
    } while (0)

#define PACK_HELPER_B(name, F) \
void glue(helper_pack ## name, SUFFIX)(CPUX86State *env, \
        Reg *d, Reg *v, Reg *s)                 \
{                                               \
    uint8_t r[PACK_WIDTH * 2];                  \
    int i;                                      \
    PACK4(F, 0, v, 0);                          \
    PACK4(F, PACK_WIDTH, s, 0);                 \
    XMM_ONLY(                                   \
        PACK4(F, 4, v, 4);                      \
        PACK4(F, 12, s, 4);                     \
        )                                       \
    for (i = 0; i < PACK_WIDTH * 2; i++) {      \
        d->B(i) = r[i];                         \
    }                                           \
    YMM_ONLY(                                   \
        PACK4(F, 0, v, 8);                      \
        PACK4(F, 4, v, 12);                     \
        PACK4(F, 8, s, 8);                      \
        PACK4(F, 12, s, 12);                    \
        for (i = 0; i < 16; i++) {              \
            d->B(i + 16) = r[i];                \
        }                                       \
        )                                       \
}

PACK_HELPER_B(sswb, satsb)
PACK_HELPER_B(uswb, satub)

void glue(helper_packssdw, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    uint16_t r[PACK_WIDTH];
    int i;

    r[0] = satsw(v->L(0));
    r[1] = satsw(v->L(1));
    r[PACK_WIDTH / 2 + 0] = satsw(s->L(0));
    r[PACK_WIDTH / 2 + 1] = satsw(s->L(1));
#if SHIFT >= 1
    r[2] = satsw(v->L(2));
    r[3] = satsw(v->L(3));
    r[6] = satsw(s->L(2));
    r[7] = satsw(s->L(3));
#endif
    for (i = 0; i < PACK_WIDTH; i++) {
        d->W(i) = r[i];
    }
#if SHIFT == 2
    r[0] = satsw(v->L(4));
    r[1] = satsw(v->L(5));
    r[2] = satsw(v->L(6));
    r[3] = satsw(v->L(7));
    r[4] = satsw(s->L(4));
    r[5] = satsw(s->L(5));
    r[6] = satsw(s->L(6));
    r[7] = satsw(s->L(7));
    for (i = 0; i < 8; i++) {
        d->W(i + 8) = r[i];
    }
#endif
}

#define UNPCK_OP(base_name, base)                                       \
                                                                        \
    void glue(helper_punpck ## base_name ## bw, SUFFIX)(CPUX86State *env,\
                                                Reg *d, Reg *v, Reg *s) \
    {                                                                   \
        uint8_t r[PACK_WIDTH * 2];                                      \
        int i;                                                          \
                                                                        \
        r[0] = v->B((base * PACK_WIDTH) + 0);                           \
        r[1] = s->B((base * PACK_WIDTH) + 0);                           \
        r[2] = v->B((base * PACK_WIDTH) + 1);                           \
        r[3] = s->B((base * PACK_WIDTH) + 1);                           \
        r[4] = v->B((base * PACK_WIDTH) + 2);                           \
        r[5] = s->B((base * PACK_WIDTH) + 2);                           \
        r[6] = v->B((base * PACK_WIDTH) + 3);                           \
        r[7] = s->B((base * PACK_WIDTH) + 3);                           \
        XMM_ONLY(                                                       \
                 r[8] = v->B((base * PACK_WIDTH) + 4);                  \
                 r[9] = s->B((base * PACK_WIDTH) + 4);                  \
                 r[10] = v->B((base * PACK_WIDTH) + 5);                 \
                 r[11] = s->B((base * PACK_WIDTH) + 5);                 \
                 r[12] = v->B((base * PACK_WIDTH) + 6);                 \
                 r[13] = s->B((base * PACK_WIDTH) + 6);                 \
                 r[14] = v->B((base * PACK_WIDTH) + 7);                 \
                 r[15] = s->B((base * PACK_WIDTH) + 7);                 \
                                                                      ) \
        for (i = 0; i < PACK_WIDTH * 2; i++) {                          \
            d->B(i) = r[i];                                             \
        }                                                               \
        YMM_ONLY(                                                       \
                r[0] = v->B((base * 8) + 16);                           \
                r[1] = s->B((base * 8) + 16);                           \
                r[2] = v->B((base * 8) + 17);                           \
                r[3] = s->B((base * 8) + 17);                           \
                r[4] = v->B((base * 8) + 18);                           \
                r[5] = s->B((base * 8) + 18);                           \
                r[6] = v->B((base * 8) + 19);                           \
                r[7] = s->B((base * 8) + 19);                           \
                r[8] = v->B((base * 8) + 20);                           \
                r[9] = s->B((base * 8) + 20);                           \
                r[10] = v->B((base * 8) + 21);                          \
                r[11] = s->B((base * 8) + 21);                          \
                r[12] = v->B((base * 8) + 22);                          \
                r[13] = s->B((base * 8) + 22);                          \
                r[14] = v->B((base * 8) + 23);                          \
                r[15] = s->B((base * 8) + 23);                          \
                for (i = 0; i < PACK_WIDTH * 2; i++) {                  \
                    d->B(16 + i) = r[i];                                \
                }                                                       \
                                                                      ) \
    }                                                                   \
                                                                        \
    void glue(helper_punpck ## base_name ## wd, SUFFIX)(CPUX86State *env,\
                                                Reg *d, Reg *v, Reg *s) \
    {                                                                   \
        uint16_t r[PACK_WIDTH];                                         \
        int i;                                                          \
                                                                        \
        r[0] = v->W((base * (PACK_WIDTH / 2)) + 0);                     \
        r[1] = s->W((base * (PACK_WIDTH / 2)) + 0);                     \
        r[2] = v->W((base * (PACK_WIDTH / 2)) + 1);                     \
        r[3] = s->W((base * (PACK_WIDTH / 2)) + 1);                     \
        XMM_ONLY(                                                       \
                 r[4] = v->W((base * 4) + 2);                           \
                 r[5] = s->W((base * 4) + 2);                           \
                 r[6] = v->W((base * 4) + 3);                           \
                 r[7] = s->W((base * 4) + 3);                           \
                                                                      ) \
        for (i = 0; i < PACK_WIDTH; i++) {                              \
            d->W(i) = r[i];                                             \
        }                                                               \
        YMM_ONLY(                                                       \
                r[0] = v->W((base * 4) + 8);                            \
                r[1] = s->W((base * 4) + 8);                            \
                r[2] = v->W((base * 4) + 9);                            \
                r[3] = s->W((base * 4) + 9);                            \
                r[4] = v->W((base * 4) + 10);                           \
                r[5] = s->W((base * 4) + 10);                           \
                r[6] = v->W((base * 4) + 11);                           \
                r[7] = s->W((base * 4) + 11);                           \
                for (i = 0; i < PACK_WIDTH; i++) {                      \
                    d->W(i + 8) = r[i];                                 \
                }                                                       \
                                                                      ) \
    }                                                                   \
                                                                        \
    void glue(helper_punpck ## base_name ## dq, SUFFIX)(CPUX86State *env,\
                                                Reg *d, Reg *v, Reg *s) \
    {                                                                   \
        uint32_t r[4];                                                  \
                                                                        \
        r[0] = v->L((base * (PACK_WIDTH / 4)) + 0);                     \
        r[1] = s->L((base * (PACK_WIDTH / 4)) + 0);                     \
        XMM_ONLY(                                                       \
                 r[2] = v->L((base * 2) + 1);                           \
                 r[3] = s->L((base * 2) + 1);                           \
                 d->L(2) = r[2];                                        \
                 d->L(3) = r[3];                                        \
                                                                      ) \
        d->L(0) = r[0];                                                 \
        d->L(1) = r[1];                                                 \
        YMM_ONLY(                                                       \
                 r[0] = v->L((base * 2) + 4);                           \
                 r[1] = s->L((base * 2) + 4);                           \
                 r[2] = v->L((base * 2) + 5);                           \
                 r[3] = s->L((base * 2) + 5);                           \
                 d->L(4) = r[0];                                        \
                 d->L(5) = r[1];                                        \
                 d->L(6) = r[2];                                        \
                 d->L(7) = r[3];                                        \
                                                                      ) \
    }                                                                   \
                                                                        \
    XMM_ONLY(                                                           \
             void glue(helper_punpck ## base_name ## qdq, SUFFIX)(      \
                        CPUX86State *env, Reg *d, Reg *v, Reg *s)       \
             {                                                          \
                 uint64_t r[2];                                         \
                                                                        \
                 r[0] = v->Q(base);                                     \
                 r[1] = s->Q(base);                                     \
                 d->Q(0) = r[0];                                        \
                 d->Q(1) = r[1];                                        \
                 YMM_ONLY(                                              \
                     r[0] = v->Q(base + 2);                             \
                     r[1] = s->Q(base + 2);                             \
                     d->Q(2) = r[0];                                    \
                     d->Q(3) = r[1];                                    \
                                                                      ) \
             }                                                          \
                                                                        )

UNPCK_OP(l, 0)
UNPCK_OP(h, 1)

#undef PACK_WIDTH
#undef PACK_HELPER_B
#undef PACK4


/* 3DNow! float ops */
#if SHIFT == 0
void helper_pi2fd(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    d->MMX_S(0) = int32_to_float32(s->MMX_L(0), &env->mmx_status);
    d->MMX_S(1) = int32_to_float32(s->MMX_L(1), &env->mmx_status);
}

void helper_pi2fw(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    d->MMX_S(0) = int32_to_float32((int16_t)s->MMX_W(0), &env->mmx_status);
    d->MMX_S(1) = int32_to_float32((int16_t)s->MMX_W(2), &env->mmx_status);
}

void helper_pf2id(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    d->MMX_L(0) = float32_to_int32_round_to_zero(s->MMX_S(0), &env->mmx_status);
    d->MMX_L(1) = float32_to_int32_round_to_zero(s->MMX_S(1), &env->mmx_status);
}

void helper_pf2iw(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    d->MMX_L(0) = satsw(float32_to_int32_round_to_zero(s->MMX_S(0),
                                                       &env->mmx_status));
    d->MMX_L(1) = satsw(float32_to_int32_round_to_zero(s->MMX_S(1),
                                                       &env->mmx_status));
}

void helper_pfacc(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    MMXReg r;

    r.MMX_S(0) = float32_add(d->MMX_S(0), d->MMX_S(1), &env->mmx_status);
    r.MMX_S(1) = float32_add(s->MMX_S(0), s->MMX_S(1), &env->mmx_status);
    *d = r;
}

void helper_pfadd(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    d->MMX_S(0) = float32_add(d->MMX_S(0), s->MMX_S(0), &env->mmx_status);
    d->MMX_S(1) = float32_add(d->MMX_S(1), s->MMX_S(1), &env->mmx_status);
}

void helper_pfcmpeq(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    d->MMX_L(0) = float32_eq_quiet(d->MMX_S(0), s->MMX_S(0),
                                   &env->mmx_status) ? -1 : 0;
    d->MMX_L(1) = float32_eq_quiet(d->MMX_S(1), s->MMX_S(1),
                                   &env->mmx_status) ? -1 : 0;
}

void helper_pfcmpge(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    d->MMX_L(0) = float32_le(s->MMX_S(0), d->MMX_S(0),
                             &env->mmx_status) ? -1 : 0;
    d->MMX_L(1) = float32_le(s->MMX_S(1), d->MMX_S(1),
                             &env->mmx_status) ? -1 : 0;
}

void helper_pfcmpgt(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    d->MMX_L(0) = float32_lt(s->MMX_S(0), d->MMX_S(0),
                             &env->mmx_status) ? -1 : 0;
    d->MMX_L(1) = float32_lt(s->MMX_S(1), d->MMX_S(1),
                             &env->mmx_status) ? -1 : 0;
}

void helper_pfmax(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    if (float32_lt(d->MMX_S(0), s->MMX_S(0), &env->mmx_status)) {
        d->MMX_S(0) = s->MMX_S(0);
    }
    if (float32_lt(d->MMX_S(1), s->MMX_S(1), &env->mmx_status)) {
        d->MMX_S(1) = s->MMX_S(1);
    }
}

void helper_pfmin(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    if (float32_lt(s->MMX_S(0), d->MMX_S(0), &env->mmx_status)) {
        d->MMX_S(0) = s->MMX_S(0);
    }
    if (float32_lt(s->MMX_S(1), d->MMX_S(1), &env->mmx_status)) {
        d->MMX_S(1) = s->MMX_S(1);
    }
}

void helper_pfmul(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    d->MMX_S(0) = float32_mul(d->MMX_S(0), s->MMX_S(0), &env->mmx_status);
    d->MMX_S(1) = float32_mul(d->MMX_S(1), s->MMX_S(1), &env->mmx_status);
}

void helper_pfnacc(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    MMXReg r;

    r.MMX_S(0) = float32_sub(d->MMX_S(0), d->MMX_S(1), &env->mmx_status);
    r.MMX_S(1) = float32_sub(s->MMX_S(0), s->MMX_S(1), &env->mmx_status);
    *d = r;
}

void helper_pfpnacc(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    MMXReg r;

    r.MMX_S(0) = float32_sub(d->MMX_S(0), d->MMX_S(1), &env->mmx_status);
    r.MMX_S(1) = float32_add(s->MMX_S(0), s->MMX_S(1), &env->mmx_status);
    *d = r;
}

void helper_pfrcp(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    d->MMX_S(0) = float32_div(float32_one, s->MMX_S(0), &env->mmx_status);
    d->MMX_S(1) = d->MMX_S(0);
}

void helper_pfrsqrt(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    d->MMX_L(1) = s->MMX_L(0) & 0x7fffffff;
    d->MMX_S(1) = float32_div(float32_one,
                              float32_sqrt(d->MMX_S(1), &env->mmx_status),
                              &env->mmx_status);
    d->MMX_L(1) |= s->MMX_L(0) & 0x80000000;
    d->MMX_L(0) = d->MMX_L(1);
}

void helper_pfsub(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    d->MMX_S(0) = float32_sub(d->MMX_S(0), s->MMX_S(0), &env->mmx_status);
    d->MMX_S(1) = float32_sub(d->MMX_S(1), s->MMX_S(1), &env->mmx_status);
}

void helper_pfsubr(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    d->MMX_S(0) = float32_sub(s->MMX_S(0), d->MMX_S(0), &env->mmx_status);
    d->MMX_S(1) = float32_sub(s->MMX_S(1), d->MMX_S(1), &env->mmx_status);
}

void helper_pswapd(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    MMXReg r;

    r.MMX_L(0) = s->MMX_L(1);
    r.MMX_L(1) = s->MMX_L(0);
    *d = r;
}
#endif

/* SSSE3 op helpers */
void glue(helper_pshufb, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    int i;
#if SHIFT == 0
    uint8_t r[8];

    for (i = 0; i < 8; i++) {
        r[i] = (s->B(i) & 0x80) ? 0 : (v->B(s->B(i) & 7));
    }
    for (i = 0; i < 8; i++) {
        d->B(i) = r[i];
    }
#else
    uint8_t r[16];

    for (i = 0; i < 16; i++) {
        r[i] = (s->B(i) & 0x80) ? 0 : (v->B(s->B(i) & 0xf));
    }
    for (i = 0; i < 16; i++) {
        d->B(i) = r[i];
    }
#if SHIFT == 2
    for (i = 0; i < 16; i++) {
        r[i] = (s->B(i + 16) & 0x80) ? 0 : (v->B((s->B(i + 16) & 0xf) + 16));
    }
    for (i = 0; i < 16; i++) {
        d->B(i + 16) = r[i];
    }
#endif
#endif
}

#if SHIFT == 0

#define SSE_HELPER_HW(name, F)  \
void glue(helper_ ## name, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s) \
{                               \
    uint16_t r[4];               \
    r[0] = F(v->W(0), v->W(1)); \
    r[1] = F(v->W(2), v->W(3)); \
    r[2] = F(s->W(0), s->W(1)); \
    r[3] = F(s->W(3), s->W(3)); \
    d->W(0) = r[0];             \
    d->W(1) = r[1];             \
    d->W(2) = r[2];             \
    d->W(3) = r[3];             \
}

#define SSE_HELPER_HL(name, F)  \
void glue(helper_ ## name, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s) \
{                               \
    uint32_t r0, r1;             \
    r0 = F(v->L(0), v->L(1));   \
    r1 = F(s->L(0), s->L(1));   \
    d->W(0) = r0;               \
    d->W(1) = r1;               \
}

#else

#define SSE_HELPER_HW(name, F)  \
void glue(helper_ ## name, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s) \
{                                   \
    int32_t r[8];                   \
    r[0] = F(v->W(0), v->W(1));     \
    r[1] = F(v->W(2), v->W(3));     \
    r[2] = F(v->W(4), v->W(5));     \
    r[3] = F(v->W(6), v->W(7));     \
    r[4] = F(s->W(0), s->W(1));     \
    r[5] = F(s->W(2), s->W(3));     \
    r[6] = F(s->W(4), s->W(5));     \
    r[7] = F(s->W(6), s->W(7));     \
    d->W(0) = r[0];                 \
    d->W(1) = r[1];                 \
    d->W(2) = r[2];                 \
    d->W(3) = r[3];                 \
    d->W(4) = r[4];                 \
    d->W(5) = r[5];                 \
    d->W(6) = r[6];                 \
    d->W(7) = r[7];                 \
    YMM_ONLY(                       \
    r[0] = F(v->W(8), v->W(9));     \
    r[1] = F(v->W(10), v->W(11));   \
    r[2] = F(v->W(12), v->W(13));   \
    r[3] = F(v->W(14), v->W(15));   \
    r[4] = F(s->W(8), s->W(9));     \
    r[5] = F(s->W(10), s->W(11));   \
    r[6] = F(s->W(12), s->W(13));   \
    r[7] = F(s->W(14), s->W(15));   \
    d->W(8) = r[0];                 \
    d->W(9) = r[1];                 \
    d->W(10) = r[2];                \
    d->W(11) = r[3];                \
    d->W(12) = r[4];                \
    d->W(13) = r[5];                \
    d->W(14) = r[6];                \
    d->W(15) = r[7];                \
    )                               \
}

#define SSE_HELPER_HL(name, F)  \
void glue(helper_ ## name, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s) \
{                               \
    int32_t r0, r1, r2, r3;     \
    r0 = F(v->L(0), v->L(1));   \
    r1 = F(v->L(2), v->L(3));   \
    r2 = F(s->L(0), s->L(1));   \
    r3 = F(s->L(2), s->L(3));   \
    d->L(0) = r0;               \
    d->L(1) = r1;               \
    d->L(2) = r2;               \
    d->L(3) = r3;               \
    YMM_ONLY(                   \
    r0 = F(v->L(4), v->L(5));   \
    r1 = F(v->L(6), v->L(7));   \
    r2 = F(s->L(4), s->L(5));   \
    r3 = F(s->L(6), s->L(7));   \
    d->L(4) = r0;               \
    d->L(5) = r1;               \
    d->L(6) = r2;               \
    d->L(7) = r3;               \
    )                           \
}
#endif

SSE_HELPER_HW(phaddw, FADD)
SSE_HELPER_HW(phsubw, FSUB)
SSE_HELPER_HW(phaddsw, FADDSW)
SSE_HELPER_HW(phsubsw, FSUBSW)
SSE_HELPER_HL(phaddd, FADD)
SSE_HELPER_HL(phsubd, FSUB)

#undef SSE_HELPER_HW
#undef SSE_HELPER_HL

void glue(helper_pmaddubsw, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    d->W(0) = satsw((int8_t)s->B(0) * (uint8_t)v->B(0) +
                    (int8_t)s->B(1) * (uint8_t)v->B(1));
    d->W(1) = satsw((int8_t)s->B(2) * (uint8_t)v->B(2) +
                    (int8_t)s->B(3) * (uint8_t)v->B(3));
    d->W(2) = satsw((int8_t)s->B(4) * (uint8_t)v->B(4) +
                    (int8_t)s->B(5) * (uint8_t)v->B(5));
    d->W(3) = satsw((int8_t)s->B(6) * (uint8_t)v->B(6) +
                    (int8_t)s->B(7) * (uint8_t)v->B(7));
#if SHIFT >= 1
    d->W(4) = satsw((int8_t)s->B(8) * (uint8_t)v->B(8) +
                    (int8_t)s->B(9) * (uint8_t)v->B(9));
    d->W(5) = satsw((int8_t)s->B(10) * (uint8_t)v->B(10) +
                    (int8_t)s->B(11) * (uint8_t)v->B(11));
    d->W(6) = satsw((int8_t)s->B(12) * (uint8_t)v->B(12) +
                    (int8_t)s->B(13) * (uint8_t)v->B(13));
    d->W(7) = satsw((int8_t)s->B(14) * (uint8_t)v->B(14) +
                    (int8_t)s->B(15) * (uint8_t)v->B(15));
#if SHIFT == 2
    int i;
    for (i = 8; i < 16; i++) {
        d->W(i) = satsw((int8_t)s->B(i * 2) * (uint8_t)v->B(i * 2) +
                        (int8_t)s->B(i * 2 + 1) * (uint8_t)v->B(i * 2 + 1));
    }
#endif
#endif
}

#define FABSB(x) (x > INT8_MAX  ? -(int8_t)x : x)
#define FABSW(x) (x > INT16_MAX ? -(int16_t)x : x)
#define FABSL(x) (x > INT32_MAX ? -(int32_t)x : x)
SSE_HELPER_1(helper_pabsb, B, 8, FABSB)
SSE_HELPER_1(helper_pabsw, W, 4, FABSW)
SSE_HELPER_1(helper_pabsd, L, 2, FABSL)

#define FMULHRSW(d, s) (((int16_t) d * (int16_t)s + 0x4000) >> 15)
SSE_HELPER_W(helper_pmulhrsw, FMULHRSW)

#define FSIGNB(d, s) (s <= INT8_MAX  ? s ? d : 0 : -(int8_t)d)
#define FSIGNW(d, s) (s <= INT16_MAX ? s ? d : 0 : -(int16_t)d)
#define FSIGNL(d, s) (s <= INT32_MAX ? s ? d : 0 : -(int32_t)d)
SSE_HELPER_B(helper_psignb, FSIGNB)
SSE_HELPER_W(helper_psignw, FSIGNW)
SSE_HELPER_L(helper_psignd, FSIGNL)

void glue(helper_palignr, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s,
                                  int32_t shift)
{
    /* XXX could be checked during translation */
    if (shift >= (SHIFT ? 32 : 16)) {
        d->Q(0) = 0;
        XMM_ONLY(d->Q(1) = 0);
#if SHIFT == 2
        d->Q(2) = 0;
        d->Q(3) = 0;
#endif
    } else {
        shift <<= 3;
#define SHR(v, i) (i < 64 && i > -64 ? i > 0 ? v >> (i) : (v << -(i)) : 0)
#if SHIFT == 0
        d->Q(0) = SHR(s->Q(0), shift - 0) |
            SHR(v->Q(0), shift -  64);
#else
        uint64_t r0, r1;

        r0 = SHR(s->Q(0), shift - 0) |
             SHR(s->Q(1), shift -  64) |
             SHR(v->Q(0), shift - 128) |
             SHR(v->Q(1), shift - 192);
        r1 = SHR(s->Q(0), shift + 64) |
             SHR(s->Q(1), shift -   0) |
             SHR(v->Q(0), shift -  64) |
             SHR(v->Q(1), shift - 128);
        d->Q(0) = r0;
        d->Q(1) = r1;
#if SHIFT == 2
        r0 = SHR(s->Q(2), shift - 0) |
             SHR(s->Q(3), shift -  64) |
             SHR(v->Q(2), shift - 128) |
             SHR(v->Q(3), shift - 192);
        r1 = SHR(s->Q(2), shift + 64) |
             SHR(s->Q(3), shift -   0) |
             SHR(v->Q(2), shift -  64) |
             SHR(v->Q(3), shift - 128);
        d->Q(2) = r0;
        d->Q(3) = r1;
#endif
#endif
#undef SHR
    }
}

#if SHIFT >= 1

#define BLEND_V128(elem, num, F, b) do {                                    \
    d->elem(b + 0) = F(v->elem(b + 0), s->elem(b + 0), m->elem(b + 0));     \
    d->elem(b + 1) = F(v->elem(b + 1), s->elem(b + 1), m->elem(b + 1));     \
    if (num > 2) {                                                          \
        d->elem(b + 2) = F(v->elem(b + 2), s->elem(b + 2), m->elem(b + 2)); \
        d->elem(b + 3) = F(v->elem(b + 3), s->elem(b + 3), m->elem(b + 3)); \
    }                                                                       \
    if (num > 4) {                                                          \
        d->elem(b + 4) = F(v->elem(b + 4), s->elem(b + 4), m->elem(b + 4)); \
        d->elem(b + 5) = F(v->elem(b + 5), s->elem(b + 5), m->elem(b + 5)); \
        d->elem(b + 6) = F(v->elem(b + 6), s->elem(b + 6), m->elem(b + 6)); \
        d->elem(b + 7) = F(v->elem(b + 7), s->elem(b + 7), m->elem(b + 7)); \
    }                                                                       \
    if (num > 8) {                                                          \
        d->elem(b + 8) = F(v->elem(b + 8), s->elem(b + 8), m->elem(b + 8)); \
        d->elem(b + 9) = F(v->elem(b + 9), s->elem(b + 9), m->elem(b + 9)); \
        d->elem(b + 10) = F(v->elem(b + 10), s->elem(b + 10), m->elem(b + 10));\
        d->elem(b + 11) = F(v->elem(b + 11), s->elem(b + 11), m->elem(b + 11));\
        d->elem(b + 12) = F(v->elem(b + 12), s->elem(b + 12), m->elem(b + 12));\
        d->elem(b + 13) = F(v->elem(b + 13), s->elem(b + 13), m->elem(b + 13));\
        d->elem(b + 14) = F(v->elem(b + 14), s->elem(b + 14), m->elem(b + 14));\
        d->elem(b + 15) = F(v->elem(b + 15), s->elem(b + 15), m->elem(b + 15));\
    }                                                                   \
    } while (0)

#define SSE_HELPER_V(name, elem, num, F)                                \
    void glue(name, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s,   \
                            Reg *m)                                     \
    {                                                                   \
        BLEND_V128(elem, num, F, 0);                                    \
        YMM_ONLY(BLEND_V128(elem, num, F, num);)                        \
    }

#define BLEND_I128(elem, num, F, b) do {                                    \
    d->elem(b + 0) = F(v->elem(b + 0), s->elem(b + 0), ((imm >> 0) & 1));   \
    d->elem(b + 1) = F(v->elem(b + 1), s->elem(b + 1), ((imm >> 1) & 1));   \
    if (num > 2) {                                                          \
        d->elem(b + 2) = F(v->elem(b + 2), s->elem(b + 2), ((imm >> 2) & 1)); \
        d->elem(b + 3) = F(v->elem(b + 3), s->elem(b + 3), ((imm >> 3) & 1)); \
    }                                                                       \
    if (num > 4) {                                                          \
        d->elem(b + 4) = F(v->elem(b + 4), s->elem(b + 4), ((imm >> 4) & 1)); \
        d->elem(b + 5) = F(v->elem(b + 5), s->elem(b + 5), ((imm >> 5) & 1)); \
        d->elem(b + 6) = F(v->elem(b + 6), s->elem(b + 6), ((imm >> 6) & 1)); \
        d->elem(b + 7) = F(v->elem(b + 7), s->elem(b + 7), ((imm >> 7) & 1)); \
    }                                                                       \
    } while (0)

#define SSE_HELPER_I(name, elem, num, F)                                \
    void glue(name, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s,   \
                            uint32_t imm)                               \
    {                                                                   \
        BLEND_I128(elem, num, F, 0);                                    \
        YMM_ONLY(                                                       \
        if (num < 8)                                                    \
            imm >>= num;                                                \
        BLEND_I128(elem, num, F, num);                                  \
        )                                                               \
    }

/* SSE4.1 op helpers */
#define FBLENDVB(v, s, m) ((m & 0x80) ? s : v)
#define FBLENDVPS(v, s, m) ((m & 0x80000000) ? s : v)
#define FBLENDVPD(v, s, m) ((m & 0x8000000000000000LL) ? s : v)
SSE_HELPER_V(helper_pblendvb, B, 16, FBLENDVB)
SSE_HELPER_V(helper_blendvps, L, 4, FBLENDVPS)
SSE_HELPER_V(helper_blendvpd, Q, 2, FBLENDVPD)

void glue(helper_ptest, SUFFIX)(CPUX86State *env, Reg *d, Reg *s)
{
    uint64_t zf = (s->Q(0) &  d->Q(0)) | (s->Q(1) &  d->Q(1));
    uint64_t cf = (s->Q(0) & ~d->Q(0)) | (s->Q(1) & ~d->Q(1));

#if SHIFT == 2
    zf |= (s->Q(2) &  d->Q(2)) | (s->Q(3) &  d->Q(3));
    cf |= (s->Q(2) & ~d->Q(2)) | (s->Q(3) & ~d->Q(3));
#endif
    CC_SRC = (zf ? 0 : CC_Z) | (cf ? 0 : CC_C);
}

#define SSE_HELPER_F(name, elem, num, F)        \
    void glue(name, SUFFIX)(CPUX86State *env, Reg *d, Reg *s)     \
    {                                           \
        if (num * SHIFT > 2) {                  \
            if (num * SHIFT > 8) {              \
                d->elem(15) = F(15);            \
                d->elem(14) = F(14);            \
                d->elem(13) = F(13);            \
                d->elem(12) = F(12);            \
                d->elem(11) = F(11);            \
                d->elem(10) = F(10);            \
                d->elem(9) = F(9);              \
                d->elem(8) = F(8);              \
            }                                   \
            if (num * SHIFT > 4) {              \
                d->elem(7) = F(7);              \
                d->elem(6) = F(6);              \
                d->elem(5) = F(5);              \
                d->elem(4) = F(4);              \
            }                                   \
            d->elem(3) = F(3);                  \
            d->elem(2) = F(2);                  \
        }                                       \
        d->elem(1) = F(1);                      \
        d->elem(0) = F(0);                      \
    }

SSE_HELPER_F(helper_pmovsxbw, W, 8, (int8_t) s->B)
SSE_HELPER_F(helper_pmovsxbd, L, 4, (int8_t) s->B)
SSE_HELPER_F(helper_pmovsxbq, Q, 2, (int8_t) s->B)
SSE_HELPER_F(helper_pmovsxwd, L, 4, (int16_t) s->W)
SSE_HELPER_F(helper_pmovsxwq, Q, 2, (int16_t) s->W)
SSE_HELPER_F(helper_pmovsxdq, Q, 2, (int32_t) s->L)
SSE_HELPER_F(helper_pmovzxbw, W, 8, s->B)
SSE_HELPER_F(helper_pmovzxbd, L, 4, s->B)
SSE_HELPER_F(helper_pmovzxbq, Q, 2, s->B)
SSE_HELPER_F(helper_pmovzxwd, L, 4, s->W)
SSE_HELPER_F(helper_pmovzxwq, Q, 2, s->W)
SSE_HELPER_F(helper_pmovzxdq, Q, 2, s->L)

void glue(helper_pmuldq, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    d->Q(0) = (int64_t)(int32_t) v->L(0) * (int32_t) s->L(0);
    d->Q(1) = (int64_t)(int32_t) v->L(2) * (int32_t) s->L(2);
#if SHIFT == 2
    d->Q(2) = (int64_t)(int32_t) v->L(4) * (int32_t) s->L(4);
    d->Q(3) = (int64_t)(int32_t) v->L(6) * (int32_t) s->L(6);
#endif
}

#define FCMPEQQ(d, s) (d == s ? -1 : 0)
SSE_HELPER_Q(helper_pcmpeqq, FCMPEQQ)

void glue(helper_packusdw, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    uint16_t r[8];

    r[0] = satuw((int32_t) v->L(0));
    r[1] = satuw((int32_t) v->L(1));
    r[2] = satuw((int32_t) v->L(2));
    r[3] = satuw((int32_t) v->L(3));
    r[4] = satuw((int32_t) s->L(0));
    r[5] = satuw((int32_t) s->L(1));
    r[6] = satuw((int32_t) s->L(2));
    r[7] = satuw((int32_t) s->L(3));
    d->W(0) = r[0];
    d->W(1) = r[1];
    d->W(2) = r[2];
    d->W(3) = r[3];
    d->W(4) = r[4];
    d->W(5) = r[5];
    d->W(6) = r[6];
    d->W(7) = r[7];
#if SHIFT == 2
    r[0] = satuw((int32_t) v->L(4));
    r[1] = satuw((int32_t) v->L(5));
    r[2] = satuw((int32_t) v->L(6));
    r[3] = satuw((int32_t) v->L(7));
    r[4] = satuw((int32_t) s->L(4));
    r[5] = satuw((int32_t) s->L(5));
    r[6] = satuw((int32_t) s->L(6));
    r[7] = satuw((int32_t) s->L(7));
    d->W(8) = r[0];
    d->W(9) = r[1];
    d->W(10) = r[2];
    d->W(11) = r[3];
    d->W(12) = r[4];
    d->W(13) = r[5];
    d->W(14) = r[6];
    d->W(15) = r[7];
#endif
}

#define FMINSB(d, s) MIN((int8_t)d, (int8_t)s)
#define FMINSD(d, s) MIN((int32_t)d, (int32_t)s)
#define FMAXSB(d, s) MAX((int8_t)d, (int8_t)s)
#define FMAXSD(d, s) MAX((int32_t)d, (int32_t)s)
SSE_HELPER_B(helper_pminsb, FMINSB)
SSE_HELPER_L(helper_pminsd, FMINSD)
SSE_HELPER_W(helper_pminuw, MIN)
SSE_HELPER_L(helper_pminud, MIN)
SSE_HELPER_B(helper_pmaxsb, FMAXSB)
SSE_HELPER_L(helper_pmaxsd, FMAXSD)
SSE_HELPER_W(helper_pmaxuw, MAX)
SSE_HELPER_L(helper_pmaxud, MAX)

#define FMULLD(d, s) ((int32_t)d * (int32_t)s)
SSE_HELPER_L(helper_pmulld, FMULLD)

#if SHIFT == 1
void glue(helper_phminposuw, SUFFIX)(CPUX86State *env, Reg *d, Reg *s)
{
    int idx = 0;

    if (s->W(1) < s->W(idx)) {
        idx = 1;
    }
    if (s->W(2) < s->W(idx)) {
        idx = 2;
    }
    if (s->W(3) < s->W(idx)) {
        idx = 3;
    }
    if (s->W(4) < s->W(idx)) {
        idx = 4;
    }
    if (s->W(5) < s->W(idx)) {
        idx = 5;
    }
    if (s->W(6) < s->W(idx)) {
        idx = 6;
    }
    if (s->W(7) < s->W(idx)) {
        idx = 7;
    }

    d->W(0) = s->W(idx);
    d->W(1) = idx;
    d->L(1) = 0;
    d->Q(1) = 0;
}
#endif

void glue(helper_roundps, SUFFIX)(CPUX86State *env, Reg *d, Reg *s,
                                  uint32_t mode)
{
    uint8_t old_flags = get_float_exception_flags(&env->sse_status);
    signed char prev_rounding_mode;

    prev_rounding_mode = env->sse_status.float_rounding_mode;
    if (!(mode & (1 << 2))) {
        switch (mode & 3) {
        case 0:
            set_float_rounding_mode(float_round_nearest_even, &env->sse_status);
            break;
        case 1:
            set_float_rounding_mode(float_round_down, &env->sse_status);
            break;
        case 2:
            set_float_rounding_mode(float_round_up, &env->sse_status);
            break;
        case 3:
            set_float_rounding_mode(float_round_to_zero, &env->sse_status);
            break;
        }
    }

    d->ZMM_S(0) = float32_round_to_int(s->ZMM_S(0), &env->sse_status);
    d->ZMM_S(1) = float32_round_to_int(s->ZMM_S(1), &env->sse_status);
    d->ZMM_S(2) = float32_round_to_int(s->ZMM_S(2), &env->sse_status);
    d->ZMM_S(3) = float32_round_to_int(s->ZMM_S(3), &env->sse_status);
#if SHIFT == 2
    d->ZMM_S(4) = float32_round_to_int(s->ZMM_S(4), &env->sse_status);
    d->ZMM_S(5) = float32_round_to_int(s->ZMM_S(5), &env->sse_status);
    d->ZMM_S(6) = float32_round_to_int(s->ZMM_S(6), &env->sse_status);
    d->ZMM_S(7) = float32_round_to_int(s->ZMM_S(7), &env->sse_status);
#endif

    if (mode & (1 << 3) && !(old_flags & float_flag_inexact)) {
        set_float_exception_flags(get_float_exception_flags(&env->sse_status) &
                                  ~float_flag_inexact,
                                  &env->sse_status);
    }
    env->sse_status.float_rounding_mode = prev_rounding_mode;
}

void glue(helper_roundpd, SUFFIX)(CPUX86State *env, Reg *d, Reg *s,
                                  uint32_t mode)
{
    uint8_t old_flags = get_float_exception_flags(&env->sse_status);
    signed char prev_rounding_mode;

    prev_rounding_mode = env->sse_status.float_rounding_mode;
    if (!(mode & (1 << 2))) {
        switch (mode & 3) {
        case 0:
            set_float_rounding_mode(float_round_nearest_even, &env->sse_status);
            break;
        case 1:
            set_float_rounding_mode(float_round_down, &env->sse_status);
            break;
        case 2:
            set_float_rounding_mode(float_round_up, &env->sse_status);
            break;
        case 3:
            set_float_rounding_mode(float_round_to_zero, &env->sse_status);
            break;
        }
    }

    d->ZMM_D(0) = float64_round_to_int(s->ZMM_D(0), &env->sse_status);
    d->ZMM_D(1) = float64_round_to_int(s->ZMM_D(1), &env->sse_status);
#if SHIFT == 2
    d->ZMM_D(2) = float64_round_to_int(s->ZMM_D(2), &env->sse_status);
    d->ZMM_D(3) = float64_round_to_int(s->ZMM_D(3), &env->sse_status);
#endif

    if (mode & (1 << 3) && !(old_flags & float_flag_inexact)) {
        set_float_exception_flags(get_float_exception_flags(&env->sse_status) &
                                  ~float_flag_inexact,
                                  &env->sse_status);
    }
    env->sse_status.float_rounding_mode = prev_rounding_mode;
}

#if SHIFT == 1
void helper_roundss_xmm(CPUX86State *env, Reg *d, Reg *s,
                                  uint32_t mode)
{
    uint8_t old_flags = get_float_exception_flags(&env->sse_status);
    signed char prev_rounding_mode;

    prev_rounding_mode = env->sse_status.float_rounding_mode;
    if (!(mode & (1 << 2))) {
        switch (mode & 3) {
        case 0:
            set_float_rounding_mode(float_round_nearest_even, &env->sse_status);
            break;
        case 1:
            set_float_rounding_mode(float_round_down, &env->sse_status);
            break;
        case 2:
            set_float_rounding_mode(float_round_up, &env->sse_status);
            break;
        case 3:
            set_float_rounding_mode(float_round_to_zero, &env->sse_status);
            break;
        }
    }

    d->ZMM_S(0) = float32_round_to_int(s->ZMM_S(0), &env->sse_status);

    if (mode & (1 << 3) && !(old_flags & float_flag_inexact)) {
        set_float_exception_flags(get_float_exception_flags(&env->sse_status) &
                                  ~float_flag_inexact,
                                  &env->sse_status);
    }
    env->sse_status.float_rounding_mode = prev_rounding_mode;
}

void helper_roundsd_xmm(CPUX86State *env, Reg *d, Reg *s,
                                  uint32_t mode)
{
    uint8_t old_flags = get_float_exception_flags(&env->sse_status);
    signed char prev_rounding_mode;

    prev_rounding_mode = env->sse_status.float_rounding_mode;
    if (!(mode & (1 << 2))) {
        switch (mode & 3) {
        case 0:
            set_float_rounding_mode(float_round_nearest_even, &env->sse_status);
            break;
        case 1:
            set_float_rounding_mode(float_round_down, &env->sse_status);
            break;
        case 2:
            set_float_rounding_mode(float_round_up, &env->sse_status);
            break;
        case 3:
            set_float_rounding_mode(float_round_to_zero, &env->sse_status);
            break;
        }
    }

    d->ZMM_D(0) = float64_round_to_int(s->ZMM_D(0), &env->sse_status);

    if (mode & (1 << 3) && !(old_flags & float_flag_inexact)) {
        set_float_exception_flags(get_float_exception_flags(&env->sse_status) &
                                  ~float_flag_inexact,
                                  &env->sse_status);
    }
    env->sse_status.float_rounding_mode = prev_rounding_mode;
}
#endif

#define FBLENDP(v, s, m) (m ? s : v)
SSE_HELPER_I(helper_blendps, L, 4, FBLENDP)
SSE_HELPER_I(helper_blendpd, Q, 2, FBLENDP)
SSE_HELPER_I(helper_pblendw, W, 8, FBLENDP)

void glue(helper_dpps, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s,
                               uint32_t mask)
{
    float32 prod, iresult, iresult2;

    /*
     * We must evaluate (A+B)+(C+D), not ((A+B)+C)+D
     * to correctly round the intermediate results
     */
    if (mask & (1 << 4)) {
        iresult = float32_mul(v->ZMM_S(0), s->ZMM_S(0), &env->sse_status);
    } else {
        iresult = float32_zero;
    }
    if (mask & (1 << 5)) {
        prod = float32_mul(v->ZMM_S(1), s->ZMM_S(1), &env->sse_status);
    } else {
        prod = float32_zero;
    }
    iresult = float32_add(iresult, prod, &env->sse_status);
    if (mask & (1 << 6)) {
        iresult2 = float32_mul(v->ZMM_S(2), s->ZMM_S(2), &env->sse_status);
    } else {
        iresult2 = float32_zero;
    }
    if (mask & (1 << 7)) {
        prod = float32_mul(v->ZMM_S(3), s->ZMM_S(3), &env->sse_status);
    } else {
        prod = float32_zero;
    }
    iresult2 = float32_add(iresult2, prod, &env->sse_status);
    iresult = float32_add(iresult, iresult2, &env->sse_status);

    d->ZMM_S(0) = (mask & (1 << 0)) ? iresult : float32_zero;
    d->ZMM_S(1) = (mask & (1 << 1)) ? iresult : float32_zero;
    d->ZMM_S(2) = (mask & (1 << 2)) ? iresult : float32_zero;
    d->ZMM_S(3) = (mask & (1 << 3)) ? iresult : float32_zero;
#if SHIFT == 2
    if (mask & (1 << 4)) {
        iresult = float32_mul(v->ZMM_S(4), s->ZMM_S(4), &env->sse_status);
    } else {
        iresult = float32_zero;
    }
    if (mask & (1 << 5)) {
        prod = float32_mul(v->ZMM_S(5), s->ZMM_S(5), &env->sse_status);
    } else {
        prod = float32_zero;
    }
    iresult = float32_add(iresult, prod, &env->sse_status);
    if (mask & (1 << 6)) {
        iresult2 = float32_mul(v->ZMM_S(6), s->ZMM_S(6), &env->sse_status);
    } else {
        iresult2 = float32_zero;
    }
    if (mask & (1 << 7)) {
        prod = float32_mul(v->ZMM_S(7), s->ZMM_S(7), &env->sse_status);
    } else {
        prod = float32_zero;
    }
    iresult2 = float32_add(iresult2, prod, &env->sse_status);
    iresult = float32_add(iresult, iresult2, &env->sse_status);

    d->ZMM_S(4) = (mask & (1 << 0)) ? iresult : float32_zero;
    d->ZMM_S(5) = (mask & (1 << 1)) ? iresult : float32_zero;
    d->ZMM_S(6) = (mask & (1 << 2)) ? iresult : float32_zero;
    d->ZMM_S(7) = (mask & (1 << 3)) ? iresult : float32_zero;
#endif
}

#if SHIFT == 1
/* Oddly, there is no ymm version of dppd */
void glue(helper_dppd, SUFFIX)(CPUX86State *env,
                               Reg *d, Reg *v, Reg *s, uint32_t mask)
{
    float64 iresult;

    if (mask & (1 << 4)) {
        iresult = float64_mul(v->ZMM_D(0), s->ZMM_D(0), &env->sse_status);
    } else {
        iresult = float64_zero;
    }

    if (mask & (1 << 5)) {
        iresult = float64_add(iresult,
                              float64_mul(v->ZMM_D(1), s->ZMM_D(1),
                                          &env->sse_status),
                              &env->sse_status);
    }
    d->ZMM_D(0) = (mask & (1 << 0)) ? iresult : float64_zero;
    d->ZMM_D(1) = (mask & (1 << 1)) ? iresult : float64_zero;
}
#endif

void glue(helper_mpsadbw, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s,
                                  uint32_t offset)
{
    int s0 = (offset & 3) << 2;
    int d0 = (offset & 4) << 0;
    int i;
    uint16_t r[8];

    for (i = 0; i < 8; i++, d0++) {
        r[i] = 0;
        r[i] += abs1(v->B(d0 + 0) - s->B(s0 + 0));
        r[i] += abs1(v->B(d0 + 1) - s->B(s0 + 1));
        r[i] += abs1(v->B(d0 + 2) - s->B(s0 + 2));
        r[i] += abs1(v->B(d0 + 3) - s->B(s0 + 3));
    }
    for (i = 0; i < 8; i++) {
        d->W(i) = r[i];
    }
#if SHIFT == 2
    s0 = ((offset & 0x18) >> 1) + 16;
    d0 = ((offset & 0x20) >> 3) + 16;

    for (i = 0; i < 8; i++, d0++) {
        r[i] = 0;
        r[i] += abs1(v->B(d0 + 0) - s->B(s0 + 0));
        r[i] += abs1(v->B(d0 + 1) - s->B(s0 + 1));
        r[i] += abs1(v->B(d0 + 2) - s->B(s0 + 2));
        r[i] += abs1(v->B(d0 + 3) - s->B(s0 + 3));
    }
    for (i = 0; i < 8; i++) {
        d->W(i + 8) = r[i];
    }
#endif
}

/* SSE4.2 op helpers */
#define FCMPGTQ(d, s) ((int64_t)d > (int64_t)s ? -1 : 0)
SSE_HELPER_Q(helper_pcmpgtq, FCMPGTQ)

#if SHIFT == 1
static inline int pcmp_elen(CPUX86State *env, int reg, uint32_t ctrl)
{
    int64_t val;

    /* Presence of REX.W is indicated by a bit higher than 7 set */
    if (ctrl >> 8) {
        val = env->regs[reg];
    } else {
        val = (int32_t)env->regs[reg];
    }
    if (val < 0)
        val = 16;

    if (ctrl & 1) {
        if (val > 8) {
            return 8;
        }
    } else {
        if (val > 16) {
            return 16;
        }
    }
    return val;
}

static inline int pcmp_ilen(Reg *r, uint8_t ctrl)
{
    int val = 0;

    if (ctrl & 1) {
        while (val < 8 && r->W(val)) {
            val++;
        }
    } else {
        while (val < 16 && r->B(val)) {
            val++;
        }
    }

    return val;
}

static inline int pcmp_val(Reg *r, uint8_t ctrl, int i)
{
    switch ((ctrl >> 0) & 3) {
    case 0:
        return r->B(i);
    case 1:
        return r->W(i);
    case 2:
        return (int8_t)r->B(i);
    case 3:
    default:
        return (int16_t)r->W(i);
    }
}

static inline unsigned pcmpxstrx(CPUX86State *env, Reg *d, Reg *s,
                                 int8_t ctrl, int valids, int validd)
{
    unsigned int res = 0;
    int v;
    int j, i;
    int upper = (ctrl & 1) ? 7 : 15;

    valids--;
    validd--;

    CC_SRC = (valids < upper ? CC_Z : 0) | (validd < upper ? CC_S : 0);

    switch ((ctrl >> 2) & 3) {
    case 0:
        for (j = valids; j >= 0; j--) {
            res <<= 1;
            v = pcmp_val(s, ctrl, j);
            for (i = validd; i >= 0; i--) {
                res |= (v == pcmp_val(d, ctrl, i));
            }
        }
        break;
    case 1:
        for (j = valids; j >= 0; j--) {
            res <<= 1;
            v = pcmp_val(s, ctrl, j);
            for (i = ((validd - 1) | 1); i >= 0; i -= 2) {
                res |= (pcmp_val(d, ctrl, i - 0) >= v &&
                        pcmp_val(d, ctrl, i - 1) <= v);
            }
        }
        break;
    case 2:
        res = (1 << (upper - MAX(valids, validd))) - 1;
        res <<= MAX(valids, validd) - MIN(valids, validd);
        for (i = MIN(valids, validd); i >= 0; i--) {
            res <<= 1;
            v = pcmp_val(s, ctrl, i);
            res |= (v == pcmp_val(d, ctrl, i));
        }
        break;
    case 3:
        if (validd == -1) {
            res = (2 << upper) - 1;
            break;
        }
        for (j = valids == upper ? valids : valids - validd; j >= 0; j--) {
            res <<= 1;
            v = 1;
            for (i = MIN(valids - j, validd); i >= 0; i--) {
                v &= (pcmp_val(s, ctrl, i + j) == pcmp_val(d, ctrl, i));
            }
            res |= v;
        }
        break;
    }

    switch ((ctrl >> 4) & 3) {
    case 1:
        res ^= (2 << upper) - 1;
        break;
    case 3:
        res ^= (1 << (valids + 1)) - 1;
        break;
    }

    if (res) {
        CC_SRC |= CC_C;
    }
    if (res & 1) {
        CC_SRC |= CC_O;
    }

    return res;
}

void glue(helper_pcmpestri, SUFFIX)(CPUX86State *env, Reg *d, Reg *s,
                                    uint32_t ctrl)
{
    unsigned int res = pcmpxstrx(env, d, s, ctrl,
                                 pcmp_elen(env, R_EDX, ctrl),
                                 pcmp_elen(env, R_EAX, ctrl));

    if (res) {
        env->regs[R_ECX] = (ctrl & (1 << 6)) ? 31 - clz32(res) : ctz32(res);
    } else {
        env->regs[R_ECX] = 16 >> (ctrl & (1 << 0));
    }
}

void glue(helper_pcmpestrm, SUFFIX)(CPUX86State *env, Reg *d, Reg *s,
                                    uint32_t ctrl)
{
    int i;
    unsigned int res = pcmpxstrx(env, d, s, ctrl,
                                 pcmp_elen(env, R_EDX, ctrl),
                                 pcmp_elen(env, R_EAX, ctrl));

    if ((ctrl >> 6) & 1) {
        if (ctrl & 1) {
            for (i = 0; i < 8; i++, res >>= 1) {
                env->xmm_regs[0].W(i) = (res & 1) ? ~0 : 0;
            }
        } else {
            for (i = 0; i < 16; i++, res >>= 1) {
                env->xmm_regs[0].B(i) = (res & 1) ? ~0 : 0;
            }
        }
    } else {
        env->xmm_regs[0].Q(1) = 0;
        env->xmm_regs[0].Q(0) = res;
    }
}

void glue(helper_pcmpistri, SUFFIX)(CPUX86State *env, Reg *d, Reg *s,
                                    uint32_t ctrl)
{
    unsigned int res = pcmpxstrx(env, d, s, ctrl,
                                 pcmp_ilen(s, ctrl),
                                 pcmp_ilen(d, ctrl));

    if (res) {
        env->regs[R_ECX] = (ctrl & (1 << 6)) ? 31 - clz32(res) : ctz32(res);
    } else {
        env->regs[R_ECX] = 16 >> (ctrl & (1 << 0));
    }
}

void glue(helper_pcmpistrm, SUFFIX)(CPUX86State *env, Reg *d, Reg *s,
                                    uint32_t ctrl)
{
    int i;
    unsigned int res = pcmpxstrx(env, d, s, ctrl,
                                 pcmp_ilen(s, ctrl),
                                 pcmp_ilen(d, ctrl));

    if ((ctrl >> 6) & 1) {
        if (ctrl & 1) {
            for (i = 0; i < 8; i++, res >>= 1) {
                env->xmm_regs[0].W(i) = (res & 1) ? ~0 : 0;
            }
        } else {
            for (i = 0; i < 16; i++, res >>= 1) {
                env->xmm_regs[0].B(i) = (res & 1) ? ~0 : 0;
            }
        }
    } else {
        env->xmm_regs[0].Q(1) = 0;
        env->xmm_regs[0].Q(0) = res;
    }
}

#define CRCPOLY        0x1edc6f41
#define CRCPOLY_BITREV 0x82f63b78
target_ulong helper_crc32(uint32_t crc1, target_ulong msg, uint32_t len)
{
    target_ulong crc = (msg & ((target_ulong) -1 >>
                               (TARGET_LONG_BITS - len))) ^ crc1;

    while (len--) {
        crc = (crc >> 1) ^ ((crc & 1) ? CRCPOLY_BITREV : 0);
    }

    return crc;
}

#endif

#if SHIFT == 1
static void clmulq(uint64_t *dest_l, uint64_t *dest_h,
                          uint64_t a, uint64_t b)
{
    uint64_t al, ah, resh, resl;

    ah = 0;
    al = a;
    resh = resl = 0;

    while (b) {
        if (b & 1) {
            resl ^= al;
            resh ^= ah;
        }
        ah = (ah << 1) | (al >> 63);
        al <<= 1;
        b >>= 1;
    }

    *dest_l = resl;
    *dest_h = resh;
}
#endif

void glue(helper_pclmulqdq, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s,
                                    uint32_t ctrl)
{
    uint64_t a, b;

    a = v->Q((ctrl & 1) != 0);
    b = s->Q((ctrl & 16) != 0);
    clmulq(&d->Q(0), &d->Q(1), a, b);
#if SHIFT == 2
    a = v->Q(((ctrl & 1) != 0) + 2);
    b = s->Q(((ctrl & 16) != 0) + 2);
    clmulq(&d->Q(2), &d->Q(3), a, b);
#endif
}

void glue(helper_aesdec, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    int i;
    Reg st = *v;
    Reg rk = *s;

    for (i = 0 ; i < 4 ; i++) {
        d->L(i) = rk.L(i) ^ bswap32(AES_Td0[st.B(AES_ishifts[4 * i + 0])] ^
                                    AES_Td1[st.B(AES_ishifts[4 * i + 1])] ^
                                    AES_Td2[st.B(AES_ishifts[4 * i + 2])] ^
                                    AES_Td3[st.B(AES_ishifts[4 * i + 3])]);
    }
#if SHIFT == 2
    for (i = 0 ; i < 4 ; i++) {
        d->L(i + 4) = rk.L(i + 4) ^ bswap32(
                AES_Td0[st.B(AES_ishifts[4 * i + 0] + 16)] ^
                AES_Td1[st.B(AES_ishifts[4 * i + 1] + 16)] ^
                AES_Td2[st.B(AES_ishifts[4 * i + 2] + 16)] ^
                AES_Td3[st.B(AES_ishifts[4 * i + 3] + 16)]);
    }
#endif
}

void glue(helper_aesdeclast, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    int i;
    Reg st = *v;
    Reg rk = *s;

    for (i = 0; i < 16; i++) {
        d->B(i) = rk.B(i) ^ (AES_isbox[st.B(AES_ishifts[i])]);
    }
#if SHIFT == 2
    for (i = 0; i < 16; i++) {
        d->B(i + 16) = rk.B(i + 16) ^ (AES_isbox[st.B(AES_ishifts[i] + 16)]);
    }
#endif
}

void glue(helper_aesenc, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    int i;
    Reg st = *v;
    Reg rk = *s;

    for (i = 0 ; i < 4 ; i++) {
        d->L(i) = rk.L(i) ^ bswap32(AES_Te0[st.B(AES_shifts[4 * i + 0])] ^
                                    AES_Te1[st.B(AES_shifts[4 * i + 1])] ^
                                    AES_Te2[st.B(AES_shifts[4 * i + 2])] ^
                                    AES_Te3[st.B(AES_shifts[4 * i + 3])]);
    }
#if SHIFT == 2
    for (i = 0 ; i < 4 ; i++) {
        d->L(i + 4) = rk.L(i + 4) ^ bswap32(
                AES_Te0[st.B(AES_shifts[4 * i + 0] + 16)] ^
                AES_Te1[st.B(AES_shifts[4 * i + 1] + 16)] ^
                AES_Te2[st.B(AES_shifts[4 * i + 2] + 16)] ^
                AES_Te3[st.B(AES_shifts[4 * i + 3] + 16)]);
    }
#endif
}

void glue(helper_aesenclast, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    int i;
    Reg st = *v;
    Reg rk = *s;

    for (i = 0; i < 16; i++) {
        d->B(i) = rk.B(i) ^ (AES_sbox[st.B(AES_shifts[i])]);
    }
#if SHIFT == 2
    for (i = 0; i < 16; i++) {
        d->B(i + 16) = rk.B(i + 16) ^ (AES_sbox[st.B(AES_shifts[i] + 16)]);
    }
#endif
}

#if SHIFT == 1
void glue(helper_aesimc, SUFFIX)(CPUX86State *env, Reg *d, Reg *s)
{
    int i;
    Reg tmp = *s;

    for (i = 0 ; i < 4 ; i++) {
        d->L(i) = bswap32(AES_imc[tmp.B(4 * i + 0)][0] ^
                          AES_imc[tmp.B(4 * i + 1)][1] ^
                          AES_imc[tmp.B(4 * i + 2)][2] ^
                          AES_imc[tmp.B(4 * i + 3)][3]);
    }
}

void glue(helper_aeskeygenassist, SUFFIX)(CPUX86State *env, Reg *d, Reg *s,
                                          uint32_t ctrl)
{
    int i;
    Reg tmp = *s;

    for (i = 0 ; i < 4 ; i++) {
        d->B(i) = AES_sbox[tmp.B(i + 4)];
        d->B(i + 8) = AES_sbox[tmp.B(i + 12)];
    }
    d->L(1) = (d->L(0) << 24 | d->L(0) >> 8) ^ ctrl;
    d->L(3) = (d->L(2) << 24 | d->L(2) >> 8) ^ ctrl;
}
#endif
#endif

#if SHIFT >= 1
void glue(helper_vbroadcastb, SUFFIX)(CPUX86State *env, Reg *d, Reg *s)
{
    uint8_t val = s->B(0);
    int i;

    for (i = 0; i < 16 * SHIFT; i++) {
        d->B(i) = val;
    }
}

void glue(helper_vbroadcastw, SUFFIX)(CPUX86State *env, Reg *d, Reg *s)
{
    uint16_t val = s->W(0);
    int i;

    for (i = 0; i < 8 * SHIFT; i++) {
        d->W(i) = val;
    }
}

void glue(helper_vbroadcastl, SUFFIX)(CPUX86State *env, Reg *d, Reg *s)
{
    uint32_t val = s->L(0);
    int i;

    for (i = 0; i < 8 * SHIFT; i++) {
        d->L(i) = val;
    }
}

void glue(helper_vbroadcastq, SUFFIX)(CPUX86State *env, Reg *d, Reg *s)
{
    uint64_t val = s->Q(0);
    d->Q(0) = val;
    d->Q(1) = val;
#if SHIFT == 2
    d->Q(2) = val;
    d->Q(3) = val;
#endif
}

void glue(helper_vpermilpd, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    uint64_t r0, r1;

    r0 = v->Q((s->Q(0) >> 1) & 1);
    r1 = v->Q((s->Q(1) >> 1) & 1);
    d->Q(0) = r0;
    d->Q(1) = r1;
#if SHIFT == 2
    r0 = v->Q(((s->Q(2) >> 1) & 1) + 2);
    r1 = v->Q(((s->Q(3) >> 1) & 1) + 2);
    d->Q(2) = r0;
    d->Q(3) = r1;
#endif
}

void glue(helper_vpermilps, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    uint32_t r0, r1, r2, r3;

    r0 = v->L(s->L(0) & 3);
    r1 = v->L(s->L(1) & 3);
    r2 = v->L(s->L(2) & 3);
    r3 = v->L(s->L(3) & 3);
    d->L(0) = r0;
    d->L(1) = r1;
    d->L(2) = r2;
    d->L(3) = r3;
#if SHIFT == 2
    r0 = v->L((s->L(4) & 3) + 4);
    r1 = v->L((s->L(5) & 3) + 4);
    r2 = v->L((s->L(6) & 3) + 4);
    r3 = v->L((s->L(7) & 3) + 4);
    d->L(4) = r0;
    d->L(5) = r1;
    d->L(6) = r2;
    d->L(7) = r3;
#endif
}

void glue(helper_vpermilpd_imm, SUFFIX)(CPUX86State *env,
                                        Reg *d, Reg *s, uint32_t order)
{
    uint64_t r0, r1;

    r0 = s->Q((order >> 0) & 1);
    r1 = s->Q((order >> 1) & 1);
    d->Q(0) = r0;
    d->Q(1) = r1;
#if SHIFT == 2
    r0 = s->Q(((order >> 2) & 1) + 2);
    r1 = s->Q(((order >> 3) & 1) + 2);
    d->Q(2) = r0;
    d->Q(3) = r1;
#endif
}

void glue(helper_vpermilps_imm, SUFFIX)(CPUX86State *env,
                                        Reg *d, Reg *s, uint32_t order)
{
    uint32_t r0, r1, r2, r3;

    r0 = s->L((order >> 0) & 3);
    r1 = s->L((order >> 2) & 3);
    r2 = s->L((order >> 4) & 3);
    r3 = s->L((order >> 6) & 3);
    d->L(0) = r0;
    d->L(1) = r1;
    d->L(2) = r2;
    d->L(3) = r3;
#if SHIFT == 2
    r0 = s->L(((order >> 0) & 3) + 4);
    r1 = s->L(((order >> 2) & 3) + 4);
    r2 = s->L(((order >> 4) & 3) + 4);
    r3 = s->L(((order >> 6) & 3) + 4);
    d->L(4) = r0;
    d->L(5) = r1;
    d->L(6) = r2;
    d->L(7) = r3;
#endif
}

#if SHIFT == 1
#define FPSRLVD(x, c) (c < 32 ? ((x) >> c) : 0)
#define FPSRLVQ(x, c) (c < 64 ? ((x) >> c) : 0)
#define FPSRAVD(x, c) ((int32_t)(x) >> (c < 64 ? c : 31))
#define FPSRAVQ(x, c) ((int64_t)(x) >> (c < 64 ? c : 63))
#define FPSLLVD(x, c) (c < 32 ? ((x) << c) : 0)
#define FPSLLVQ(x, c) (c < 64 ? ((x) << c) : 0)
#endif

SSE_HELPER_L(helper_vpsrlvd, FPSRLVD)
SSE_HELPER_L(helper_vpsravd, FPSRAVD)
SSE_HELPER_L(helper_vpsllvd, FPSLLVD)

SSE_HELPER_Q(helper_vpsrlvq, FPSRLVQ)
SSE_HELPER_Q(helper_vpsravq, FPSRAVQ)
SSE_HELPER_Q(helper_vpsllvq, FPSLLVQ)

void glue(helper_vtestps, SUFFIX)(CPUX86State *env, Reg *d, Reg *s)
{
    uint32_t zf = (s->L(0) &  d->L(0)) | (s->L(1) &  d->L(1));
    uint32_t cf = (s->L(0) & ~d->L(0)) | (s->L(1) & ~d->L(1));

    zf |= (s->L(2) &  d->L(2)) | (s->L(3) &  d->L(3));
    cf |= (s->L(2) & ~d->L(2)) | (s->L(3) & ~d->L(3));
#if SHIFT == 2
    zf |= (s->L(4) &  d->L(4)) | (s->L(5) &  d->L(5));
    cf |= (s->L(4) & ~d->L(4)) | (s->L(5) & ~d->L(5));
    zf |= (s->L(6) &  d->L(6)) | (s->L(7) &  d->L(7));
    cf |= (s->L(6) & ~d->L(6)) | (s->L(7) & ~d->L(7));
#endif
    CC_SRC = ((zf >> 31) ? 0 : CC_Z) | ((cf >> 31) ? 0 : CC_C);
}

void glue(helper_vtestpd, SUFFIX)(CPUX86State *env, Reg *d, Reg *s)
{
    uint64_t zf = (s->Q(0) &  d->Q(0)) | (s->Q(1) &  d->Q(1));
    uint64_t cf = (s->Q(0) & ~d->Q(0)) | (s->Q(1) & ~d->Q(1));

#if SHIFT == 2
    zf |= (s->Q(2) &  d->Q(2)) | (s->Q(3) &  d->Q(3));
    cf |= (s->Q(2) & ~d->Q(2)) | (s->Q(3) & ~d->Q(3));
#endif
    CC_SRC = ((zf >> 63) ? 0 : CC_Z) | ((cf >> 63) ? 0 : CC_C);
}

void glue(helper_vpmaskmovd_st, SUFFIX)(CPUX86State *env,
                                        Reg *s, Reg *v, target_ulong a0)
{
    int i;

    for (i = 0; i < (2 << SHIFT); i++) {
        if (v->L(i) >> 31) {
            cpu_stl_data_ra(env, a0 + i * 4, s->L(i), GETPC());
        }
    }
}

void glue(helper_vpmaskmovq_st, SUFFIX)(CPUX86State *env,
                                        Reg *s, Reg *v, target_ulong a0)
{
    int i;

    for (i = 0; i < (1 << SHIFT); i++) {
        if (v->Q(i) >> 63) {
            cpu_stq_data_ra(env, a0 + i * 8, s->Q(i), GETPC());
        }
    }
}

void glue(helper_vpmaskmovd, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    d->L(0) = (v->L(0) >> 31) ? s->L(0) : 0;
    d->L(1) = (v->L(1) >> 31) ? s->L(1) : 0;
    d->L(2) = (v->L(2) >> 31) ? s->L(2) : 0;
    d->L(3) = (v->L(3) >> 31) ? s->L(3) : 0;
#if SHIFT == 2
    d->L(4) = (v->L(4) >> 31) ? s->L(4) : 0;
    d->L(5) = (v->L(5) >> 31) ? s->L(5) : 0;
    d->L(6) = (v->L(6) >> 31) ? s->L(6) : 0;
    d->L(7) = (v->L(7) >> 31) ? s->L(7) : 0;
#endif
}

void glue(helper_vpmaskmovq, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    d->Q(0) = (v->Q(0) >> 63) ? s->Q(0) : 0;
    d->Q(1) = (v->Q(1) >> 63) ? s->Q(1) : 0;
#if SHIFT == 2
    d->Q(2) = (v->Q(2) >> 63) ? s->Q(2) : 0;
    d->Q(3) = (v->Q(3) >> 63) ? s->Q(3) : 0;
#endif
}

#define VGATHER_HELPER(scale)                                       \
void glue(helper_vpgatherdd ## scale, SUFFIX)(CPUX86State *env,     \
        Reg *d, Reg *v, Reg *s, target_ulong a0)                    \
{                                                                   \
    int i;                                                          \
    for (i = 0; i < (2 << SHIFT); i++) {                            \
        if (v->L(i) >> 31) {                                        \
            target_ulong addr = a0                                  \
                + ((target_ulong)(int32_t)s->L(i) << scale);        \
            d->L(i) = cpu_ldl_data_ra(env, addr, GETPC());          \
        }                                                           \
        v->L(i) = 0;                                                \
    }                                                               \
}                                                                   \
void glue(helper_vpgatherdq ## scale, SUFFIX)(CPUX86State *env,     \
        Reg *d, Reg *v, Reg *s, target_ulong a0)                    \
{                                                                   \
    int i;                                                          \
    for (i = 0; i < (1 << SHIFT); i++) {                            \
        if (v->Q(i) >> 63) {                                        \
            target_ulong addr = a0                                  \
                + ((target_ulong)(int32_t)s->L(i) << scale);        \
            d->Q(i) = cpu_ldq_data_ra(env, addr, GETPC());          \
        }                                                           \
        v->Q(i) = 0;                                                \
    }                                                               \
}                                                                   \
void glue(helper_vpgatherqd ## scale, SUFFIX)(CPUX86State *env,     \
        Reg *d, Reg *v, Reg *s, target_ulong a0)                    \
{                                                                   \
    int i;                                                          \
    for (i = 0; i < (1 << SHIFT); i++) {                            \
        if (v->L(i) >> 31) {                                        \
            target_ulong addr = a0                                  \
                + ((target_ulong)(int64_t)s->Q(i) << scale);        \
            d->L(i) = cpu_ldl_data_ra(env, addr, GETPC());          \
        }                                                           \
        v->L(i) = 0;                                                \
    }                                                               \
    d->Q(SHIFT) = 0;                                                    \
    v->Q(SHIFT) = 0;                                                    \
    YMM_ONLY(                                                       \
    d->Q(3) = 0;                                                    \
    v->Q(3) = 0;                                                    \
    )                                                               \
}                                                                   \
void glue(helper_vpgatherqq ## scale, SUFFIX)(CPUX86State *env,     \
        Reg *d, Reg *v, Reg *s, target_ulong a0)                    \
{                                                                   \
    int i;                                                          \
    for (i = 0; i < (1 << SHIFT); i++) {                            \
        if (v->Q(i) >> 63) {                                        \
            target_ulong addr = a0                                  \
                + ((target_ulong)(int64_t)s->Q(i) << scale);        \
            d->Q(i) = cpu_ldq_data_ra(env, addr, GETPC());          \
        }                                                           \
        v->Q(i) = 0;                                                \
    }                                                               \
}

VGATHER_HELPER(0)
VGATHER_HELPER(1)
VGATHER_HELPER(2)
VGATHER_HELPER(3)

#if SHIFT == 2
void glue(helper_vbroadcastdq, SUFFIX)(CPUX86State *env, Reg *d, Reg *s)
{
    d->Q(0) = s->Q(0);
    d->Q(1) = s->Q(1);
    d->Q(2) = s->Q(0);
    d->Q(3) = s->Q(1);
}

void helper_vzeroall(CPUX86State *env)
{
    int i;

    for (i = 0; i < 8; i++) {
        env->xmm_regs[i].ZMM_Q(0) = 0;
        env->xmm_regs[i].ZMM_Q(1) = 0;
        env->xmm_regs[i].ZMM_Q(2) = 0;
        env->xmm_regs[i].ZMM_Q(3) = 0;
    }
}

void helper_vzeroupper(CPUX86State *env)
{
    int i;

    for (i = 0; i < 8; i++) {
        env->xmm_regs[i].ZMM_Q(2) = 0;
        env->xmm_regs[i].ZMM_Q(3) = 0;
    }
}

#ifdef TARGET_X86_64
void helper_vzeroall_hi8(CPUX86State *env)
{
    int i;

    for (i = 8; i < 16; i++) {
        env->xmm_regs[i].ZMM_Q(0) = 0;
        env->xmm_regs[i].ZMM_Q(1) = 0;
        env->xmm_regs[i].ZMM_Q(2) = 0;
        env->xmm_regs[i].ZMM_Q(3) = 0;
    }
}

void helper_vzeroupper_hi8(CPUX86State *env)
{
    int i;

    for (i = 8; i < 16; i++) {
        env->xmm_regs[i].ZMM_Q(2) = 0;
        env->xmm_regs[i].ZMM_Q(3) = 0;
    }
}
#endif

void helper_vpermdq_ymm(CPUX86State *env,
                        Reg *d, Reg *v, Reg *s, uint32_t order)
{
    uint64_t r0, r1, r2, r3;

    switch (order & 3) {
    case 0:
        r0 = v->Q(0);
        r1 = v->Q(1);
        break;
    case 1:
        r0 = v->Q(2);
        r1 = v->Q(3);
        break;
    case 2:
        r0 = s->Q(0);
        r1 = s->Q(1);
        break;
    case 3:
        r0 = s->Q(2);
        r1 = s->Q(3);
        break;
    }
    switch ((order >> 4) & 3) {
    case 0:
        r2 = v->Q(0);
        r3 = v->Q(1);
        break;
    case 1:
        r2 = v->Q(2);
        r3 = v->Q(3);
        break;
    case 2:
        r2 = s->Q(0);
        r3 = s->Q(1);
        break;
    case 3:
        r2 = s->Q(2);
        r3 = s->Q(3);
        break;
    }
    d->Q(0) = r0;
    d->Q(1) = r1;
    d->Q(2) = r2;
    d->Q(3) = r3;
}

void helper_vpermq_ymm(CPUX86State *env, Reg *d, Reg *s, uint32_t order)
{
    uint64_t r0, r1, r2, r3;
    r0 = s->Q(order & 3);
    r1 = s->Q((order >> 2) & 3);
    r2 = s->Q((order >> 4) & 3);
    r3 = s->Q((order >> 6) & 3);
    d->Q(0) = r0;
    d->Q(1) = r1;
    d->Q(2) = r2;
    d->Q(3) = r3;
}

void helper_vpermd_ymm(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    uint32_t r[8];
    int i;

    for (i = 0; i < 8; i++) {
        r[i] = s->L(v->L(i) & 7);
    }
    for (i = 0; i < 8; i++) {
        d->L(i) = r[i];
    }
}

#endif
#endif

#undef SHIFT_HELPER_W
#undef SHIFT_HELPER_L
#undef SHIFT_HELPER_Q
#undef SSE_HELPER_S
#undef SSE_HELPER_CMP

#undef SHIFT
#undef XMM_ONLY
#undef YMM_ONLY
#undef Reg
#undef B
#undef W
#undef L
#undef Q
#undef SUFFIX
