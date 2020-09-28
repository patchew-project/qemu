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

#include "qemu/osdep.h"
#include <math.h>
#include "fma_emu.h"
#include "arch.h"
#include "macros.h"

#define BITS_MASK_8 0x5555555555555555ULL
#define PAIR_MASK_8 0x3333333333333333ULL
#define NYBL_MASK_8 0x0f0f0f0f0f0f0f0fULL
#define BYTE_MASK_8 0x00ff00ff00ff00ffULL
#define HALF_MASK_8 0x0000ffff0000ffffULL
#define WORD_MASK_8 0x00000000ffffffffULL

uint64_t interleave(uint32_t odd, uint32_t even)
{
    /* Convert to long long */
    uint64_t myodd = odd;
    uint64_t myeven = even;
    /* First, spread bits out */
    myodd = (myodd | (myodd << 16)) & HALF_MASK_8;
    myeven = (myeven | (myeven << 16)) & HALF_MASK_8;
    myodd = (myodd | (myodd << 8)) & BYTE_MASK_8;
    myeven = (myeven | (myeven << 8)) & BYTE_MASK_8;
    myodd = (myodd | (myodd << 4)) & NYBL_MASK_8;
    myeven = (myeven | (myeven << 4)) & NYBL_MASK_8;
    myodd = (myodd | (myodd << 2)) & PAIR_MASK_8;
    myeven = (myeven | (myeven << 2)) & PAIR_MASK_8;
    myodd = (myodd | (myodd << 1)) & BITS_MASK_8;
    myeven = (myeven | (myeven << 1)) & BITS_MASK_8;
    /* Now OR together */
    return myeven | (myodd << 1);
}

uint64_t deinterleave(uint64_t src)
{
    /* Get odd and even bits */
    uint64_t myodd = ((src >> 1) & BITS_MASK_8);
    uint64_t myeven = (src & BITS_MASK_8);

    /* Unspread bits */
    myeven = (myeven | (myeven >> 1)) & PAIR_MASK_8;
    myodd = (myodd | (myodd >> 1)) & PAIR_MASK_8;
    myeven = (myeven | (myeven >> 2)) & NYBL_MASK_8;
    myodd = (myodd | (myodd >> 2)) & NYBL_MASK_8;
    myeven = (myeven | (myeven >> 4)) & BYTE_MASK_8;
    myodd = (myodd | (myodd >> 4)) & BYTE_MASK_8;
    myeven = (myeven | (myeven >> 8)) & HALF_MASK_8;
    myodd = (myodd | (myodd >> 8)) & HALF_MASK_8;
    myeven = (myeven | (myeven >> 16)) & WORD_MASK_8;
    myodd = (myodd | (myodd >> 16)) & WORD_MASK_8;

    /* Return odd bits in upper half */
    return myeven | (myodd << 32);
}

uint32_t carry_from_add64(uint64_t a, uint64_t b, uint32_t c)
{
    uint64_t tmpa, tmpb, tmpc;
    tmpa = fGETUWORD(0, a);
    tmpb = fGETUWORD(0, b);
    tmpc = tmpa + tmpb + c;
    tmpa = fGETUWORD(1, a);
    tmpb = fGETUWORD(1, b);
    tmpc = tmpa + tmpb + fGETUWORD(1, tmpc);
    tmpc = fGETUWORD(1, tmpc);
    return tmpc;
}

int32_t conv_round(int32_t a, int n)
{
    int64_t val;

    if (n == 0) {
        val = a;
    } else if ((a & ((1 << (n - 1)) - 1)) == 0) {    /* N-1..0 all zero? */
        /* Add LSB from int part */
        val = ((fSE32_64(a)) + (int64_t) (((uint32_t) ((1 << n) & a)) >> 1));
    } else {
        val = ((fSE32_64(a)) + (1 << (n - 1)));
    }

    val = val >> n;
    return (int32_t)val;
}

size16s_t cast8s_to_16s(int64_t a)
{
    size16s_t result = {.hi = 0, .lo = 0};
    result.lo = a;
    if (a < 0) {
        result.hi = -1;
    }
    return result;
}

int64_t cast16s_to_8s(size16s_t a)
{
    return a.lo;
}

size16s_t add128(size16s_t a, size16s_t b)
{
    size16s_t result = {.hi = 0, .lo = 0};
    result.lo = a.lo + b.lo;
    result.hi = a.hi + b.hi;

    if (result.lo < b.lo) {
        result.hi++;
    }

    return result;
}

size16s_t sub128(size16s_t a, size16s_t b)
{
    size16s_t result = {.hi = 0, .lo = 0};
    result.lo = a.lo - b.lo;
    result.hi = a.hi - b.hi;
    if (result.lo > a.lo) {
        result.hi--;
    }

    return result;
}

size16s_t shiftr128(size16s_t a, uint32_t n)
{
    size16s_t result;
    result.lo = (a.lo >> n) | (a.hi << (64 - n));
    result.hi = a.hi >> n;
    return result;
}

size16s_t shiftl128(size16s_t a, uint32_t n)
{
    size16s_t result;
    result.lo = a.lo << n;
    result.hi = (a.hi << n) | (a.lo >> (64 - n));
    return result;
}

size16s_t and128(size16s_t a, size16s_t b)
{
    size16s_t result;
    result.lo = a.lo & b.lo;
    result.hi = a.hi & b.hi;
    return result;
}

/* Floating Point Stuff */

static const int roundingmodes[] = {
    FE_TONEAREST,
    FE_TOWARDZERO,
    FE_DOWNWARD,
    FE_UPWARD
};

void arch_fpop_start(CPUHexagonState *env)
{
    fegetenv(&env->fenv);
    feclearexcept(FE_ALL_EXCEPT);
    fesetround(roundingmodes[fREAD_REG_FIELD(USR, USR_FPRND)]);
}

#define NOTHING             /* Don't do anything */

#define TEST_FLAG(LIBCF, MYF, MYE) \
    do { \
        if (fetestexcept(LIBCF)) { \
            if (GET_USR_FIELD(USR_##MYF) == 0) { \
                SET_USR_FIELD(USR_##MYF, 1); \
                if (GET_USR_FIELD(USR_##MYE)) { \
                    NOTHING \
                } \
            } \
        } \
    } while (0)

void arch_fpop_end(CPUHexagonState *env)
{
    if (fetestexcept(FE_ALL_EXCEPT)) {
        TEST_FLAG(FE_INEXACT, FPINPF, FPINPE);
        TEST_FLAG(FE_DIVBYZERO, FPDBZF, FPDBZE);
        TEST_FLAG(FE_INVALID, FPINVF, FPINVE);
        TEST_FLAG(FE_OVERFLOW, FPOVFF, FPOVFE);
        TEST_FLAG(FE_UNDERFLOW, FPUNFF, FPUNFE);
    }
    fesetenv(&env->fenv);
}

#undef TEST_FLAG


void arch_raise_fpflag(unsigned int flags)
{
    feraiseexcept(flags);
}

int arch_sf_recip_common(int32_t *Rs, int32_t *Rt, int32_t *Rd, int *adjust)
{
    int n_class;
    int d_class;
    int n_exp;
    int d_exp;
    int ret = 0;
    int32_t RsV, RtV, RdV;
    int PeV = 0;
    RsV = *Rs;
    RtV = *Rt;
    n_class = fpclassify(fFLOAT(RsV));
    d_class = fpclassify(fFLOAT(RtV));
    if ((n_class == FP_NAN) && (d_class == FP_NAN)) {
        if (fGETBIT(22, RsV & RtV) == 0) {
            fRAISEFLAGS(FE_INVALID);
        }
        RdV = RsV = RtV = fSFNANVAL();
    } else if (n_class == FP_NAN) {
        if (fGETBIT(22, RsV) == 0) {
            fRAISEFLAGS(FE_INVALID);
        }
        RdV = RsV = RtV = fSFNANVAL();
    } else if (d_class == FP_NAN) {
        /* or put NaN in num/den fixup? */
        if (fGETBIT(22, RtV) == 0) {
            fRAISEFLAGS(FE_INVALID);
        }
        RdV = RsV = RtV = fSFNANVAL();
    } else if ((n_class == FP_INFINITE) && (d_class == FP_INFINITE)) {
        /* or put Inf in num fixup? */
        RdV = RsV = RtV = fSFNANVAL();
        fRAISEFLAGS(FE_INVALID);
    } else if ((n_class == FP_ZERO) && (d_class == FP_ZERO)) {
        /* or put zero in num fixup? */
        RdV = RsV = RtV = fSFNANVAL();
        fRAISEFLAGS(FE_INVALID);
    } else if (d_class == FP_ZERO) {
        /* or put Inf in num fixup? */
        RsV = fSFINFVAL(RsV ^ RtV);
        RtV = fSFONEVAL(0);
        RdV = fSFONEVAL(0);
        if (n_class != FP_INFINITE) {
            fRAISEFLAGS(FE_DIVBYZERO);
        }
    } else if (d_class == FP_INFINITE) {
        RsV = 0x80000000 & (RsV ^ RtV);
        RtV = fSFONEVAL(0);
        RdV = fSFONEVAL(0);
    } else if (n_class == FP_ZERO) {
        /* Does this just work itself out? */
        /* No, 0/Inf causes problems. */
        RsV = 0x80000000 & (RsV ^ RtV);
        RtV = fSFONEVAL(0);
        RdV = fSFONEVAL(0);
    } else if (n_class == FP_INFINITE) {
        /* Does this just work itself out? */
        RsV = fSFINFVAL(RsV ^ RtV);
        RtV = fSFONEVAL(0);
        RdV = fSFONEVAL(0);
    } else {
        PeV = 0x00;
        /* Basic checks passed */
        n_exp = fSF_GETEXP(RsV);
        d_exp = fSF_GETEXP(RtV);
        if ((n_exp - d_exp + fSF_BIAS()) <= fSF_MANTBITS()) {
            /* Near quotient underflow / inexact Q */
            PeV = 0x80;
            RtV = fSF_MUL_POW2(RtV, -64);
            RsV = fSF_MUL_POW2(RsV, 64);
        } else if ((n_exp - d_exp + fSF_BIAS()) > (fSF_MAXEXP() - 24)) {
            /* Near quotient overflow */
            PeV = 0x40;
            RtV = fSF_MUL_POW2(RtV, 32);
            RsV = fSF_MUL_POW2(RsV, -32);
        } else if (n_exp <= fSF_MANTBITS() + 2) {
            RtV = fSF_MUL_POW2(RtV, 64);
            RsV = fSF_MUL_POW2(RsV, 64);
        } else if (d_exp <= 1) {
            RtV = fSF_MUL_POW2(RtV, 32);
            RsV = fSF_MUL_POW2(RsV, 32);
        } else if (d_exp > 252) {
            RtV = fSF_MUL_POW2(RtV, -32);
            RsV = fSF_MUL_POW2(RsV, -32);
        }
        RdV = 0;
        ret = 1;
    }
    *Rs = RsV;
    *Rt = RtV;
    *Rd = RdV;
    *adjust = PeV;
    return ret;
}

int arch_sf_invsqrt_common(int32_t *Rs, int32_t *Rd, int *adjust)
{
    int r_class;
    int32_t RsV, RdV;
    int PeV = 0;
    int r_exp;
    int ret = 0;
    RsV = *Rs;
    r_class = fpclassify(fFLOAT(RsV));
    if (r_class == FP_NAN) {
        if (fGETBIT(22, RsV) == 0) {
            fRAISEFLAGS(FE_INVALID);
        }
        RdV = RsV = fSFNANVAL();
    } else if (fFLOAT(RsV) < 0.0) {
        /* Negative nonzero values are NaN */
        fRAISEFLAGS(FE_INVALID);
        RsV = fSFNANVAL();
        RdV = fSFNANVAL();
    } else if (r_class == FP_INFINITE) {
        /* or put Inf in num fixup? */
        RsV = fSFINFVAL(-1);
        RdV = fSFINFVAL(-1);
    } else if (r_class == FP_ZERO) {
        /* or put zero in num fixup? */
        RdV = fSFONEVAL(0);
    } else {
        PeV = 0x00;
        /* Basic checks passed */
        r_exp = fSF_GETEXP(RsV);
        if (r_exp <= 24) {
            RsV = fSF_MUL_POW2(RsV, 64);
            PeV = 0xe0;
        }
        RdV = 0;
        ret = 1;
    }
    *Rs = RsV;
    *Rd = RdV;
    *adjust = PeV;
    return ret;
}

