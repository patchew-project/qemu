/*
 * QEMU OpenTitan Alert handler device
 *
 * Copyright (c) 2023-2025 Rivos, Inc.
 *
 * Author(s):
 *  Emmanuel Blot <eblot@rivosinc.com>
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "qemu/osdep.h"
#include "qemu/guest-random.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/timer.h"
#include "qemu/typedefs.h"
#include "qapi/error.h"
#include "hw/opentitan/ot_alert.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/registerfields.h"
#include "hw/riscv/ibex_common.h"
#include "hw/riscv/ibex_irq.h"
#include "hw/core/sysbus.h"
#include "trace.h"

#define PARAM_N_ALERTS    65u
#define PARAM_N_LPG       24u
#define PARAM_N_LPG_WIDTH 5u
#define PARAM_ESC_CNT_DW  32u
#define PARAM_ACCU_CNT_DW 16u
#define PARAM_N_CLASSES   4u
#define PARAM_N_ESC_SEV   4u
#define PARAM_N_PHASES    4u
#define PARAM_N_LOC_ALERT 7u
#define PARAM_PING_CNT_DW 16u
#define PARAM_PHASE_DW    2u
#define PARAM_CLASS_DW    2u

/* clang-format off */
REG32(INTR_STATE, 0x0u)
    SHARED_FIELD(INTR_STATE_CLASSA, 0u, 1u)
    SHARED_FIELD(INTR_STATE_CLASSB, 1u, 1u)
    SHARED_FIELD(INTR_STATE_CLASSC, 2u, 1u)
    SHARED_FIELD(INTR_STATE_CLASSD, 3u, 1u)
REG32(INTR_ENABLE, 0x4u)
REG32(INTR_TEST, 0x8u)
REG32(PING_TIMER_REGWEN, 0xcu)
    FIELD(PING_TIMER_REGWEN, EN, 0u, 1u)
REG32(PING_TIMEOUT_CYC_SHADOWED, 0x10u)
    FIELD(PING_TIMEOUT_CYC_SHADOWED, VAL, 0u, 16u)
REG32(PING_TIMER_EN_SHADOWED, 0x14u)
    FIELD(PING_TIMER_EN_SHADOWED, EN, 0u, 1u)
REG32(ALERT_REGWEN, 0x18u)
    SHARED_FIELD(ALERT_REGWEN_EN, 0u, 1u)
REG32(ALERT_EN_SHADOWED, 0x11cu)
    SHARED_FIELD(ALERT_EN_SHADOWED_EN, 0u, 1u)
REG32(ALERT_CLASS_SHADOWED, 0x220u)
    SHARED_FIELD(ALERT_CLASS_SHADOWED_EN, 0u, 2u)
REG32(ALERT_CAUSE, 0x324u)
    SHARED_FIELD(ALERT_CAUSE_EN, 0u, 1u)
REG32(LOC_ALERT_REGWEN, 0x428u)
    SHARED_FIELD(LOC_ALERT_REGWEN_EN, 0u, 1u)
REG32(LOC_ALERT_EN_SHADOWED, 0x444u)
    SHARED_FIELD(LOC_ALERT_EN_SHADOWED_EN, 0u, 1u)
REG32(LOC_ALERT_CLASS_SHADOWED, 0x460u)
    SHARED_FIELD(LOC_ALERT_CLASS_SHADOWED_EN, 0u, 2u)
REG32(LOC_ALERT_CAUSE, 0x47cu)
    SHARED_FIELD(LOC_ALERT_CAUSE_EN, 0u, 1u)
REG32(CLASS_REGWEN, 0x498u)
    FIELD(CLASS_REGWEN, EN, 0u, 1u)
REG32(CLASS_CTRL_SHADOWED, 0x49cu)
    SHARED_FIELD(CLASS_CTRL_SHADOWED_EN, 0u, 1u)
    SHARED_FIELD(CLASS_CTRL_SHADOWED_LOCK, 1u, 1u)
    SHARED_FIELD(CLASS_CTRL_SHADOWED_EN_E0, 2u, 1u)
    SHARED_FIELD(CLASS_CTRL_SHADOWED_EN_E1, 3u, 1u)
    SHARED_FIELD(CLASS_CTRL_SHADOWED_EN_E2, 4u, 1u)
    SHARED_FIELD(CLASS_CTRL_SHADOWED_EN_E3, 5u, 1u)
    SHARED_FIELD(CLASS_CTRL_SHADOWED_MAP_E0, 6u, 2u)
    SHARED_FIELD(CLASS_CTRL_SHADOWED_MAP_E1, 8u, 2u)
    SHARED_FIELD(CLASS_CTRL_SHADOWED_MAP_E2, 10u, 2u)
    SHARED_FIELD(CLASS_CTRL_SHADOWED_MAP_E3, 12u, 2u)
REG32(CLASS_CLR_REGWEN, 0x4a0u)
    SHARED_FIELD(CLASS_CLR_REGWEN_EN, 0u, 1u)
REG32(CLASS_CLR_SHADOWED, 0x4a4u)
    SHARED_FIELD(CLASS_CLR_SHADOWED_EN, 0u, 1u)
REG32(CLASS_ACCUM_CNT, 0x4a8u)
    SHARED_FIELD(CLASS_ACCUM_CNT, 0u, 16u)
REG32(CLASS_ACCUM_THRESH_SHADOWED, 0x4acu)
    SHARED_FIELD(CLASS_ACCUM_THRESH_SHADOWED, 0u, 16u)
REG32(CLASS_TIMEOUT_CYC_SHADOWED, 0x4b0u)
REG32(CLASS_CRASHDUMP_TRIGGER_SHADOWED, 0x4b4u)
    SHARED_FIELD(CLASS_CRASHDUMP_TRIGGER_SHADOWED, 0u, 2u)
REG32(CLASS_PHASE0_CYC_SHADOWED, 0x4b8u)
REG32(CLASS_PHASE1_CYC_SHADOWED, 0x4bcu)
REG32(CLASS_PHASE2_CYC_SHADOWED, 0x4c0u)
REG32(CLASS_PHASE3_CYC_SHADOWED, 0x4c4u)
REG32(CLASS_ESC_CNT, 0x4c8u)
REG32(CLASS_STATE, 0x4ccu)
    FIELD(CLASS_STATE, VAL, 0u, 3u)
/* clang-format on */

enum {
    ALERT_ID_ALERT_PINGFAIL,
    ALERT_ID_ESC_PINGFAIL,
    ALERT_ID_ALERT_INTEGFAIL,
    ALERT_ID_ESC_INTEGFAIL,
    ALERT_ID_BUS_INTEGFAIL,
    ALERT_ID_SHADOW_REG_UPDATE_ERROR,
    ALERT_ID_SHADOW_REG_STORAGE_ERROR,
};

enum {
    ALERT_CLASSA,
    ALERT_CLASSB,
    ALERT_CLASSC,
    ALERT_CLASSD,
};

enum {
    STATE_IDLE,
    STATE_TIMEOUT,
    STATE_FSMERROR,
    STATE_TERMINAL,
    STATE_PHASE0,
    STATE_PHASE1,
    STATE_PHASE2,
    STATE_PHASE3,
};

#define INTR_MASK ((1u << PARAM_N_CLASSES) - 1u)
#define CLASS_CTRL_SHADOWED_MASK \
    (CLASS_CTRL_SHADOWED_EN_MASK | CLASS_CTRL_SHADOWED_LOCK_MASK | \
     CLASS_CTRL_SHADOWED_EN_E0_MASK | CLASS_CTRL_SHADOWED_EN_E1_MASK | \
     CLASS_CTRL_SHADOWED_EN_E2_MASK | CLASS_CTRL_SHADOWED_EN_E3_MASK | \
     CLASS_CTRL_SHADOWED_MAP_E0_MASK | CLASS_CTRL_SHADOWED_MAP_E1_MASK | \
     CLASS_CTRL_SHADOWED_MAP_E2_MASK | CLASS_CTRL_SHADOWED_MAP_E3_MASK)

#define R32_OFF(_r_) ((_r_) / sizeof(uint32_t))

#define R_LAST_REG R32_OFF(0x574u)
#define REGS_COUNT (R_LAST_REG + 1u)
#define REGS_SIZE  (REGS_COUNT * sizeof(uint32_t))

#define ALERT_SLOT_SIZE           R32_OFF(sizeof(struct alerts))
#define LOC_ALERT_SLOT_SIZE       R32_OFF(sizeof(struct loc_alerts))
#define CLASS_SLOT_SIZE           R32_OFF(sizeof(struct classes))
#define CASE_RANGE(_reg_, _cnt_)  (_reg_)...((_reg_) + (_cnt_) - (1u))
#define CASE_STRIDE(_reg_, _cls_) ((_reg_) + (_cls_) * (CLASS_SLOT_SIZE))
#define SLOT_OFFSET(_reg_, _base_, _kind_) \
    (((_reg_) - (_base_)) / _kind_##_SLOT_SIZE)
#define ALERT_SLOT(_reg_)     SLOT_OFFSET(_reg_, R_ALERT_REGWEN, ALERT)
#define LOC_ALERT_SLOT(_reg_) SLOT_OFFSET(_reg_, R_LOC_ALERT_REGWEN, LOC_ALERT)
#define CLASS_SLOT(_reg_)     SLOT_OFFSET(_reg_, R_CLASS_REGWEN, CLASS)

#define CHECK_REGWEN(_reg_, _cond_) \
    ot_alert_check_regwen(__func__, (_reg_), (_cond_))

struct intr {
    uint32_t state;
    uint32_t enable;
    uint32_t test;
};

struct ping {
    uint32_t timer_regwen;
    uint32_t timeout_cyc_shadowed;
    uint32_t timer_en_shadowed;
};

struct alerts {
    uint32_t regwen;
    uint32_t en_shadowed;
    uint32_t class_shadowed;
    uint32_t cause;
};

struct loc_alerts {
    uint32_t regwen;
    uint32_t en_shadowed;
    uint32_t class_shadowed;
    uint32_t cause;
};

struct classes {
    uint32_t regwen;
    uint32_t ctrl_shadowed;
    uint32_t clr_regwen;
    uint32_t clr_shadowed;
    uint32_t accum_cnt;
    uint32_t accum_thresh_shadowed;
    uint32_t timeout_cyc_shadowed;
    uint32_t crashdump_trigger_shadowed;
    uint32_t phase0_cyc_shadowed;
    uint32_t phase1_cyc_shadowed;
    uint32_t phase2_cyc_shadowed;
    uint32_t phase3_cyc_shadowed;
    uint32_t esc_cnt;
    uint32_t state;
};

typedef struct OtAlertRegs {
    struct intr intr;
    struct ping ping;
    struct alerts alerts[PARAM_N_ALERTS];
    struct loc_alerts loc_alerts[PARAM_N_LOC_ALERT];
    struct classes classes[PARAM_N_CLASSES];
} OtAlertRegs;

struct OtAlertState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    IbexIRQ irqs[PARAM_N_CLASSES];

    OtAlertRegs *regs;
};

struct OtAlertClass {
    SysBusDeviceClass parent_class;
    ResettablePhases parent_phases;
};

static inline bool
ot_alert_check_regwen(const char *func, unsigned reg, bool cond)
{
    if (!cond) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: reg 0x%04x is write-protected\n",
                      func, (unsigned)(reg * sizeof(uint32_t)));
        return false;
    }
    return true;
}

static void ot_alert_update_irqs(OtAlertState *s)
{
    uint32_t level = s->regs->intr.state & s->regs->intr.enable;

    for (unsigned ix = 0; ix < ARRAY_SIZE(s->irqs); ix++) {
        ibex_irq_set(&s->irqs[ix], (int)((level >> ix) & 0x1));
    }
}

static uint64_t ot_alert_regs_read(void *opaque, hwaddr addr, unsigned size)
{
    OtAlertState *s = opaque;
    OtAlertRegs *regs = s->regs;
    uint32_t val32;

    hwaddr reg = R32_OFF(addr);

    switch (reg) {
    case R_INTR_STATE:
        val32 = regs->intr.state;
        break;
    case R_INTR_ENABLE:
        val32 = regs->intr.enable;
        break;
    case R_PING_TIMER_REGWEN:
        val32 = regs->ping.timer_regwen;
        break;
    case R_PING_TIMEOUT_CYC_SHADOWED:
        val32 = regs->ping.timeout_cyc_shadowed;
        break;
    case R_PING_TIMER_EN_SHADOWED:
        val32 = regs->ping.timer_en_shadowed;
        break;
    case R_INTR_TEST:
        qemu_log_mask(LOG_GUEST_ERROR, "W/O register 0x02%" HWADDR_PRIx "\n",
                      addr);
        val32 = 0;
        break;
    case CASE_RANGE(R_ALERT_REGWEN, PARAM_N_ALERTS):
        val32 = regs->alerts[ALERT_SLOT(reg)].regwen;
        break;
    case CASE_RANGE(R_ALERT_EN_SHADOWED, PARAM_N_ALERTS):
        val32 = regs->alerts[ALERT_SLOT(reg)].en_shadowed;
        break;
    case CASE_RANGE(R_ALERT_CLASS_SHADOWED, PARAM_N_ALERTS):
        val32 = regs->alerts[ALERT_SLOT(reg)].class_shadowed;
        break;
    case CASE_RANGE(R_ALERT_CAUSE, PARAM_N_ALERTS):
        val32 = regs->alerts[ALERT_SLOT(reg)].cause;
        break;
    case CASE_RANGE(R_LOC_ALERT_REGWEN, PARAM_N_LOC_ALERT):
        val32 = regs->loc_alerts[LOC_ALERT_SLOT(reg)].regwen;
        break;
    case CASE_RANGE(R_LOC_ALERT_EN_SHADOWED, PARAM_N_LOC_ALERT):
        val32 = regs->loc_alerts[LOC_ALERT_SLOT(reg)].en_shadowed;
        break;
    case CASE_RANGE(R_LOC_ALERT_CLASS_SHADOWED, PARAM_N_LOC_ALERT):
        val32 = regs->loc_alerts[LOC_ALERT_SLOT(reg)].class_shadowed;
        break;
    case CASE_RANGE(R_LOC_ALERT_CAUSE, PARAM_N_LOC_ALERT):
        val32 = regs->loc_alerts[LOC_ALERT_SLOT(reg)].cause;
        break;
    case CASE_STRIDE(R_CLASS_REGWEN, ALERT_CLASSA):
    case CASE_STRIDE(R_CLASS_REGWEN, ALERT_CLASSB):
    case CASE_STRIDE(R_CLASS_REGWEN, ALERT_CLASSC):
    case CASE_STRIDE(R_CLASS_REGWEN, ALERT_CLASSD):
        val32 = regs->classes[CLASS_SLOT(reg)].regwen;
        break;
    case CASE_STRIDE(R_CLASS_CTRL_SHADOWED, ALERT_CLASSA):
    case CASE_STRIDE(R_CLASS_CTRL_SHADOWED, ALERT_CLASSB):
    case CASE_STRIDE(R_CLASS_CTRL_SHADOWED, ALERT_CLASSC):
    case CASE_STRIDE(R_CLASS_CTRL_SHADOWED, ALERT_CLASSD):
        val32 = regs->classes[CLASS_SLOT(reg)].ctrl_shadowed;
        break;
    case CASE_STRIDE(R_CLASS_CLR_REGWEN, ALERT_CLASSA):
    case CASE_STRIDE(R_CLASS_CLR_REGWEN, ALERT_CLASSB):
    case CASE_STRIDE(R_CLASS_CLR_REGWEN, ALERT_CLASSC):
    case CASE_STRIDE(R_CLASS_CLR_REGWEN, ALERT_CLASSD):
        val32 = regs->classes[CLASS_SLOT(reg)].clr_regwen;
        break;
    case CASE_STRIDE(R_CLASS_CLR_SHADOWED, ALERT_CLASSA):
    case CASE_STRIDE(R_CLASS_CLR_SHADOWED, ALERT_CLASSB):
    case CASE_STRIDE(R_CLASS_CLR_SHADOWED, ALERT_CLASSC):
    case CASE_STRIDE(R_CLASS_CLR_SHADOWED, ALERT_CLASSD):
        val32 = regs->classes[CLASS_SLOT(reg)].clr_shadowed;
        break;
    case CASE_STRIDE(R_CLASS_ACCUM_CNT, ALERT_CLASSA):
    case CASE_STRIDE(R_CLASS_ACCUM_CNT, ALERT_CLASSB):
    case CASE_STRIDE(R_CLASS_ACCUM_CNT, ALERT_CLASSC):
    case CASE_STRIDE(R_CLASS_ACCUM_CNT, ALERT_CLASSD):
        val32 = regs->classes[CLASS_SLOT(reg)].accum_cnt;
        break;
    case CASE_STRIDE(R_CLASS_ACCUM_THRESH_SHADOWED, ALERT_CLASSA):
    case CASE_STRIDE(R_CLASS_ACCUM_THRESH_SHADOWED, ALERT_CLASSB):
    case CASE_STRIDE(R_CLASS_ACCUM_THRESH_SHADOWED, ALERT_CLASSC):
    case CASE_STRIDE(R_CLASS_ACCUM_THRESH_SHADOWED, ALERT_CLASSD):
        val32 = regs->classes[CLASS_SLOT(reg)].accum_thresh_shadowed;
        break;
    case CASE_STRIDE(R_CLASS_TIMEOUT_CYC_SHADOWED, ALERT_CLASSA):
    case CASE_STRIDE(R_CLASS_TIMEOUT_CYC_SHADOWED, ALERT_CLASSB):
    case CASE_STRIDE(R_CLASS_TIMEOUT_CYC_SHADOWED, ALERT_CLASSC):
    case CASE_STRIDE(R_CLASS_TIMEOUT_CYC_SHADOWED, ALERT_CLASSD):
        val32 = regs->classes[CLASS_SLOT(reg)].timeout_cyc_shadowed;
        break;
    case CASE_STRIDE(R_CLASS_CRASHDUMP_TRIGGER_SHADOWED, ALERT_CLASSA):
    case CASE_STRIDE(R_CLASS_CRASHDUMP_TRIGGER_SHADOWED, ALERT_CLASSB):
    case CASE_STRIDE(R_CLASS_CRASHDUMP_TRIGGER_SHADOWED, ALERT_CLASSC):
    case CASE_STRIDE(R_CLASS_CRASHDUMP_TRIGGER_SHADOWED, ALERT_CLASSD):
        val32 = regs->classes[CLASS_SLOT(reg)].crashdump_trigger_shadowed;
        break;
    case CASE_STRIDE(R_CLASS_PHASE0_CYC_SHADOWED, ALERT_CLASSA):
    case CASE_STRIDE(R_CLASS_PHASE0_CYC_SHADOWED, ALERT_CLASSB):
    case CASE_STRIDE(R_CLASS_PHASE0_CYC_SHADOWED, ALERT_CLASSC):
    case CASE_STRIDE(R_CLASS_PHASE0_CYC_SHADOWED, ALERT_CLASSD):
        val32 = regs->classes[CLASS_SLOT(reg)].phase0_cyc_shadowed;
        break;
    case CASE_STRIDE(R_CLASS_PHASE1_CYC_SHADOWED, ALERT_CLASSA):
    case CASE_STRIDE(R_CLASS_PHASE1_CYC_SHADOWED, ALERT_CLASSB):
    case CASE_STRIDE(R_CLASS_PHASE1_CYC_SHADOWED, ALERT_CLASSC):
    case CASE_STRIDE(R_CLASS_PHASE1_CYC_SHADOWED, ALERT_CLASSD):
        val32 = regs->classes[CLASS_SLOT(reg)].phase1_cyc_shadowed;
        break;
    case CASE_STRIDE(R_CLASS_PHASE2_CYC_SHADOWED, ALERT_CLASSA):
    case CASE_STRIDE(R_CLASS_PHASE2_CYC_SHADOWED, ALERT_CLASSB):
    case CASE_STRIDE(R_CLASS_PHASE2_CYC_SHADOWED, ALERT_CLASSC):
    case CASE_STRIDE(R_CLASS_PHASE2_CYC_SHADOWED, ALERT_CLASSD):
        val32 = regs->classes[CLASS_SLOT(reg)].phase2_cyc_shadowed;
        break;
    case CASE_STRIDE(R_CLASS_PHASE3_CYC_SHADOWED, ALERT_CLASSA):
    case CASE_STRIDE(R_CLASS_PHASE3_CYC_SHADOWED, ALERT_CLASSB):
    case CASE_STRIDE(R_CLASS_PHASE3_CYC_SHADOWED, ALERT_CLASSC):
    case CASE_STRIDE(R_CLASS_PHASE3_CYC_SHADOWED, ALERT_CLASSD):
        val32 = regs->classes[CLASS_SLOT(reg)].phase3_cyc_shadowed;
        break;
    case CASE_STRIDE(R_CLASS_ESC_CNT, ALERT_CLASSA):
    case CASE_STRIDE(R_CLASS_ESC_CNT, ALERT_CLASSB):
    case CASE_STRIDE(R_CLASS_ESC_CNT, ALERT_CLASSC):
    case CASE_STRIDE(R_CLASS_ESC_CNT, ALERT_CLASSD):
        val32 = regs->classes[CLASS_SLOT(reg)].esc_cnt;
        break;
    case CASE_STRIDE(R_CLASS_STATE, ALERT_CLASSA):
    case CASE_STRIDE(R_CLASS_STATE, ALERT_CLASSB):
    case CASE_STRIDE(R_CLASS_STATE, ALERT_CLASSC):
    case CASE_STRIDE(R_CLASS_STATE, ALERT_CLASSD):
        val32 =
            regs->classes[reg - CASE_STRIDE(R_CLASS_STATE, ALERT_CLASSA)].state;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        val32 = 0;
        break;
    }

    uint64_t pc = ibex_get_current_pc();
    trace_ot_alert_io_read_out((unsigned)addr, (uint64_t)val32, pc);

    return (uint64_t)val32;
};

static void ot_alert_regs_write(void *opaque, hwaddr addr, uint64_t val64,
                                unsigned size)
{
    OtAlertState *s = opaque;
    OtAlertRegs *regs = s->regs;
    uint32_t val32 = (uint32_t)val64;

    hwaddr reg = R32_OFF(addr);

    uint64_t pc = ibex_get_current_pc();
    trace_ot_alert_io_write((unsigned)addr, val64, pc);

    switch (reg) {
    case R_INTR_STATE:
        val32 &= INTR_MASK;
        regs->intr.state &= ~val32; /* RW1C */
        ot_alert_update_irqs(s);
        break;
    case R_INTR_ENABLE:
        val32 &= INTR_MASK;
        regs->intr.enable = val32;
        ot_alert_update_irqs(s);
        break;
    case R_INTR_TEST:
        val32 &= INTR_MASK;
        regs->intr.state |= val32;
        ot_alert_update_irqs(s);
        break;
    case R_PING_TIMER_REGWEN:
        val32 &= R_PING_TIMER_REGWEN_EN_MASK;
        regs->ping.timer_regwen &= ~val32; /* RW1C */
        break;
    case R_PING_TIMEOUT_CYC_SHADOWED:
        val32 &= R_PING_TIMEOUT_CYC_SHADOWED_VAL_MASK;
        regs->ping.timeout_cyc_shadowed = val32;
        break;
    case R_PING_TIMER_EN_SHADOWED:
        val32 = R_PING_TIMER_EN_SHADOWED_EN_MASK; /* RW1S */
        regs->ping.timer_en_shadowed |= val32;
        break;
    case CASE_RANGE(R_ALERT_REGWEN, PARAM_N_ALERTS):
        val32 &= ALERT_REGWEN_EN_MASK;
        regs->alerts[ALERT_SLOT(reg)].regwen &= val32; /* RW0C */
        break;
    case CASE_RANGE(R_ALERT_EN_SHADOWED, PARAM_N_ALERTS):
        if (CHECK_REGWEN(reg, regs->alerts[reg - R_ALERT_EN_SHADOWED].regwen)) {
            val32 &= ALERT_EN_SHADOWED_EN_MASK;
            regs->alerts[ALERT_SLOT(reg)].en_shadowed = val32;
        }
        break;
    case CASE_RANGE(R_ALERT_CLASS_SHADOWED, PARAM_N_ALERTS):
        if (CHECK_REGWEN(reg,
                         regs->alerts[reg - R_ALERT_CLASS_SHADOWED].regwen)) {
            val32 &= ALERT_CLASS_SHADOWED_EN_MASK;
            regs->alerts[ALERT_SLOT(reg)].en_shadowed = val32;
        }
        break;
    case CASE_RANGE(R_ALERT_CAUSE, PARAM_N_ALERTS):
        val32 = ALERT_CAUSE_EN_MASK;
        regs->alerts[ALERT_SLOT(reg)].cause &= ~val32; /* RW1C */
        break;
    case CASE_RANGE(R_LOC_ALERT_REGWEN, PARAM_N_LOC_ALERT):
        val32 &= LOC_ALERT_REGWEN_EN_MASK;
        regs->loc_alerts[LOC_ALERT_SLOT(reg)].regwen &= val32; /* RW0C */
        break;
    case CASE_RANGE(R_LOC_ALERT_EN_SHADOWED, PARAM_N_LOC_ALERT):
        if (CHECK_REGWEN(reg, regs->loc_alerts[LOC_ALERT_SLOT(reg)].regwen)) {
            val32 &= LOC_ALERT_EN_SHADOWED_EN_MASK;
            regs->loc_alerts[LOC_ALERT_SLOT(reg)].en_shadowed = val32;
        }
        break;
    case CASE_RANGE(R_LOC_ALERT_CLASS_SHADOWED, PARAM_N_LOC_ALERT):
        if (CHECK_REGWEN(reg, regs->loc_alerts[LOC_ALERT_SLOT(reg)].regwen)) {
            val32 &= LOC_ALERT_CLASS_SHADOWED_EN_MASK;
            regs->loc_alerts[LOC_ALERT_SLOT(reg)].en_shadowed = val32;
        }
        break;
    case CASE_RANGE(R_LOC_ALERT_CAUSE, PARAM_N_LOC_ALERT):
        val32 = LOC_ALERT_CAUSE_EN_MASK;
        regs->loc_alerts[LOC_ALERT_SLOT(reg)].cause &= ~val32; /* RW1C */
        break;
    case CASE_STRIDE(R_CLASS_REGWEN, ALERT_CLASSA):
    case CASE_STRIDE(R_CLASS_REGWEN, ALERT_CLASSB):
    case CASE_STRIDE(R_CLASS_REGWEN, ALERT_CLASSC):
    case CASE_STRIDE(R_CLASS_REGWEN, ALERT_CLASSD):
        val32 = R_CLASS_REGWEN_EN_MASK;
        regs->classes[CLASS_SLOT(reg)].regwen &= val32; /* RW0C */
        break;
    case CASE_STRIDE(R_CLASS_CTRL_SHADOWED, ALERT_CLASSA):
    case CASE_STRIDE(R_CLASS_CTRL_SHADOWED, ALERT_CLASSB):
    case CASE_STRIDE(R_CLASS_CTRL_SHADOWED, ALERT_CLASSC):
    case CASE_STRIDE(R_CLASS_CTRL_SHADOWED, ALERT_CLASSD):
        if (CHECK_REGWEN(reg, regs->classes[CLASS_SLOT(reg)].regwen)) {
            val32 &= CLASS_CTRL_SHADOWED_MASK;
            regs->classes[CLASS_SLOT(reg)].ctrl_shadowed = val32;
        }
        break;
    case CASE_STRIDE(R_CLASS_CLR_REGWEN, ALERT_CLASSA):
    case CASE_STRIDE(R_CLASS_CLR_REGWEN, ALERT_CLASSB):
    case CASE_STRIDE(R_CLASS_CLR_REGWEN, ALERT_CLASSC):
    case CASE_STRIDE(R_CLASS_CLR_REGWEN, ALERT_CLASSD):
        val32 &= CLASS_CLR_REGWEN_EN_MASK;
        regs->classes[CLASS_SLOT(reg)].clr_regwen &= val32; /* RW0C */
        break;
    case CASE_STRIDE(R_CLASS_CLR_SHADOWED, ALERT_CLASSA):
    case CASE_STRIDE(R_CLASS_CLR_SHADOWED, ALERT_CLASSB):
    case CASE_STRIDE(R_CLASS_CLR_SHADOWED, ALERT_CLASSC):
    case CASE_STRIDE(R_CLASS_CLR_SHADOWED, ALERT_CLASSD):
        if (CHECK_REGWEN(reg, regs->classes[CLASS_SLOT(reg)].clr_regwen)) {
            val32 &= CLASS_CLR_SHADOWED_EN_MASK;
            regs->classes[CLASS_SLOT(reg)].clr_shadowed = val32;
        }
        break;
    case CASE_STRIDE(R_CLASS_ACCUM_THRESH_SHADOWED, ALERT_CLASSA):
    case CASE_STRIDE(R_CLASS_ACCUM_THRESH_SHADOWED, ALERT_CLASSB):
    case CASE_STRIDE(R_CLASS_ACCUM_THRESH_SHADOWED, ALERT_CLASSC):
    case CASE_STRIDE(R_CLASS_ACCUM_THRESH_SHADOWED, ALERT_CLASSD):
        if (CHECK_REGWEN(reg, regs->classes[CLASS_SLOT(reg)].regwen)) {
            val32 &= CLASS_ACCUM_THRESH_SHADOWED_MASK;
            regs->classes[CLASS_SLOT(reg)].accum_thresh_shadowed = val32;
        }
        break;
    case CASE_STRIDE(R_CLASS_TIMEOUT_CYC_SHADOWED, ALERT_CLASSA):
    case CASE_STRIDE(R_CLASS_TIMEOUT_CYC_SHADOWED, ALERT_CLASSB):
    case CASE_STRIDE(R_CLASS_TIMEOUT_CYC_SHADOWED, ALERT_CLASSC):
    case CASE_STRIDE(R_CLASS_TIMEOUT_CYC_SHADOWED, ALERT_CLASSD):
        if (CHECK_REGWEN(reg, regs->classes[CLASS_SLOT(reg)].regwen)) {
            regs->classes[CLASS_SLOT(reg)].timeout_cyc_shadowed = val32;
        }
        break;
    case CASE_STRIDE(R_CLASS_CRASHDUMP_TRIGGER_SHADOWED, ALERT_CLASSA):
    case CASE_STRIDE(R_CLASS_CRASHDUMP_TRIGGER_SHADOWED, ALERT_CLASSB):
    case CASE_STRIDE(R_CLASS_CRASHDUMP_TRIGGER_SHADOWED, ALERT_CLASSC):
    case CASE_STRIDE(R_CLASS_CRASHDUMP_TRIGGER_SHADOWED, ALERT_CLASSD):
        if (CHECK_REGWEN(reg, regs->classes[CLASS_SLOT(reg)].regwen)) {
            val32 &= CLASS_CRASHDUMP_TRIGGER_SHADOWED_MASK;
            regs->classes[CLASS_SLOT(reg)].crashdump_trigger_shadowed = val32;
        }
        break;
    case CASE_STRIDE(R_CLASS_PHASE0_CYC_SHADOWED, ALERT_CLASSA):
    case CASE_STRIDE(R_CLASS_PHASE0_CYC_SHADOWED, ALERT_CLASSB):
    case CASE_STRIDE(R_CLASS_PHASE0_CYC_SHADOWED, ALERT_CLASSC):
    case CASE_STRIDE(R_CLASS_PHASE0_CYC_SHADOWED, ALERT_CLASSD):
        if (CHECK_REGWEN(reg, regs->classes[CLASS_SLOT(reg)].regwen)) {
            regs->classes[CLASS_SLOT(reg)].phase0_cyc_shadowed = val32;
        }
        break;
    case CASE_STRIDE(R_CLASS_PHASE1_CYC_SHADOWED, ALERT_CLASSA):
    case CASE_STRIDE(R_CLASS_PHASE1_CYC_SHADOWED, ALERT_CLASSB):
    case CASE_STRIDE(R_CLASS_PHASE1_CYC_SHADOWED, ALERT_CLASSC):
    case CASE_STRIDE(R_CLASS_PHASE1_CYC_SHADOWED, ALERT_CLASSD):
        if (CHECK_REGWEN(reg, regs->classes[CLASS_SLOT(reg)].regwen)) {
            regs->classes[CLASS_SLOT(reg)].phase1_cyc_shadowed = val32;
        }
        break;
    case CASE_STRIDE(R_CLASS_PHASE2_CYC_SHADOWED, ALERT_CLASSA):
    case CASE_STRIDE(R_CLASS_PHASE2_CYC_SHADOWED, ALERT_CLASSB):
    case CASE_STRIDE(R_CLASS_PHASE2_CYC_SHADOWED, ALERT_CLASSC):
    case CASE_STRIDE(R_CLASS_PHASE2_CYC_SHADOWED, ALERT_CLASSD):
        if (CHECK_REGWEN(reg, regs->classes[CLASS_SLOT(reg)].regwen)) {
            regs->classes[CLASS_SLOT(reg)].phase2_cyc_shadowed = val32;
        }
        break;
    case CASE_STRIDE(R_CLASS_PHASE3_CYC_SHADOWED, ALERT_CLASSA):
    case CASE_STRIDE(R_CLASS_PHASE3_CYC_SHADOWED, ALERT_CLASSB):
    case CASE_STRIDE(R_CLASS_PHASE3_CYC_SHADOWED, ALERT_CLASSC):
    case CASE_STRIDE(R_CLASS_PHASE3_CYC_SHADOWED, ALERT_CLASSD):
        if (CHECK_REGWEN(reg, regs->classes[CLASS_SLOT(reg)].regwen)) {
            regs->classes[CLASS_SLOT(reg)].phase3_cyc_shadowed = val32;
        }
        break;
    case CASE_STRIDE(R_CLASS_ACCUM_CNT, ALERT_CLASSA):
    case CASE_STRIDE(R_CLASS_ACCUM_CNT, ALERT_CLASSB):
    case CASE_STRIDE(R_CLASS_ACCUM_CNT, ALERT_CLASSC):
    case CASE_STRIDE(R_CLASS_ACCUM_CNT, ALERT_CLASSD):
    case CASE_STRIDE(R_CLASS_ESC_CNT, ALERT_CLASSA):
    case CASE_STRIDE(R_CLASS_ESC_CNT, ALERT_CLASSB):
    case CASE_STRIDE(R_CLASS_ESC_CNT, ALERT_CLASSC):
    case CASE_STRIDE(R_CLASS_ESC_CNT, ALERT_CLASSD):
    case CASE_STRIDE(R_CLASS_STATE, ALERT_CLASSA):
    case CASE_STRIDE(R_CLASS_STATE, ALERT_CLASSB):
    case CASE_STRIDE(R_CLASS_STATE, ALERT_CLASSC):
    case CASE_STRIDE(R_CLASS_STATE, ALERT_CLASSD):
        qemu_log_mask(LOG_GUEST_ERROR, "R/O register 0x02%" HWADDR_PRIx "\n",
                      addr);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        break;
    }
};

static const MemoryRegionOps ot_alert_regs_ops = {
    .read = &ot_alert_regs_read,
    .write = &ot_alert_regs_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4u,
    .impl.max_access_size = 4u,
};

static void ot_alert_reset_enter(Object *obj, ResetType type)
{
    OtAlertClass *c = OT_ALERT_GET_CLASS(obj);
    OtAlertState *s = OT_ALERT(obj);

    if (c->parent_phases.enter) {
        c->parent_phases.enter(obj, type);
    }

    OtAlertRegs *regs = s->regs;
    memset(regs, 0, sizeof(*regs));

    regs->ping.timer_regwen = 0x1u;
    regs->ping.timeout_cyc_shadowed = 0x100u;
    for (unsigned ix = 0; ix < PARAM_N_ALERTS; ix++) {
        regs->alerts[ix].regwen = 0x1u;
    }
    for (unsigned ix = 0; ix < PARAM_N_LOC_ALERT; ix++) {
        regs->loc_alerts[ix].regwen = 0x1u;
    }
    for (unsigned ix = 0; ix < PARAM_N_CLASSES; ix++) {
        regs->classes[ix].regwen = 0x1u;
        regs->classes[ix].ctrl_shadowed = 0x393cu;
        regs->classes[ix].clr_regwen = 0x1u;
    }

    ot_alert_update_irqs(s);
}

static void ot_alert_init(Object *obj)
{
    OtAlertState *s = OT_ALERT(obj);

    memory_region_init_io(&s->mmio, obj, &ot_alert_regs_ops, s, TYPE_OT_ALERT,
                          REGS_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mmio);

    s->regs = g_new0(OtAlertRegs, 1);

    for (unsigned ix = 0; ix < ARRAY_SIZE(s->irqs); ix++) {
        ibex_sysbus_init_irq(obj, &s->irqs[ix]);
    }
}

static void ot_alert_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);

    ResettableClass *rc = RESETTABLE_CLASS(klass);
    OtAlertClass *ac = OT_ALERT_CLASS(klass);
    resettable_class_set_parent_phases(rc, &ot_alert_reset_enter, NULL, NULL,
                                       &ac->parent_phases);
}

static const TypeInfo ot_alert_info = {
    .name = TYPE_OT_ALERT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(OtAlertState),
    .instance_init = &ot_alert_init,
    .class_size = sizeof(OtAlertClass),
    .class_init = &ot_alert_class_init,
};

static void ot_alert_register_types(void)
{
    type_register_static(&ot_alert_info);
}

type_init(ot_alert_register_types)
