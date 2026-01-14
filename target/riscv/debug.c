/*
 * QEMU RISC-V Native Debug Support
 *
 * Copyright (c) 2022 Wind River Systems, Inc.
 *
 * Author:
 *   Bin Meng <bin.meng@windriver.com>
 *
 * This provides the native debug support via the Trigger Module, as defined
 * in the RISC-V Debug Specification:
 * https://github.com/riscv/riscv-debug-spec/raw/master/riscv-debug-stable.pdf
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "cpu.h"
#include "trace.h"
#include "exec/helper-proto.h"
#include "exec/watchpoint.h"

/*
 * The following M-mode trigger CSRs are implemented:
 *
 * - tselect
 * - tdata1
 * - tdata2
 * - tdata3
 * - tinfo
 *
 * The following triggers are initialized by default:
 *
 * Index | Type |          tdata mapping | Description
 * ------+------+------------------------+------------
 *     0 |    2 |         tdata1, tdata2 | Address / Data Match
 *     1 |    2 |         tdata1, tdata2 | Address / Data Match
 */

/* tdata availability of a trigger */
typedef bool tdata_avail[TDATA_NUM];

static tdata_avail tdata_mapping[TRIGGER_TYPE_NUM] = {
    [TRIGGER_TYPE_NO_EXIST] = { false, false, false },
    [TRIGGER_TYPE_AD_MATCH] = { true, true, true },
    [TRIGGER_TYPE_INST_CNT] = { true, false, true },
    [TRIGGER_TYPE_INT] = { true, true, true },
    [TRIGGER_TYPE_EXCP] = { true, true, true },
    [TRIGGER_TYPE_AD_MATCH6] = { true, true, true },
    [TRIGGER_TYPE_EXT_SRC] = { true, false, false },
    [TRIGGER_TYPE_UNAVAIL] = { true, true, true }
};

/* only breakpoint size 1/2/4/8 supported */
static int access_size[SIZE_NUM] = {
    [SIZE_ANY] = 0,
    [SIZE_1B]  = 1,
    [SIZE_2B]  = 2,
    [SIZE_4B]  = 4,
    [SIZE_6B]  = -1,
    [SIZE_8B]  = 8,
    [6 ... 15] = -1,
};

static inline target_ulong extract_trigger_type(CPURISCVState *env,
                                                target_ulong tdata1)
{
    switch (riscv_cpu_mxl(env)) {
    case MXL_RV32:
        return extract32(tdata1, 28, 4);
    case MXL_RV64:
    case MXL_RV128:
        return extract64(tdata1, 60, 4);
    default:
        g_assert_not_reached();
    }
}

static inline target_ulong get_trigger_type(CPURISCVState *env,
                                            target_ulong trigger_index)
{
    SdtrigTrigger *trigger = &env->sdtrig_state.triggers[trigger_index];
    return extract_trigger_type(env, trigger->tdata1);
}

static trigger_action_t get_trigger_action(CPURISCVState *env,
                                           target_ulong trigger_index)
{
    SdtrigTrigger *trigger = &env->sdtrig_state.triggers[trigger_index];
    target_ulong tdata1 = trigger->tdata1;
    int trigger_type = get_trigger_type(env, trigger_index);
    trigger_action_t action = DBG_ACTION_NONE;

    switch (trigger_type) {
    case TRIGGER_TYPE_AD_MATCH:
        action = (tdata1 & TYPE2_ACTION) >> 12;
        break;
    case TRIGGER_TYPE_AD_MATCH6:
        action = (tdata1 & TYPE6_ACTION) >> 12;
        break;
    case TRIGGER_TYPE_INST_CNT:
        action = (tdata1 & ITRIGGER_ACTION);
        break;
    case TRIGGER_TYPE_INT:
    case TRIGGER_TYPE_EXCP:
    case TRIGGER_TYPE_EXT_SRC:
        qemu_log_mask(LOG_UNIMP, "trigger type: %d is not supported\n",
                      trigger_type);
        break;
    case TRIGGER_TYPE_NO_EXIST:
    case TRIGGER_TYPE_UNAVAIL:
        qemu_log_mask(LOG_GUEST_ERROR, "trigger type: %d does not exit\n",
                      trigger_type);
        break;
    default:
        g_assert_not_reached();
    }

    return action;
}

static inline target_ulong build_tdata1(CPURISCVState *env,
                                        trigger_type_t type,
                                        bool dmode, target_ulong data)
{
    target_ulong tdata1;

    switch (riscv_cpu_mxl(env)) {
    case MXL_RV32:
        tdata1 = RV32_TYPE(type) |
                 (dmode ? RV32_DMODE : 0) |
                 (data & RV32_DATA_MASK);
        break;
    case MXL_RV64:
    case MXL_RV128:
        tdata1 = RV64_TYPE(type) |
                 (dmode ? RV64_DMODE : 0) |
                 (data & RV64_DATA_MASK);
        break;
    default:
        g_assert_not_reached();
    }

    return tdata1;
}

bool tdata_available(CPURISCVState *env, int tdata_index)
{
    int trigger_type = get_trigger_type(env, env->sdtrig_state.trigger_cur);

    if (unlikely(tdata_index >= TDATA_NUM)) {
        return false;
    }

    return tdata_mapping[trigger_type][tdata_index];
}

target_ulong tselect_csr_read(CPURISCVState *env)
{
    return env->sdtrig_state.trigger_cur;
}

void tselect_csr_write(CPURISCVState *env, target_ulong val)
{
    CPUState *cs = env_cpu(env);
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(cs);

    if (val < mcc->def->debug_cfg->nr_triggers) {
        env->sdtrig_state.trigger_cur = val;
    }
}

static target_ulong tdata1_validate(CPURISCVState *env, target_ulong val,
                                    trigger_type_t t)
{
    uint32_t type, dmode;
    target_ulong tdata1;

    switch (riscv_cpu_mxl(env)) {
    case MXL_RV32:
        type = extract32(val, 28, 4);
        dmode = extract32(val, 27, 1);
        tdata1 = RV32_TYPE(t);
        break;
    case MXL_RV64:
    case MXL_RV128:
        type = extract64(val, 60, 4);
        dmode = extract64(val, 59, 1);
        tdata1 = RV64_TYPE(t);
        break;
    default:
        g_assert_not_reached();
    }

    if (type != t) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ignoring type write to tdata1 register\n");
    }

    if (dmode != 0) {
        qemu_log_mask(LOG_UNIMP, "debug mode is not supported\n");
    }

    return tdata1;
}

static inline void warn_always_zero_bit(target_ulong val, target_ulong mask,
                                        const char *msg)
{
    if (val & mask) {
        qemu_log_mask(LOG_UNIMP, "%s bit is always zero\n", msg);
    }
}

static target_ulong textra_validate(CPURISCVState *env, target_ulong tdata3)
{
    target_ulong mhvalue, mhselect;
    target_ulong mhselect_new;
    target_ulong textra;
    const uint32_t mhselect_no_rvh[8] = { 0, 0, 0, 0, 4, 4, 4, 4 };

    switch (riscv_cpu_mxl(env)) {
    case MXL_RV32:
        mhvalue  = get_field(tdata3, TEXTRA32_MHVALUE);
        mhselect = get_field(tdata3, TEXTRA32_MHSELECT);
        /* Validate unimplemented (always zero) bits */
        warn_always_zero_bit(tdata3, (target_ulong)TEXTRA32_SBYTEMASK,
                             "sbytemask");
        warn_always_zero_bit(tdata3, (target_ulong)TEXTRA32_SVALUE,
                             "svalue");
        warn_always_zero_bit(tdata3, (target_ulong)TEXTRA32_SSELECT,
                             "sselect");
        break;
    case MXL_RV64:
    case MXL_RV128:
        mhvalue  = get_field(tdata3, TEXTRA64_MHVALUE);
        mhselect = get_field(tdata3, TEXTRA64_MHSELECT);
        /* Validate unimplemented (always zero) bits */
        warn_always_zero_bit(tdata3, (target_ulong)TEXTRA64_SBYTEMASK,
                             "sbytemask");
        warn_always_zero_bit(tdata3, (target_ulong)TEXTRA64_SVALUE,
                             "svalue");
        warn_always_zero_bit(tdata3, (target_ulong)TEXTRA64_SSELECT,
                             "sselect");
        break;
    default:
        g_assert_not_reached();
    }

    /* Validate mhselect. */
    mhselect_new = mhselect_no_rvh[mhselect];
    if (mhselect != mhselect_new) {
        qemu_log_mask(LOG_UNIMP, "mhselect only supports 0 or 4 for now\n");
    }

    /* Write legal values into textra */
    textra = 0;
    switch (riscv_cpu_mxl(env)) {
    case MXL_RV32:
        textra = set_field(textra, TEXTRA32_MHVALUE,  mhvalue);
        textra = set_field(textra, TEXTRA32_MHSELECT, mhselect_new);
        break;
    case MXL_RV64:
    case MXL_RV128:
        textra = set_field(textra, TEXTRA64_MHVALUE,  mhvalue);
        textra = set_field(textra, TEXTRA64_MHSELECT, mhselect_new);
        break;
    default:
        g_assert_not_reached();
    }

    return textra;
}

/* Return true if an exception should be raised */
static bool do_trigger_action(CPURISCVState *env, target_ulong trigger_index)
{
    trigger_action_t action = get_trigger_action(env, trigger_index);

    switch (action) {
    case DBG_ACTION_NONE:
        break;
    case DBG_ACTION_BP:
        return true;
    case DBG_ACTION_DBG_MODE:
    case DBG_ACTION_TRACE0:
    case DBG_ACTION_TRACE1:
    case DBG_ACTION_TRACE2:
    case DBG_ACTION_TRACE3:
    case DBG_ACTION_EXT_DBG0:
    case DBG_ACTION_EXT_DBG1:
        qemu_log_mask(LOG_UNIMP, "action: %d is not supported\n", action);
        break;
    default:
        g_assert_not_reached();
    }
    return false;
}

/*
 * Check the privilege level of specific trigger matches CPU's current privilege
 * level.
 */
static bool type2_priv_match(CPURISCVState *env, target_ulong tdata1)
{
    /* type 2 trigger cannot be fired in VU/VS mode */
    if (env->virt_enabled) {
        return false;
    }
    /* check U/S/M bit against current privilege level */
    return (((tdata1 >> 3) & 0b1011) & BIT(env->priv));
}

static bool type6_priv_match(CPURISCVState *env, target_ulong tdata1)
{
    if (env->virt_enabled) {
        /* check VU/VS bit against current privilege level */
        return (((tdata1 >> 23) & 0b11) & BIT(env->priv));
    } else {
        /* check U/S/M bit against current privilege level */
        return (((tdata1 >> 3) & 0b1011) & BIT(env->priv));
    }
}

static bool icount_priv_match(CPURISCVState *env, target_ulong tdata1)
{
    if (env->virt_enabled) {
        /* check VU/VS bit against current privilege level */
        return (((tdata1 >> 25) & 0b11) & BIT(env->priv));
    } else {
        /* check U/S/M bit against current privilege level */
        return (((tdata1 >> 6) & 0b1011) & BIT(env->priv));
    }
}

static bool trigger_priv_match(CPURISCVState *env, trigger_type_t type,
                               int trigger_index)
{
    SdtrigTrigger *trigger = &env->sdtrig_state.triggers[trigger_index];
    target_ulong tdata1 = trigger->tdata1;

    switch (type) {
    case TRIGGER_TYPE_AD_MATCH:
        return type2_priv_match(env, tdata1);
    case TRIGGER_TYPE_AD_MATCH6:
        return type6_priv_match(env, tdata1);
    case TRIGGER_TYPE_INST_CNT:
        return icount_priv_match(env, tdata1);
    case TRIGGER_TYPE_INT:
    case TRIGGER_TYPE_EXCP:
    case TRIGGER_TYPE_EXT_SRC:
        qemu_log_mask(LOG_UNIMP, "trigger type: %d is not supported\n", type);
        break;
    case TRIGGER_TYPE_NO_EXIST:
    case TRIGGER_TYPE_UNAVAIL:
        qemu_log_mask(LOG_GUEST_ERROR, "trigger type: %d does not exist\n",
                      type);
        break;
    default:
        g_assert_not_reached();
    }

    return false;
}

static bool trigger_textra_match(CPURISCVState *env, trigger_type_t type,
                                 int trigger_index)
{
    SdtrigTrigger *trigger = &env->sdtrig_state.triggers[trigger_index];
    target_ulong textra = trigger->tdata3;
    target_ulong mhvalue, mhselect;

    if (type < TRIGGER_TYPE_AD_MATCH || type > TRIGGER_TYPE_AD_MATCH6) {
        /* textra checking is only applicable when type is 2, 3, 4, 5, or 6 */
        return true;
    }

    switch (riscv_cpu_mxl(env)) {
    case MXL_RV32:
        mhvalue  = get_field(textra, TEXTRA32_MHVALUE);
        mhselect = get_field(textra, TEXTRA32_MHSELECT);
        break;
    case MXL_RV64:
    case MXL_RV128:
        mhvalue  = get_field(textra, TEXTRA64_MHVALUE);
        mhselect = get_field(textra, TEXTRA64_MHSELECT);
        break;
    default:
        g_assert_not_reached();
    }

    /* Check mhvalue and mhselect. */
    switch (mhselect) {
    case MHSELECT_IGNORE:
        break;
    case MHSELECT_MCONTEXT:
        /* Match if the low bits of mcontext/hcontext equal mhvalue. */
        if (mhvalue != env->sdtrig_state.mcontext) {
            return false;
        }
        break;
    default:
        break;
    }

    return true;
}

/* Common matching conditions for all types of the triggers. */
static bool trigger_common_match(CPURISCVState *env, trigger_type_t type,
                                 int trigger_index)
{
    return trigger_priv_match(env, type, trigger_index) &&
           trigger_textra_match(env, type, trigger_index);
}

/* type 2 trigger */

static uint32_t type2_breakpoint_size(CPURISCVState *env, target_ulong ctrl)
{
    uint32_t sizelo, sizehi = 0;

    if (riscv_cpu_mxl(env) == MXL_RV64) {
        sizehi = extract32(ctrl, 21, 2);
    }
    sizelo = extract32(ctrl, 16, 2);
    return (sizehi << 2) | sizelo;
}

static inline bool type2_breakpoint_enabled(target_ulong ctrl)
{
    bool mode = !!(ctrl & (TYPE2_U | TYPE2_S | TYPE2_M));
    bool rwx = !!(ctrl & (TYPE2_LOAD | TYPE2_STORE | TYPE2_EXEC));

    return mode && rwx;
}

static target_ulong type2_mcontrol_validate(CPURISCVState *env,
                                            target_ulong ctrl)
{
    CPUState *cs = env_cpu(env);
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(cs);
    target_ulong index = env->sdtrig_state.trigger_cur;
    target_ulong val;
    target_ulong rwx_mask;
    uint32_t size;

    /* validate the generic part first */
    val = tdata1_validate(env, ctrl, TRIGGER_TYPE_AD_MATCH);

    /* validate unimplemented (always zero) bits */
    warn_always_zero_bit(ctrl, TYPE2_MATCH, "match");
    warn_always_zero_bit(ctrl, TYPE2_CHAIN, "chain");
    warn_always_zero_bit(ctrl, TYPE2_ACTION, "action");
    warn_always_zero_bit(ctrl, TYPE2_TIMING, "timing");
    warn_always_zero_bit(ctrl, TYPE2_SELECT, "select");
    warn_always_zero_bit(ctrl, TYPE2_HIT, "hit");

    /* validate size encoding */
    size = type2_breakpoint_size(env, ctrl);
    if (access_size[size] == -1) {
        qemu_log_mask(LOG_UNIMP, "access size %d is not supported, using "
                                 "SIZE_ANY\n", size);
    } else {
        val |= (ctrl & TYPE2_SIZELO);
        if (riscv_cpu_mxl(env) == MXL_RV64) {
            val |= (ctrl & TYPE2_SIZEHI);
        }
    }

    /* only set supported access (load/store/exec) bits */
    rwx_mask = mcc->def->debug_cfg->triggers[index].mcontrol_rwx_mask;
    val |= ctrl & rwx_mask;

    /* keep the mode bits */
    val |= ctrl & (TYPE2_U | TYPE2_S | TYPE2_M);

    return val;
}

static void type2_breakpoint_insert(CPURISCVState *env, target_ulong index)
{
    SdtrigTrigger *trigger = &env->sdtrig_state.triggers[index];
    target_ulong ctrl = trigger->tdata1;
    target_ulong addr = trigger->tdata2;
    bool enabled = type2_breakpoint_enabled(ctrl);
    CPUState *cs = env_cpu(env);
    int flags = BP_CPU | BP_STOP_BEFORE_ACCESS;
    uint32_t size, def_size;

    if (!enabled) {
        return;
    }

    if (ctrl & TYPE2_EXEC) {
        cpu_breakpoint_insert(cs, addr, flags,
                              &env->sdtrig_state.cpu_breakpoint[index]);
    }

    if (ctrl & TYPE2_LOAD) {
        flags |= BP_MEM_READ;
    }
    if (ctrl & TYPE2_STORE) {
        flags |= BP_MEM_WRITE;
    }

    if (flags & BP_MEM_ACCESS) {
        size = type2_breakpoint_size(env, ctrl);
        if (size != 0) {
            cpu_watchpoint_insert(cs, addr, size, flags,
                                  &env->sdtrig_state.cpu_watchpoint[index]);
        } else {
            def_size = riscv_cpu_mxl(env) == MXL_RV64 ? 8 : 4;

            cpu_watchpoint_insert(cs, addr, def_size, flags,
                                  &env->sdtrig_state.cpu_watchpoint[index]);
        }
    }
}

static void type2_breakpoint_remove(CPURISCVState *env, target_ulong index)
{
    CPUState *cs = env_cpu(env);

    if (env->sdtrig_state.cpu_breakpoint[index]) {
        cpu_breakpoint_remove_by_ref(cs,
                                     env->sdtrig_state.cpu_breakpoint[index]);
        env->sdtrig_state.cpu_breakpoint[index] = NULL;
    }

    if (env->sdtrig_state.cpu_watchpoint[index]) {
        cpu_watchpoint_remove_by_ref(cs,
                                     env->sdtrig_state.cpu_watchpoint[index]);
        env->sdtrig_state.cpu_watchpoint[index] = NULL;
    }
}

static void type2_reg_write(CPURISCVState *env, target_ulong index,
                            int tdata_index, target_ulong val)
{
    SdtrigTrigger *trigger = &env->sdtrig_state.triggers[index];
    switch (tdata_index) {
    case TDATA1:
        trigger->tdata1 = type2_mcontrol_validate(env, val);
        break;
    case TDATA2:
        trigger->tdata2 = val;
        break;
    case TDATA3:
        trigger->tdata3 = textra_validate(env, val);
        break;
    default:
        g_assert_not_reached();
    }

    type2_breakpoint_insert(env, index);
}

/* type 6 trigger */

static inline bool type6_breakpoint_enabled(target_ulong ctrl)
{
    bool mode = !!(ctrl & (TYPE6_VU | TYPE6_VS | TYPE6_U | TYPE6_S | TYPE6_M));
    bool rwx = !!(ctrl & (TYPE6_LOAD | TYPE6_STORE | TYPE6_EXEC));

    return mode && rwx;
}

static target_ulong type6_mcontrol6_validate(CPURISCVState *env,
                                             target_ulong ctrl)
{
    CPUState *cs = env_cpu(env);
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(cs);
    target_ulong index = env->sdtrig_state.trigger_cur;
    target_ulong val;
    target_ulong rwx_mask;
    uint32_t size;

    /* validate the generic part first */
    val = tdata1_validate(env, ctrl, TRIGGER_TYPE_AD_MATCH6);

    /* validate unimplemented (always zero) bits */
    warn_always_zero_bit(ctrl, TYPE6_MATCH, "match");
    warn_always_zero_bit(ctrl, TYPE6_CHAIN, "chain");
    warn_always_zero_bit(ctrl, TYPE6_ACTION, "action");
    warn_always_zero_bit(ctrl, TYPE6_TIMING, "timing");
    warn_always_zero_bit(ctrl, TYPE6_SELECT, "select");
    warn_always_zero_bit(ctrl, TYPE6_HIT, "hit");

    /* validate size encoding */
    size = extract32(ctrl, 16, 4);
    if (access_size[size] == -1) {
        qemu_log_mask(LOG_UNIMP, "access size %d is not supported, using "
                                 "SIZE_ANY\n", size);
    } else {
        val |= (ctrl & TYPE6_SIZE);
    }

    /* only set supported access (load/store/exec) bits */
    rwx_mask = mcc->def->debug_cfg->triggers[index].mcontrol_rwx_mask;
    val |= ctrl & rwx_mask;

    /* keep the mode bits */
    val |= (ctrl & (TYPE6_VU | TYPE6_VS | TYPE6_U | TYPE6_S | TYPE6_M));

    return val;
}

static void type6_breakpoint_insert(CPURISCVState *env, target_ulong index)
{
    SdtrigTrigger *trigger = &env->sdtrig_state.triggers[index];
    target_ulong ctrl = trigger->tdata1;
    target_ulong addr = trigger->tdata2;
    bool enabled = type6_breakpoint_enabled(ctrl);
    CPUState *cs = env_cpu(env);
    int flags = BP_CPU | BP_STOP_BEFORE_ACCESS;
    uint32_t size;

    if (!enabled) {
        return;
    }

    if (ctrl & TYPE6_EXEC) {
        cpu_breakpoint_insert(cs, addr, flags,
                              &env->sdtrig_state.cpu_breakpoint[index]);
    }

    if (ctrl & TYPE6_LOAD) {
        flags |= BP_MEM_READ;
    }

    if (ctrl & TYPE6_STORE) {
        flags |= BP_MEM_WRITE;
    }

    if (flags & BP_MEM_ACCESS) {
        size = extract32(ctrl, 16, 4);
        if (size != 0) {
            cpu_watchpoint_insert(cs, addr, size, flags,
                                  &env->sdtrig_state.cpu_watchpoint[index]);
        } else {
            cpu_watchpoint_insert(cs, addr, 8, flags,
                                  &env->sdtrig_state.cpu_watchpoint[index]);
        }
    }
}

static void type6_breakpoint_remove(CPURISCVState *env, target_ulong index)
{
    type2_breakpoint_remove(env, index);
}

static void type6_reg_write(CPURISCVState *env, target_ulong index,
                            int tdata_index, target_ulong val)
{
    SdtrigTrigger *trigger = &env->sdtrig_state.triggers[index];
    switch (tdata_index) {
    case TDATA1:
        trigger->tdata1 = type6_mcontrol6_validate(env, val);
        break;
    case TDATA2:
        trigger->tdata2 = val;
        break;
    case TDATA3:
        trigger->tdata3 = textra_validate(env, val);
        break;
    default:
        g_assert_not_reached();
    }
    type6_breakpoint_insert(env, index);
}

/* icount trigger type */
static inline int
itrigger_get_count(CPURISCVState *env, int index)
{
    SdtrigTrigger *trigger = &env->sdtrig_state.triggers[index];
    return get_field(trigger->tdata1, ITRIGGER_COUNT);
}

static inline void
itrigger_set_count(CPURISCVState *env, int index, int value)
{
    SdtrigTrigger *trigger = &env->sdtrig_state.triggers[index];
    trigger->tdata1 = set_field(trigger->tdata1, ITRIGGER_COUNT, value);
}

static bool riscv_itrigger_enabled(CPURISCVState *env)
{
    int count;

    for (int i = 0; i < RV_MAX_SDTRIG_TRIGGERS; i++) {
        if (get_trigger_type(env, i) != TRIGGER_TYPE_INST_CNT) {
            continue;
        }
        if (!trigger_common_match(env, TRIGGER_TYPE_INST_CNT, i)) {
            continue;
        }
        count = itrigger_get_count(env, i);
        if (!count) {
            continue;
        }
        return true;
    }

    return false;
}

/*
 * This is called by TCG when an instruction completes.
 * TCG runs in single-step mode when itrigger_enabled = true, so
 * it can call after each insn.
 */
void helper_itrigger_match(CPURISCVState *env)
{
    int count;
    bool enabled = false;

    g_assert(env->sdtrig_state.itrigger_enabled);

    for (int i = 0; i < RV_MAX_SDTRIG_TRIGGERS; i++) {
        if (get_trigger_type(env, i) != TRIGGER_TYPE_INST_CNT) {
            continue;
        }
        if (!trigger_common_match(env, TRIGGER_TYPE_INST_CNT, i)) {
            continue;
        }
        count = itrigger_get_count(env, i);
        if (!count) {
            continue;
        }
        itrigger_set_count(env, i, count--);
        if (!count) {
            if (do_trigger_action(env, i)) {
                riscv_raise_exception(env, RISCV_EXCP_BREAKPOINT, 0);
            }
        } else {
            enabled = true;
        }
    }
    env->sdtrig_state.itrigger_enabled = enabled;
}

static target_ulong itrigger_validate(CPURISCVState *env,
                                      target_ulong ctrl)
{
    target_ulong val;

    /* validate the generic part first */
    val = tdata1_validate(env, ctrl, TRIGGER_TYPE_INST_CNT);

    /* validate unimplemented (always zero) bits */
    warn_always_zero_bit(ctrl, ITRIGGER_ACTION, "action");
    warn_always_zero_bit(ctrl, ITRIGGER_HIT, "hit");
    warn_always_zero_bit(ctrl, ITRIGGER_PENDING, "pending");

    /* keep the mode and attribute bits */
    val |= ctrl & (ITRIGGER_VU | ITRIGGER_VS | ITRIGGER_U | ITRIGGER_S |
                   ITRIGGER_M | ITRIGGER_COUNT);

    return val;
}

static void itrigger_reg_write(CPURISCVState *env, target_ulong index,
                               int tdata_index, target_ulong val)
{
    SdtrigTrigger *trigger = &env->sdtrig_state.triggers[index];
    switch (tdata_index) {
    case TDATA1:
        trigger->tdata1 = itrigger_validate(env, val);
        break;
    case TDATA2:
        qemu_log_mask(LOG_UNIMP,
                      "tdata2 is not supported for icount trigger\n");
        break;
    case TDATA3:
        trigger->tdata3 = textra_validate(env, val);
        break;
    default:
        g_assert_not_reached();
    }
}

static void anytype_reg_write(CPURISCVState *env, target_ulong index,
                              int tdata_index, target_ulong val)
{
    SdtrigTrigger *trigger = &env->sdtrig_state.triggers[index];

    /*
     * This should check the value is valid for at least one of the supported
     * trigger types.
     */
    switch (tdata_index) {
    case TDATA1:
        trigger->tdata1 = val;
        break;
    case TDATA2:
        trigger->tdata2 = val;
        break;
    case TDATA3:
        trigger->tdata3 = val;
        break;
    default:
        g_assert_not_reached();
    }
}

target_ulong tdata_csr_read(CPURISCVState *env, int tdata_index)
{
    target_ulong index = env->sdtrig_state.trigger_cur;
    SdtrigTrigger *trigger = &env->sdtrig_state.triggers[index];

    switch (tdata_index) {
    case TDATA1:
        return trigger->tdata1;
    case TDATA2:
        return trigger->tdata2;
    case TDATA3:
        return trigger->tdata3;
    default:
        g_assert_not_reached();
    }
}

void tdata_csr_write(CPURISCVState *env, int tdata_index, target_ulong val)
{
    target_ulong index = env->sdtrig_state.trigger_cur;
    int trigger_type = get_trigger_type(env, index);
    bool check_itrigger = false;

    switch (trigger_type) {
    case TRIGGER_TYPE_AD_MATCH:
        type2_breakpoint_remove(env, index);
        break;
    case TRIGGER_TYPE_AD_MATCH6:
        type6_breakpoint_remove(env, index);
        break;
    case TRIGGER_TYPE_INST_CNT:
        /*
         * itrigger_enabled is the union of all enabled icount triggers,
         * so it's easiest to recheck all if any have changed (removed or
         * added or modified).
         */
        check_itrigger = true;
        break;
    default:
        break;
    }

    if (tdata_index == TDATA1) {
        CPUState *cs = env_cpu(env);
        RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(cs);

        if (val == 0) {
            /* special case, writing 0 results in disabled trigger */
            val = build_tdata1(env, TRIGGER_TYPE_UNAVAIL, 0, 0);
        }
        trigger_type = extract_trigger_type(env, val);
        if (!(mcc->def->debug_cfg->triggers[index].type_mask &
              (1 << trigger_type))) {
            val = build_tdata1(env, TRIGGER_TYPE_UNAVAIL, 0, 0);
            trigger_type = extract_trigger_type(env, val);
        }
    }

    switch (trigger_type) {
    case TRIGGER_TYPE_AD_MATCH:
        type2_reg_write(env, index, tdata_index, val);
        break;
    case TRIGGER_TYPE_AD_MATCH6:
        type6_reg_write(env, index, tdata_index, val);
        break;
    case TRIGGER_TYPE_INST_CNT:
        itrigger_reg_write(env, index, tdata_index, val);
        check_itrigger = true;
        break;
    case TRIGGER_TYPE_UNAVAIL:
        anytype_reg_write(env, index, tdata_index, val);
        break;
    case TRIGGER_TYPE_INT:
    case TRIGGER_TYPE_EXCP:
    case TRIGGER_TYPE_EXT_SRC:
        qemu_log_mask(LOG_UNIMP, "trigger type: %d is not supported\n",
                      trigger_type);
        break;
    case TRIGGER_TYPE_NO_EXIST:
        qemu_log_mask(LOG_GUEST_ERROR, "trigger type: %d does not exit\n",
                      trigger_type);
        break;
    default:
        g_assert_not_reached();
    }

    if (check_itrigger) {
        env->sdtrig_state.itrigger_enabled = riscv_itrigger_enabled(env);
    }
}

target_ulong tinfo_csr_read(CPURISCVState *env)
{
    CPUState *cs = env_cpu(env);
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(cs);
    target_ulong index = env->sdtrig_state.trigger_cur;

    /* XXX: should we set 1 (version 1.0) in the version field? */
    return mcc->def->debug_cfg->triggers[index].type_mask;
}

void riscv_cpu_debug_excp_handler(CPUState *cs)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;

    if (cs->watchpoint_hit) {
        if (cs->watchpoint_hit->flags & BP_CPU) {
            riscv_raise_exception(env, RISCV_EXCP_BREAKPOINT, 0);
        }
    } else {
        if (cpu_breakpoint_test(cs, env->pc, BP_CPU)) {
            riscv_raise_exception(env, RISCV_EXCP_BREAKPOINT, 0);
        }
    }
}

bool riscv_cpu_debug_check_breakpoint(CPUState *cs)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    CPUBreakpoint *bp;
    target_ulong ctrl;
    target_ulong pc;
    int trigger_type;
    int i;

    QTAILQ_FOREACH(bp, &cs->breakpoints, entry) {
        for (i = 0; i < RV_MAX_SDTRIG_TRIGGERS; i++) {
            SdtrigTrigger *trigger = &env->sdtrig_state.triggers[i];
            trigger_type = get_trigger_type(env, i);

            switch (trigger_type) {
            case TRIGGER_TYPE_AD_MATCH:
            case TRIGGER_TYPE_AD_MATCH6:
                break;
            default:
                continue; /* No other types match breakpoint */
            }

            if (!trigger_common_match(env, trigger_type, i)) {
                continue;
            }

            switch (trigger_type) {
            case TRIGGER_TYPE_AD_MATCH:
                ctrl = trigger->tdata1;
                pc = trigger->tdata2;

                if ((ctrl & TYPE2_EXEC) && (bp->pc == pc)) {
                    if (do_trigger_action(env, i)) {
                        env->badaddr = pc;
                        return true;
                    }
                }
                break;
            case TRIGGER_TYPE_AD_MATCH6:
                ctrl = trigger->tdata1;
                pc = trigger->tdata2;

                if ((ctrl & TYPE6_EXEC) && (bp->pc == pc)) {
                    if (do_trigger_action(env, i)) {
                        env->badaddr = pc;
                        return true;
                    }
                }
                break;
            default:
                g_assert_not_reached();
            }
        }
    }

    return false;
}

bool riscv_cpu_debug_check_watchpoint(CPUState *cs, CPUWatchpoint *wp)
{
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    target_ulong ctrl;
    target_ulong addr;
    int trigger_type;
    int flags;
    int i;

    for (i = 0; i < RV_MAX_SDTRIG_TRIGGERS; i++) {
        SdtrigTrigger *trigger = &env->sdtrig_state.triggers[i];
        trigger_type = get_trigger_type(env, i);

        switch (trigger_type) {
        case TRIGGER_TYPE_AD_MATCH:
        case TRIGGER_TYPE_AD_MATCH6:
            break;
        default:
            continue; /* No other types match watchpoint */
        }

        if (!trigger_common_match(env, trigger_type, i)) {
            continue;
        }

        switch (trigger_type) {
        case TRIGGER_TYPE_AD_MATCH:
            ctrl = trigger->tdata1;
            addr = trigger->tdata2;
            flags = 0;

            if (ctrl & TYPE2_LOAD) {
                flags |= BP_MEM_READ;
            }
            if (ctrl & TYPE2_STORE) {
                flags |= BP_MEM_WRITE;
            }

            if ((wp->flags & flags) && (wp->vaddr == addr)) {
                if (do_trigger_action(env, i)) {
                    env->badaddr = wp->vaddr;
                    return true;
                }
            }
            break;
        case TRIGGER_TYPE_AD_MATCH6:
            ctrl = trigger->tdata1;
            addr = trigger->tdata2;
            flags = 0;

            if (ctrl & TYPE6_LOAD) {
                flags |= BP_MEM_READ;
            }
            if (ctrl & TYPE6_STORE) {
                flags |= BP_MEM_WRITE;
            }

            if ((wp->flags & flags) && (wp->vaddr == addr)) {
                if (do_trigger_action(env, i)) {
                    env->badaddr = wp->vaddr;
                    return true;
                }
            }
            break;
        default:
            g_assert_not_reached();
        }
    }

    return false;
}

void riscv_cpu_debug_change_priv(CPURISCVState *env)
{
    env->sdtrig_state.itrigger_enabled = riscv_itrigger_enabled(env);
}

void riscv_cpu_debug_post_load(CPURISCVState *env)
{
    for (int i = 0; i < RV_MAX_SDTRIG_TRIGGERS; i++) {
        int trigger_type = get_trigger_type(env, i);

        switch (trigger_type) {
        case TRIGGER_TYPE_AD_MATCH:
            type2_breakpoint_insert(env, i);
            break;
        case TRIGGER_TYPE_AD_MATCH6:
            type6_breakpoint_insert(env, i);
            break;
        default:
            break;
        }
    }
    env->sdtrig_state.itrigger_enabled = riscv_itrigger_enabled(env);
}

void riscv_trigger_reset_hold(CPURISCVState *env)
{
    target_ulong tdata1 = build_tdata1(env, TRIGGER_TYPE_UNAVAIL, 0, 0);
    int i;

    /* init to type 15 (unavailable) triggers */
    for (i = 0; i < RV_MAX_SDTRIG_TRIGGERS; i++) {
        SdtrigTrigger *trigger = &env->sdtrig_state.triggers[i];
        int trigger_type = get_trigger_type(env, i);

        switch (trigger_type) {
        case TRIGGER_TYPE_AD_MATCH:
            type2_breakpoint_remove(env, i);
            break;
        case TRIGGER_TYPE_AD_MATCH6:
            type6_breakpoint_remove(env, i);
            break;
        default:
            break;
        }

        trigger->tdata1 = tdata1;
        trigger->tdata2 = 0;
        trigger->tdata3 = 0;
    }

    env->sdtrig_state.mcontext = 0;
}
