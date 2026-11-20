/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2025 Loongson Technology Corporation Limited
 */
#include "qemu/osdep.h"
#include "cpu.h"
#include "csr.h"

#define CSR_OFF_FLAGS(NAME, FL)                            \
    [LOONGARCH_CSR_##NAME] = {                             \
        .name   = (stringify(NAME)),                       \
        .offset = CSR_OFFSET(CSR_##NAME),                  \
        .flags  = FL,                                      \
        .wrmask = LOONGARCH_CSR_##NAME## _ ## MASK         \
    }

#define CSR_OFF_ARRAY_FLAGS(NAME, N, FL)                   \
    [LOONGARCH_CSR_##NAME(N)] = {                          \
        .name   = (stringify(NAME##N)),                    \
        .offset = CSR_OFFSET(CSR_##NAME[N]),               \
        .flags  = FL,                                      \
        .wrmask = LOONGARCH_CSR_##NAME## _ ## MASK         \
    }

#define CSR_OFF(NAME)             CSR_OFF_FLAGS(NAME, CSRFL_BASIC)
#define CSR_OFF64(NAME)           CSR_OFF_FLAGS(NAME, CSRFL_WIDTH64)
#define CSR_OFF_ARRAY(NAME, N)    CSR_OFF_ARRAY_FLAGS(NAME, N, CSRFL_BASIC)
#define CSR_OFF_ARRAY64(NAME, N)  CSR_OFF_ARRAY_FLAGS(NAME, N, CSRFL_WIDTH64)

static CSRInfo csr_info[] = {
    CSR_OFF_FLAGS(CRMD, CSRFL_EXITTB),
    CSR_OFF(PRMD),
    CSR_OFF_FLAGS(EUEN, CSRFL_EXITTB),
    CSR_OFF(MISC),
    CSR_OFF(ECFG),
    CSR_OFF_FLAGS(ESTAT, CSRFL_EXITTB),
    CSR_OFF64(ERA),
    CSR_OFF64(BADV),
    CSR_OFF_FLAGS(BADI, CSRFL_READONLY),
    CSR_OFF64(EENTRY),
    CSR_OFF(TLBIDX),
    CSR_OFF64(TLBEHI),
    CSR_OFF64(TLBELO0),
    CSR_OFF64(TLBELO1),
    CSR_OFF_FLAGS(ASID, CSRFL_EXITTB),
    CSR_OFF64(PGDL),
    CSR_OFF64(PGDH),
    CSR_OFF_FLAGS(PGD, CSRFL_READONLY | CSRFL_WIDTH64),
    CSR_OFF(PWCL),
    CSR_OFF(PWCH),
    CSR_OFF(STLBPS),
    CSR_OFF(RVACFG),
    CSR_OFF_FLAGS(CPUID, CSRFL_READONLY),
    CSR_OFF_FLAGS(PRCFG1, CSRFL_READONLY),
    CSR_OFF_FLAGS(PRCFG2, CSRFL_READONLY | CSRFL_WIDTH64),
    CSR_OFF_FLAGS(PRCFG3, CSRFL_READONLY),
    CSR_OFF_ARRAY64(SAVE, 0),
    CSR_OFF_ARRAY64(SAVE, 1),
    CSR_OFF_ARRAY64(SAVE, 2),
    CSR_OFF_ARRAY64(SAVE, 3),
    CSR_OFF_ARRAY64(SAVE, 4),
    CSR_OFF_ARRAY64(SAVE, 5),
    CSR_OFF_ARRAY64(SAVE, 6),
    CSR_OFF_ARRAY64(SAVE, 7),
    CSR_OFF_ARRAY64(SAVE, 8),
    CSR_OFF_ARRAY64(SAVE, 9),
    CSR_OFF_ARRAY64(SAVE, 10),
    CSR_OFF_ARRAY64(SAVE, 11),
    CSR_OFF_ARRAY64(SAVE, 12),
    CSR_OFF_ARRAY64(SAVE, 13),
    CSR_OFF_ARRAY64(SAVE, 14),
    CSR_OFF_ARRAY64(SAVE, 15),
    CSR_OFF(TID),
    CSR_OFF_FLAGS(TCFG, CSRFL_IO | CSRFL_WIDTH64),
    CSR_OFF_FLAGS(TVAL, CSRFL_READONLY | CSRFL_IO | CSRFL_WIDTH64),
    CSR_OFF64(CNTC),
    CSR_OFF_FLAGS(TICLR, CSRFL_IO),
    CSR_OFF(LLBCTL),
    CSR_OFF(IMPCTL1),
    CSR_OFF(IMPCTL2),
    CSR_OFF64(TLBRENTRY),
    CSR_OFF64(TLBRBADV),
    CSR_OFF64(TLBRERA),
    CSR_OFF64(TLBRSAVE),
    CSR_OFF64(TLBRELO0),
    CSR_OFF64(TLBRELO1),
    CSR_OFF64(TLBREHI),
    CSR_OFF(TLBRPRMD),
    CSR_OFF_ARRAY64(DMW, 0),
    CSR_OFF_ARRAY64(DMW, 1),
    CSR_OFF_ARRAY64(DMW, 2),
    CSR_OFF_ARRAY64(DMW, 3),
    CSR_OFF_ARRAY(PERFCTRL, 0),
    CSR_OFF_ARRAY64(PERFCNTR, 0),
    CSR_OFF_ARRAY(PERFCTRL, 1),
    CSR_OFF_ARRAY64(PERFCNTR, 1),
    CSR_OFF_ARRAY(PERFCTRL, 2),
    CSR_OFF_ARRAY64(PERFCNTR, 2),
    CSR_OFF_ARRAY(PERFCTRL, 3),
    CSR_OFF_ARRAY64(PERFCNTR, 3),
    CSR_OFF_ARRAY(PERFCTRL, 4),
    CSR_OFF_ARRAY64(PERFCNTR, 4),
    CSR_OFF_ARRAY(PERFCTRL, 5),
    CSR_OFF_ARRAY64(PERFCNTR, 5),
    CSR_OFF_ARRAY(PERFCTRL, 6),
    CSR_OFF_ARRAY64(PERFCNTR, 6),
    CSR_OFF_ARRAY(PERFCTRL, 7),
    CSR_OFF_ARRAY64(PERFCNTR, 7),
    CSR_OFF_ARRAY(PERFCTRL, 8),
    CSR_OFF_ARRAY64(PERFCNTR, 8),
    CSR_OFF_ARRAY(PERFCTRL, 9),
    CSR_OFF_ARRAY64(PERFCNTR, 9),
    CSR_OFF_ARRAY(PERFCTRL, 10),
    CSR_OFF_ARRAY64(PERFCNTR, 10),
    CSR_OFF_ARRAY(PERFCTRL, 11),
    CSR_OFF_ARRAY64(PERFCNTR, 11),
    CSR_OFF_ARRAY(PERFCTRL, 12),
    CSR_OFF_ARRAY64(PERFCNTR, 12),
    CSR_OFF_ARRAY(PERFCTRL, 13),
    CSR_OFF_ARRAY64(PERFCNTR, 13),
    CSR_OFF_ARRAY(PERFCTRL, 14),
    CSR_OFF_ARRAY64(PERFCNTR, 14),
    CSR_OFF_ARRAY(PERFCTRL, 15),
    CSR_OFF_ARRAY64(PERFCNTR, 15),
    CSR_OFF_ARRAY_FLAGS(MSGIS, 0, CSRFL_READONLY | CSRFL_WIDTH64),
    CSR_OFF_ARRAY_FLAGS(MSGIS, 1, CSRFL_READONLY | CSRFL_WIDTH64),
    CSR_OFF_ARRAY_FLAGS(MSGIS, 2, CSRFL_READONLY | CSRFL_WIDTH64),
    CSR_OFF_ARRAY_FLAGS(MSGIS, 3, CSRFL_READONLY | CSRFL_WIDTH64),
    CSR_OFF_FLAGS(MSGIR, CSRFL_READONLY),
};

CSRInfo *get_csr(unsigned int csr_num)
{
    CSRInfo *csr;

    if (csr_num >= ARRAY_SIZE(csr_info)) {
        return NULL;
    }

    csr = &csr_info[csr_num];
    if (csr->flags == 0) {
        return NULL;
    }

    return csr;
}

bool set_csr_flag(unsigned int csr_num, int flag)
{
    CSRInfo *csr;

    csr = get_csr(csr_num);
    if (!csr) {
        return false;
    }

    csr->flags |= flag;
    return true;
}

bool clear_csr_wrmask(unsigned int csr_num, uint64_t wrmask)
{
    CSRInfo *csr;

    csr = get_csr(csr_num);
    if (!csr) {
        return false;
    }

    csr->wrmask &= ~wrmask;
    return true;
}

bool set_csr_wrmask(unsigned int csr_num, uint64_t wrmask)
{
    CSRInfo *csr;

    csr = get_csr(csr_num);
    if (!csr) {
        return false;
    }

    csr->wrmask |= wrmask;
    return true;
}
