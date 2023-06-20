/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch vector utilitites
 *
 * Copyright (c) 2023 Loongson Technology Corporation Limited
 */

#ifndef LOONGARCH_VEC_H
#define LOONGARCH_VEC_H

#include "fpu/softfloat.h"

#if HOST_BIG_ENDIAN
#define B(x)  B[15 - (x)]
#define H(x)  H[7 - (x)]
#define W(x)  W[3 - (x)]
#define D(x)  D[1 - (x)]
#define UB(x) UB[15 - (x)]
#define UH(x) UH[7 - (x)]
#define UW(x) UW[3 - (x)]
#define UD(x) UD[1 - (x)]
#define Q(x)  Q[x]
#define XB(x)  XB[31 - (x)]
#define XH(x)  XH[15 - (x)]
#define XW(x)  XW[7 - (x)]
#define XD(x)  XD[3 - (x)]
#define UXB(x) UXB[31 - (x)]
#define UXH(x) UXH[15 - (x)]
#define UXW(x) UXW[7 - (x)]
#define UXD(x) UXD[3 - (x)]
#define XQ(x)  XQ[1 - (x)]
#else
#define B(x)  B[x]
#define H(x)  H[x]
#define W(x)  W[x]
#define D(x)  D[x]
#define UB(x) UB[x]
#define UH(x) UH[x]
#define UW(x) UW[x]
#define UD(x) UD[x]
#define Q(x)  Q[x]
#define XB(x)  XB[x]
#define XH(x)  XH[x]
#define XW(x)  XW[x]
#define XD(x)  XD[x]
#define UXB(x) UXB[x]
#define UXH(x) UXH[x]
#define UXW(x) UXW[x]
#define UXD(x) UXD[x]
#define XQ(x)  XQ[x]
#endif /* HOST_BIG_ENDIAN */

#define DO_ADD(a, b)  (a + b)
#define DO_SUB(a, b)  (a - b)

#define DO_VAVG(a, b)  ((a >> 1) + (b >> 1) + (a & b & 1))
#define DO_VAVGR(a, b) ((a >> 1) + (b >> 1) + ((a | b) & 1))

#define DO_VABSD(a, b)  ((a > b) ? (a - b) : (b - a))

#define DO_VABS(a)      ((a < 0) ? (-a) : (a))

#define DO_MIN(a, b)    (a < b ? a : b)
#define DO_MAX(a, b)    (a > b ? a : b)

#define DO_MUL(a, b)    (a * b)

#define DO_MADD(a, b, c)  (a + b * c)
#define DO_MSUB(a, b, c)  (a - b * c)

#define DO_DIVU(N, M) (unlikely(M == 0) ? 0 : N / M)
#define DO_REMU(N, M) (unlikely(M == 0) ? 0 : N % M)
#define DO_DIV(N, M)  (unlikely(M == 0) ? 0 :\
        unlikely((N == -N) && (M == (__typeof(N))(-1))) ? N : N / M)
#define DO_REM(N, M)  (unlikely(M == 0) ? 0 :\
        unlikely((N == -N) && (M == (__typeof(N))(-1))) ? 0 : N % M)

#define DO_SIGNCOV(a, b)  (a == 0 ? 0 : a < 0 ? -b : b)

#define R_SHIFT(a, b) (a >> b)

#define DO_CLO_B(N)  (clz32(~N & 0xff) - 24)
#define DO_CLO_H(N)  (clz32(~N & 0xffff) - 16)
#define DO_CLO_W(N)  (clz32(~N))
#define DO_CLO_D(N)  (clz64(~N))
#define DO_CLZ_B(N)  (clz32(N) - 24)
#define DO_CLZ_H(N)  (clz32(N) - 16)
#define DO_CLZ_W(N)  (clz32(N))
#define DO_CLZ_D(N)  (clz64(N))

#define DO_BITCLR(a, bit) (a & ~(1ull << bit))
#define DO_BITSET(a, bit) (a | 1ull << bit)
#define DO_BITREV(a, bit) (a ^ (1ull << bit))

#define VSEQ(a, b) (a == b ? -1 : 0)
#define VSLE(a, b) (a <= b ? -1 : 0)
#define VSLT(a, b) (a < b ? -1 : 0)

uint64_t do_vmskltz_b(int64_t val);
uint64_t do_vmskltz_h(int64_t val);
uint64_t do_vmskltz_w(int64_t val);
uint64_t do_vmskltz_d(int64_t val);
uint64_t do_vmskez_b(uint64_t val);

void vec_update_fcsr0_mask(CPULoongArchState *env, uintptr_t pc, int mask);
void vec_update_fcsr0(CPULoongArchState *env, uintptr_t pc);
void vec_clear_cause(CPULoongArchState *env);

uint32_t do_flogb_32(CPULoongArchState *env, uint32_t fj);
uint64_t do_flogb_64(CPULoongArchState *env, uint64_t fj);
uint32_t do_fsqrt_32(CPULoongArchState *env, uint32_t fj);
uint64_t do_fsqrt_64(CPULoongArchState *env, uint64_t fj);
uint32_t do_frecip_32(CPULoongArchState *env, uint32_t fj);
uint64_t do_frecip_64(CPULoongArchState *env, uint64_t fj);
uint32_t do_frsqrt_32(CPULoongArchState *env, uint32_t fj);
uint64_t do_frsqrt_64(CPULoongArchState *env, uint64_t fj);

uint64_t vfcmp_common(CPULoongArchState *env,
                      FloatRelation cmp, uint32_t flags);

bool do_match2(uint64_t n, uint64_t m0, uint64_t m1, int esz);

#endif /* LOONGARCH_VEC_H */
