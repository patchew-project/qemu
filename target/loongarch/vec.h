/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch vector utilitites
 *
 * Copyright (c) 2023 Loongson Technology Corporation Limited
 */

#ifndef LOONGARCH_VEC_H
#define LOONGARCH_VEC_H

#ifndef CONFIG_USER_ONLY
 #define CHECK_VEC do { \
     if ((ctx->vl == LSX_LEN) && \
         (ctx->base.tb->flags & HW_FLAGS_EUEN_SXE) == 0) { \
         generate_exception(ctx, EXCCODE_SXD); \
         return true; \
     } \
     if ((ctx->vl == LASX_LEN) && \
         (ctx->base.tb->flags & HW_FLAGS_EUEN_ASXE) == 0) { \
         generate_exception(ctx, EXCCODE_ASXD); \
         return true; \
     } \
 } while (0)
#else
 #define CHECK_VEC
#endif /*!CONFIG_USER_ONLY */

#if HOST_BIG_ENDIAN
#define B(x)  B[(x) ^ 15]
#define H(x)  H[(x) ^ 7]
#define W(x)  W[(x) ^ 3]
#define D(x)  D[(x) ^ 1]
#define UB(x) UB[(x) ^ 15]
#define UH(x) UH[(x) ^ 7]
#define UW(x) UW[(x) ^ 3]
#define UD(x) UD[(x) ^ 1]
#define Q(x)  Q[x]
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
#endif /* HOST_BIG_ENDIAN */

#define DO_ADD(a, b)  (a + b)
#define DO_SUB(a, b)  (a - b)

#define DO_VAVG(a, b)  ((a >> 1) + (b >> 1) + (a & b & 1))
#define DO_VAVGR(a, b) ((a >> 1) + (b >> 1) + ((a | b) & 1))

#endif /* LOONGARCH_VEC_H */
