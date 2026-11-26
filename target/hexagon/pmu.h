/*
 * Hexagon PMU event definitions
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HEXAGON_TARGET_PMU_H
#define HEXAGON_TARGET_PMU_H

#include "hex_regs.h"

enum {
    PMU_NO_EVENT = 0x000,
    COMMITTED_PKT_ANY = 0x003,
    COMMITTED_PKT_T0 = 0x00c,
    COMMITTED_PKT_T1 = 0x00d,
    COMMITTED_PKT_T2 = 0x00e,
    COMMITTED_PKT_T3 = 0x00f,
    COMMITTED_PKT_T4 = 0x010,
    COMMITTED_PKT_T5 = 0x011,
    COMMITTED_PKT_T6 = 0x015,
    COMMITTED_PKT_T7 = 0x016,
    COMMITTED_PKT_T8 = 0x1c5,
    COMMITTED_PKT_T9 = 0x1c6,
    COMMITTED_PKT_T10 = 0x1c7,
    COMMITTED_PKT_T11 = 0x1c8,
    COMMITTED_PKT_T12 = 0x1c9,
    COMMITTED_PKT_T13 = 0x1ca,
    COMMITTED_PKT_T14 = 0x1cb,
    COMMITTED_PKT_T15 = 0x1cc,
    COMMITTED_LD = 0x030,
    COMMITTED_ST = 0x031,
    COMMITTED_MEMOP = 0x032,
    HVX_PKT = 0x111,
    HVX_PKT_THREAD = 0x112,
    HVXPIPE_ALU = 0x128,
    HVXPIPE_MPY = 0x129,
    HVXPIPE_SHIFT = 0x12a,
    HVXPIPE_PERM = 0x12b,
    HLXFIFO_PKT = 0x1a2,
    HLXLD = 0x1a6,
    HLXST = 0x1aa,
    HLXSLOT_ALU = 0x1af,
    HLXSLOT_SHIFT = 0x1b0,
    HLXSLOT_MPY = 0x1b1,
    HLXSLOT_LD = 0x1b2,
    HLXSLOT_ST = 0x1b3,
    CYCLES_1_HVX_CONTEXTS_RUNNING = 0x115,
    CYCLES_2_HVX_CONTEXTS_RUNNING = 0x116,
    CYCLES_3_HVX_CONTEXTS_RUNNING = 0x117,
    CYCLES_4_HVX_CONTEXTS_RUNNING = 0x12c,
    CYCLES_5_HVX_CONTEXTS_RUNNING = 0x191,
    CYCLES_6_HVX_CONTEXTS_RUNNING = 0x192,
};

#define HEX_PMU_IMPLEMENTED_EVENTS \
    DECL_PMU_EVENT(PMU_NO_EVENT,       0x000) \
    DECL_PMU_EVENT(COMMITTED_PKT_ANY,  0x003) \
    DECL_PMU_EVENT(COMMITTED_PKT_T0,   0x00c) \
    DECL_PMU_EVENT(COMMITTED_PKT_T1,   0x00d) \
    DECL_PMU_EVENT(COMMITTED_PKT_T2,   0x00e) \
    DECL_PMU_EVENT(COMMITTED_PKT_T3,   0x00f) \
    DECL_PMU_EVENT(COMMITTED_PKT_T4,   0x010) \
    DECL_PMU_EVENT(COMMITTED_PKT_T5,   0x011) \
    DECL_PMU_EVENT(COMMITTED_PKT_T6,   0x015) \
    DECL_PMU_EVENT(COMMITTED_PKT_T7,   0x016) \
    DECL_PMU_EVENT(COMMITTED_PKT_T8,   0x1c5) \
    DECL_PMU_EVENT(COMMITTED_PKT_T9,   0x1c6) \
    DECL_PMU_EVENT(COMMITTED_PKT_T10,  0x1c7) \
    DECL_PMU_EVENT(COMMITTED_PKT_T11,  0x1c8) \
    DECL_PMU_EVENT(COMMITTED_PKT_T12,  0x1c9) \
    DECL_PMU_EVENT(COMMITTED_PKT_T13,  0x1ca) \
    DECL_PMU_EVENT(COMMITTED_PKT_T14,  0x1cb) \
    DECL_PMU_EVENT(COMMITTED_PKT_T15,  0x1cc) \
    DECL_PMU_EVENT(COMMITTED_LD,       0x030) \
    DECL_PMU_EVENT(COMMITTED_ST,       0x031) \
    DECL_PMU_EVENT(COMMITTED_MEMOP,    0x032) \
    DECL_PMU_EVENT(HVX_PKT,            0x111) \
    DECL_PMU_EVENT(HVX_PKT_THREAD,     0x112) \
    DECL_PMU_EVENT(HVXPIPE_ALU,        0x128) \
    DECL_PMU_EVENT(HVXPIPE_MPY,        0x129) \
    DECL_PMU_EVENT(HVXPIPE_SHIFT,      0x12a) \
    DECL_PMU_EVENT(HVXPIPE_PERM,       0x12b) \
    DECL_PMU_EVENT(HLXFIFO_PKT,        0x1a2) \
    DECL_PMU_EVENT(HLXLD,              0x1a6) \
    DECL_PMU_EVENT(HLXST,              0x1aa) \
    DECL_PMU_EVENT(HLXSLOT_ALU,        0x1af) \
    DECL_PMU_EVENT(HLXSLOT_SHIFT,      0x1b0) \
    DECL_PMU_EVENT(HLXSLOT_MPY,        0x1b1) \
    DECL_PMU_EVENT(HLXSLOT_LD,         0x1b2) \
    DECL_PMU_EVENT(HLXSLOT_ST,         0x1b3)

/*
 * HLXLD/HLXST count in units of one 128-byte HVX-sized beat, so a full
 * 2KB HLX vector access adds HLX_PMU_BEATS_PER_ACCESS to the event. This is
 * the unit the reference simulator's derived statistics assume
 * (VTCM_RD_BW_HLX = HLXLD * 128 bytes per cycle).
 */
#define HLX_PMU_BEAT_BYTES 128

#define NUM_PMU_CTRS 12

/* An event id is 10 bits: PMUEVTCFG[7:0] and PMUCFG.MSBCNT[1:0]. */
#define PMU_NUM_EVENT_IDS 1024

/*
 * PMU sregs are defined in this order: 4, 5, 6, 7, 0, 1, 2, 3.
 * The gregs are ordered the same way.
 */
#define IS_PMU_CNT_SREG(REG) \
    (((REG) >= HEX_SREG_PMUCNT4 && (REG) <= HEX_SREG_PMUCNT3) || \
     ((REG) >= HEX_SREG_PMUCNT8 && (REG) <= HEX_SREG_PMUCNT11))
#define IS_PMU_CFG_SREG(REG) \
    ((REG) == HEX_SREG_PMUEVTCFG || (REG) == HEX_SREG_PMUEVTCFG1 || \
     (REG) == HEX_SREG_PMUEVTCFG2 || (REG) == HEX_SREG_PMUCFG || \
     (REG) == HEX_SREG_PMUSTID0 || (REG) == HEX_SREG_PMUSTID1 || \
     (REG) == HEX_SREG_PMUSTID2)
#define IS_PMU_SREG(REG) (IS_PMU_CNT_SREG(REG) || IS_PMU_CFG_SREG(REG))

#ifndef CONFIG_USER_ONLY
#define IS_PMU_GREG(REG) \
    (((REG) >= HEX_GREG_GPMUCNT4 && (REG) <= HEX_GREG_GPMUCNT7) || \
     ((REG) >= HEX_GREG_GPMUCNT0 && (REG) <= HEX_GREG_GPMUCNT3) || \
     ((REG) >= HEX_GREG_GPMUCNT8 && (REG) <= HEX_GREG_GPMUCNT9))

static inline unsigned int pmu_index_from_sreg(int reg)
{
    g_assert(IS_PMU_CNT_SREG(reg));
    if (reg >= HEX_SREG_PMUCNT8) {
        return 8 + reg - HEX_SREG_PMUCNT8;
    }
    if (reg >= HEX_SREG_PMUCNT0) {
        return reg - HEX_SREG_PMUCNT0;
    }
    return 4 + reg - HEX_SREG_PMUCNT4;
}

/* The first counter whose event is selected by this PMUEVTCFG register */
static inline unsigned int pmu_evtcfg_base(int reg)
{
    switch (reg) {
    case HEX_SREG_PMUEVTCFG:
        return 0;
    case HEX_SREG_PMUEVTCFG1:
        return 4;
    default:
        g_assert(reg == HEX_SREG_PMUEVTCFG2);
        return 8;
    }
}

/* Which PMUSTID register holds the STID match of this counter */
static inline unsigned int pmu_stid_index(int reg)
{
    switch (reg) {
    case HEX_SREG_PMUSTID0:
        return 0;
    case HEX_SREG_PMUSTID1:
        return 1;
    default:
        g_assert(reg == HEX_SREG_PMUSTID2);
        return 2;
    }
}

/*
 * Where PMUCFG holds bits 9:8 of counter @index's event: bits 15:0 for
 * the first 8 counters, and bits 28:21, past the thread mode, for the rest.
 */
static inline unsigned int pmu_cfg_msb_shift(unsigned int index)
{
    return index < 8 ? index * 2 : 21 + (index - 8) * 2;
}

static inline unsigned int pmu_index_from_greg(int reg)
{
    g_assert(IS_PMU_GREG(reg));
    if (reg >= HEX_GREG_GPMUCNT8) {
        return 8 + reg - HEX_GREG_GPMUCNT8;
    }
    if (reg >= HEX_GREG_GPMUCNT0) {
        return reg - HEX_GREG_GPMUCNT0;
    }
    return 4 + reg - HEX_GREG_GPMUCNT4;
}

static inline int pmu_committed_pkt_thread(int event)
{
    /* Note that the COMMITTED_PKT_T* event numbers are not contiguous. */
    if (event >= COMMITTED_PKT_T8) {
        return 8 + event - COMMITTED_PKT_T8;
    }
    return event < COMMITTED_PKT_T6 ?
           event - COMMITTED_PKT_T0 :
           6 + event - COMMITTED_PKT_T6;
}
#endif /* !CONFIG_USER_ONLY */

#endif /* HEXAGON_TARGET_PMU_H */
