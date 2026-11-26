/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch CSRs
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#ifndef LOONGARCH_CPU_CSR_H
#define LOONGARCH_CPU_CSR_H

#include "hw/core/registerfields.h"

/* Based on kernel definitions: arch/loongarch/include/asm/loongarch.h */

#define CSR_WRMASK(name, shift, length)                 \
     enum { LOONGARCH_CSR_##name ##_MASK = MAKE_64BIT_MASK(shift, length)};

/* Basic CSRs */
#define LOONGARCH_CSR_CRMD           0x0 /* Current mode info */
FIELD(CSR_CRMD, PLV, 0, 2)
FIELD(CSR_CRMD, IE, 2, 1)
FIELD(CSR_CRMD, DA, 3, 1)
FIELD(CSR_CRMD, PG, 4, 1)
FIELD(CSR_CRMD, DATF, 5, 2)
FIELD(CSR_CRMD, DATM, 7, 2)
FIELD(CSR_CRMD, WE, 9, 1)
CSR_WRMASK(CRMD, 0, 10)

#define LOONGARCH_CSR_PRMD           0x1 /* Prev-exception mode info */
FIELD(CSR_PRMD, PPLV, 0, 2)
FIELD(CSR_PRMD, PIE, 2, 1)
FIELD(CSR_PRMD, PWE, 3, 1)
CSR_WRMASK(PRMD, 0, 4)

#define LOONGARCH_CSR_EUEN           0x2 /* Extended unit enable */
FIELD(CSR_EUEN, FPE, 0, 1)
FIELD(CSR_EUEN, SXE, 1, 1)
FIELD(CSR_EUEN, ASXE, 2, 1)
FIELD(CSR_EUEN, BTE, 3, 1)
CSR_WRMASK(EUEN, 0, 4)

#define LOONGARCH_CSR_MISC           0x3 /* Misc config */
FIELD(CSR_MISC, VA32, 1, 3)
FIELD(CSR_MISC, RSV, 4, 1)
FIELD(CSR_MISC, DRDTL, 5, 3)
FIELD(CSR_MISC, RSV1, 8, 1)
FIELD(CSR_MISC, RPCNTL, 9, 3)
FIELD(CSR_MISC, ALCL, 12, 4)
FIELD(CSR_MISC, DWPL, 16, 3)
CSR_WRMASK(MISC, 1, 18)

#define LOONGARCH_CSR_ECFG           0x4 /* Exception config */
FIELD(CSR_ECFG, LIE, 0, 13)
FIELD(CSR_ECFG, RSV, 13, 3)
FIELD(CSR_ECFG, VS, 16, 3)
CSR_WRMASK(ECFG, 0, 19)

#define LOONGARCH_CSR_ESTAT          0x5 /* Exception status */
FIELD(CSR_ESTAT, IS, 0, 15)        /* bit 15 is msg interrupt enabled */
FIELD(CSR_ESTAT, MSGINT, 14, 1)
FIELD(CSR_ESTAT, ECODE, 16, 6)
FIELD(CSR_ESTAT, ESUBCODE, 22, 9)
CSR_WRMASK(ESTAT, 0, 2)

#define LOONGARCH_CSR_ERA            0x6 /* Exception return address */
CSR_WRMASK(ERA, 0, 64)

#define LOONGARCH_CSR_BADV           0x7 /* Bad virtual address */
CSR_WRMASK(BADV, 0, 64)

#define LOONGARCH_CSR_BADI           0x8 /* Bad instruction */
CSR_WRMASK(BADI, 0, 1)         /* fail to compile with (BADI, 0, 0) */

#define LOONGARCH_CSR_EENTRY         0xc /* Exception entry address */
CSR_WRMASK(EENTRY, 12, 52)

/* TLB related CSRs */
#define LOONGARCH_CSR_TLBIDX         0x10 /* TLB Index, EHINV, PageSize, NP */
FIELD(CSR_TLBIDX, INDEX, 0, 12)
FIELD(CSR_TLBIDX, PS, 24, 6)
FIELD(CSR_TLBIDX, NE, 31, 1)
CSR_WRMASK(TLBIDX, 0, 32)

#define LOONGARCH_CSR_TLBEHI         0x11 /* TLB EntryHi */
FIELD(CSR_TLBEHI_32, VPPN, 13, 19)
FIELD(CSR_TLBEHI_64, VPPN, 13, 35)
CSR_WRMASK(TLBEHI, 13, 35)

#define LOONGARCH_CSR_TLBELO0        0x12 /* TLB EntryLo0 */
#define LOONGARCH_CSR_TLBELO1        0x13 /* TLB EntryLo1 */
FIELD(TLBENTRY, V, 0, 1)
FIELD(TLBENTRY, D, 1, 1)
FIELD(TLBENTRY, PLV, 2, 2)
FIELD(TLBENTRY, MAT, 4, 2)
FIELD(TLBENTRY, G, 6, 1)
FIELD(TLBENTRY, HUGE, 6, 1)
FIELD(TLBENTRY, P, 7, 1)
FIELD(TLBENTRY, W, 8, 1)
FIELD(TLBENTRY, HGLOBAL, 12, 1)
FIELD(TLBENTRY, LEVEL, 13, 2)
FIELD(TLBENTRY_32, PPN, 8, 24)
FIELD(TLBENTRY_64, PPN, 12, 36)
FIELD(TLBENTRY_64, NR, 61, 1)
FIELD(TLBENTRY_64, NX, 62, 1)
FIELD(TLBENTRY_64, RPLV, 63, 1)
CSR_WRMASK(TLBELO0, 0, 64)
CSR_WRMASK(TLBELO1, 0, 64)

#define LOONGARCH_CSR_ASID           0x18 /* Address space identifier */
FIELD(CSR_ASID, ASID, 0, 10)
FIELD(CSR_ASID, ASIDBITS, 16, 8)
CSR_WRMASK(ASID, 0, 10)

/* Page table base address when badv[47] = 0 */
#define LOONGARCH_CSR_PGDL           0x19
CSR_WRMASK(PGDL, 12, 52)
/* Page table base address when badv[47] = 1 */
#define LOONGARCH_CSR_PGDH           0x1a
CSR_WRMASK(PGDH, 12, 52)

#define LOONGARCH_CSR_PGD            0x1b /* Page table base address */
CSR_WRMASK(PGD, 0, 1)

/* Page walk controller's low addr */
#define LOONGARCH_CSR_PWCL           0x1c
FIELD(CSR_PWCL, PTBASE, 0, 5)
FIELD(CSR_PWCL, PTWIDTH, 5, 5)
FIELD(CSR_PWCL, DIR1_BASE, 10, 5)
FIELD(CSR_PWCL, DIR1_WIDTH, 15, 5)
FIELD(CSR_PWCL, DIR2_BASE, 20, 5)
FIELD(CSR_PWCL, DIR2_WIDTH, 25, 5)
FIELD(CSR_PWCL, PTEWIDTH, 30, 2)
CSR_WRMASK(PWCL, 0, 32)

/* Page walk controller's high addr */
#define LOONGARCH_CSR_PWCH           0x1d
FIELD(CSR_PWCH, DIR3_BASE, 0, 6)
FIELD(CSR_PWCH, DIR3_WIDTH, 6, 6)
FIELD(CSR_PWCH, DIR4_BASE, 12, 6)
FIELD(CSR_PWCH, DIR4_WIDTH, 18, 6)
FIELD(CSR_PWCH, HPTW_EN, 24, 1)
FIELD(CSR_PWCH, RESERVE, 25, 7)
CSR_WRMASK(PWCH, 0, 24)

#define LOONGARCH_CSR_STLBPS         0x1e /* Stlb page size */
FIELD(CSR_STLBPS, PS, 0, 5)
FIELD(CSR_STLBPS, RESERVE, 5, 27)
CSR_WRMASK(STLBPS, 0, 6)

#define LOONGARCH_CSR_RVACFG         0x1f /* Reduced virtual address config */
FIELD(CSR_RVACFG, RBITS, 0, 4)
CSR_WRMASK(RVACFG, 0, 4)

/* Config CSRs */
#define LOONGARCH_CSR_CPUID          0x20 /* CPU core id */
CSR_WRMASK(CPUID, 0, 1)

#define LOONGARCH_CSR_PRCFG1         0x21 /* Config1 */
FIELD(CSR_PRCFG1, SAVE_NUM, 0, 4)
FIELD(CSR_PRCFG1, TIMER_BITS, 4, 8)
FIELD(CSR_PRCFG1, VSMAX, 12, 3)
CSR_WRMASK(PRCFG1, 0, 1)

#define LOONGARCH_CSR_PRCFG2         0x22 /* Config2 */
CSR_WRMASK(PRCFG2, 0, 1)

#define LOONGARCH_CSR_PRCFG3         0x23 /* Config3 */
FIELD(CSR_PRCFG3, TLB_TYPE, 0, 4)
FIELD(CSR_PRCFG3, MTLB_ENTRY, 4, 8)
FIELD(CSR_PRCFG3, STLB_WAYS, 12, 8)
FIELD(CSR_PRCFG3, STLB_SETS, 20, 8)
CSR_WRMASK(PRCFG3, 0, 1)

/*
 * Save registers count can read from PRCFG1.SAVE_NUM
 * The Min count is 1. Max count is 15.
 */
#define LOONGARCH_CSR_SAVE(N)        (0x30 + N)
CSR_WRMASK(SAVE, 0, 64)

/* Timer CSRs */
#define LOONGARCH_CSR_TID            0x40 /* Timer ID */
CSR_WRMASK(TID, 0, 32)

#define LOONGARCH_CSR_TCFG           0x41 /* Timer config */
FIELD(CSR_TCFG, EN, 0, 1)
FIELD(CSR_TCFG, PERIODIC, 1, 1)
FIELD(CSR_TCFG, INIT_VAL, 2, 46)
CSR_WRMASK(TCFG, 0, 48)

#define LOONGARCH_CSR_TVAL           0x42 /* Timer ticks remain */
CSR_WRMASK(TVAL, 0, 1)

#define LOONGARCH_CSR_CNTC           0x43 /* Timer offset */
CSR_WRMASK(CNTC, 0, 64)

#define LOONGARCH_CSR_TICLR          0x44 /* Timer interrupt clear */
CSR_WRMASK(TICLR, 0, 1)

/* LLBCTL CSRs */
#define LOONGARCH_CSR_LLBCTL         0x60 /* LLBit control */
FIELD(CSR_LLBCTL, ROLLB, 0, 1)
FIELD(CSR_LLBCTL, WCLLB, 1, 1)
FIELD(CSR_LLBCTL, KLO, 2, 1)
CSR_WRMASK(LLBCTL, 1, 2)

/* Implement dependent */
#define LOONGARCH_CSR_IMPCTL1        0x80 /* LoongArch config1 */
CSR_WRMASK(IMPCTL1, 0, 64)

#define LOONGARCH_CSR_IMPCTL2        0x81 /* LoongArch config2*/
CSR_WRMASK(IMPCTL2, 0, 64)

/* TLB Refill CSRs */
#define LOONGARCH_CSR_TLBRENTRY      0x88 /* TLB refill exception address */
CSR_WRMASK(TLBRENTRY, 12, 36)
#define LOONGARCH_CSR_TLBRBADV       0x89 /* TLB refill badvaddr */
CSR_WRMASK(TLBRBADV, 0, 64)
#define LOONGARCH_CSR_TLBRERA        0x8a /* TLB refill ERA */
FIELD(CSR_TLBRERA, ISTLBR, 0, 1)
FIELD(CSR_TLBRERA, PC, 2, 62)
CSR_WRMASK(TLBRERA, 0, 64)
#define LOONGARCH_CSR_TLBRSAVE       0x8b /* KScratch for TLB refill */
CSR_WRMASK(TLBRSAVE, 0, 64)
#define LOONGARCH_CSR_TLBRELO0       0x8c /* TLB refill entrylo0 */
CSR_WRMASK(TLBRELO0, 0, 64)
#define LOONGARCH_CSR_TLBRELO1       0x8d /* TLB refill entrylo1 */
CSR_WRMASK(TLBRELO1, 0, 64)
#define LOONGARCH_CSR_TLBREHI        0x8e /* TLB refill entryhi */
FIELD(CSR_TLBREHI, PS, 0, 6)
FIELD(CSR_TLBREHI_32, VPPN, 13, 19)
FIELD(CSR_TLBREHI_64, VPPN, 13, 35)
CSR_WRMASK(TLBREHI, 0, 48)
#define LOONGARCH_CSR_TLBRPRMD       0x8f /* TLB refill mode info */
FIELD(CSR_TLBRPRMD, PPLV, 0, 2)
FIELD(CSR_TLBRPRMD, PIE, 2, 1)
FIELD(CSR_TLBRPRMD, PWE, 4, 1)
CSR_WRMASK(TLBRPRMD, 0, 5)

/*Msg interrupt registers */
#define N_MSGIS                      4
#define LOONGARCH_CSR_MSGIS(N)       (0xa0 + N)
FIELD(CSR_MSGIS, IS, 0, 63)
CSR_WRMASK(MSGIS, 0, 1)

#define LOONGARCH_CSR_MSGIR               0xa4
FIELD(CSR_MSGIR, INTNUM, 0, 8)
FIELD(CSR_MSGIR, ACTIVE, 31, 1)
CSR_WRMASK(MSGIR, 0, 1)

FIELD(CSR_MSGIE, PT, 0, 8)

/* Direct map windows CSRs*/
#define LOONGARCH_CSR_DMW(N)         (0x180 + N)
FIELD(CSR_DMW, PLV0, 0, 1)
FIELD(CSR_DMW, PLV1, 1, 1)
FIELD(CSR_DMW, PLV2, 2, 1)
FIELD(CSR_DMW, PLV3, 3, 1)
FIELD(CSR_DMW, MAT, 4, 2)
FIELD(CSR_DMW_32, PSEG, 25, 3)
FIELD(CSR_DMW_32, VSEG, 29, 3)
FIELD(CSR_DMW_64, VSEG, 60, 4)
CSR_WRMASK(DMW, 0, 64)

/* Performance Counter registers */
#define MAX_PERF_EVENTS              16
#define LOONGARCH_CSR_PERFCTRL(N)    (0x200 + 2 * N)
#define LOONGARCH_CSR_PERFCNTR(N)    (0x201 + 2 * N)
CSR_WRMASK(PERFCTRL, 0, 21)
CSR_WRMASK(PERFCNTR, 0, 64)

#define LOONGARCH_CSR_MAX            (LOONGARCH_CSR_PERFCTRL(MAX_PERF_EVENTS))

#endif /* LOONGARCH_CPU_CSR_H */
