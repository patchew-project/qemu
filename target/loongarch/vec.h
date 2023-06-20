/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch vector utilitites
 *
 * Copyright (c) 2023 Loongson Technology Corporation Limited
 */

#ifndef LOONGARCH_VEC_H
#define LOONGARCH_VEC_H

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

#endif /* LOONGARCH_VEC_H */
