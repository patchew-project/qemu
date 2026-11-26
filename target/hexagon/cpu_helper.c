/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "cpu_helper.h"
#include "system/cpus.h"
#include "hw/core/boards.h"
#include "hw/hexagon/hexagon.h"
#include "hw/hexagon/hexagon_globalreg.h"
#include "hw/hexagon/hexagon_hvx_context.h"
#include "hw/hexagon/hexagon_pmu.h"
#include "pmu.h"
#include "hex_interrupts.h"
#include "hex_mmu.h"
#include "system/runstate.h"
#include "exec/cpu-interrupt.h"
#include "exec/target_page.h"
#include "accel/tcg/cpu-ldst.h"
#include "exec/cputlb.h"
#include "qemu/log.h"
#include "tcg/tcg-op.h"
#include "internal.h"
#include "macros.h"
#include "sys_macros.h"
#include "arch.h"
#include "hw/hexagon/hexagon_hlx.h"

#ifndef CONFIG_USER_ONLY

static bool hexagon_write_memory_small(CPUHexagonState *env, target_ulong addr,
                                       int byte_count, uint64_t data,
                                       int mmu_idx, uintptr_t retaddr)
{
    /* handle small sizes */
    switch (byte_count) {
    case 1:
        cpu_stb_mmuidx_ra(env, addr, (uint8_t)data, mmu_idx, retaddr);
        return true;

    case 2:
        cpu_stw_le_mmuidx_ra(env, addr, (uint16_t)data, mmu_idx, retaddr);
        return true;

    case 4:
        cpu_stl_le_mmuidx_ra(env, addr, (uint32_t)data, mmu_idx, retaddr);
        return true;

    case 8:
        cpu_stq_le_mmuidx_ra(env, addr, (uint64_t)data, mmu_idx, retaddr);
        return true;

    default:
        /* larger request, handle elsewhere */
        return false;
    }
}

void hexagon_write_memory(CPUHexagonState *env, target_ulong vaddr,
                          int size, uint64_t data, uintptr_t retaddr)
{
    CPUState *cs = env_cpu(env);
    unsigned mmu_idx = cpu_mmu_index(cs, false);
    if (!hexagon_write_memory_small(env, vaddr, size, data, mmu_idx, retaddr)) {
        cpu_abort(cs, "%s: ERROR: bad size = %d!\n", __func__, size);
    }
}

static inline uint32_t page_start(uint32_t addr)
{
    uint32_t page_align = ~(TARGET_PAGE_SIZE - 1);
    return addr & page_align;
}

void hexagon_peek_memory_range(CPUHexagonState *env, uint32_t start_addr,
                               uint32_t length, uintptr_t retaddr)
{
    CPUState *cs = env_cpu(env);
    unsigned int mmu_idx = cpu_mmu_index(cs, false);
    uint32_t first = page_start(start_addr);
    uint32_t last = page_start(start_addr + length - 1);

    for (uint32_t page = first; page <= last; page += TARGET_PAGE_SIZE) {
        cpu_ldub_mmuidx_ra(env, page, mmu_idx, retaddr);
    }
}

#endif

#define pmu_sum_all(field) ({                       \
    uint32_t total_ = 0;                            \
    CPU_FOREACH(cs) {                               \
        total_ += cpu_env(cs)->pmu.field;           \
    }                                               \
    total_;                                         \
})

static uint32_t hexagon_pmu_event_stats(CPUHexagonState *env, int event)
{
    CPUState *cs;

    g_assert(bql_locked());

    switch (event) {
    case PMU_NO_EVENT:
        return 0;

    case COMMITTED_PKT_ANY: {
        uint32_t total = 0;
        CPU_FOREACH(cs) {
            total += cpu_env(cs)->pmu.num_packets;
        }
        return total;
    }
    case HVX_PKT: {
        uint32_t total = 0;
        CPU_FOREACH(cs) {
            total += cpu_env(cs)->pmu.hvx_packets;
        }
        return total;
    }

    case COMMITTED_PKT_T0:
    case COMMITTED_PKT_T1:
    case COMMITTED_PKT_T2:
    case COMMITTED_PKT_T3:
    case COMMITTED_PKT_T4:
    case COMMITTED_PKT_T5:
    case COMMITTED_PKT_T6:
    case COMMITTED_PKT_T7:
    case COMMITTED_PKT_T8:
    case COMMITTED_PKT_T9:
    case COMMITTED_PKT_T10:
    case COMMITTED_PKT_T11:
    case COMMITTED_PKT_T12:
    case COMMITTED_PKT_T13:
    case COMMITTED_PKT_T14:
    case COMMITTED_PKT_T15: {
        unsigned int tid = pmu_committed_pkt_thread(event);
        CPU_FOREACH(cs) {
            CPUHexagonState *e = cpu_env(cs);
            if (e->threadId == tid) {
                return e->pmu.num_packets;
            }
        }
        return 0;
    }

    case COMMITTED_LD:
        return pmu_sum_all(committed_loads);
    case COMMITTED_ST:
        return pmu_sum_all(committed_stores);
    case COMMITTED_MEMOP:
        return pmu_sum_all(committed_memops);
    case HVX_PKT_THREAD:
        return env->pmu.xe_packets;
    case HVXPIPE_ALU:
        return pmu_sum_all(hvx_pipe_alu);
    case HVXPIPE_MPY:
        return pmu_sum_all(hvx_pipe_mpy);
    case HVXPIPE_SHIFT:
        return pmu_sum_all(hvx_pipe_shift);
    case HVXPIPE_PERM:
        return pmu_sum_all(hvx_pipe_perm);

    case HLXFIFO_PKT:
        return pmu_sum_all(hlx_packets);
    case HLXSLOT_ALU:
        return pmu_sum_all(hlx_slot_alu);
    case HLXSLOT_SHIFT:
        return pmu_sum_all(hlx_slot_shift);
    case HLXSLOT_MPY:
        return pmu_sum_all(hlx_slot_mpy);
    case HLXSLOT_LD:
        return pmu_sum_all(hlx_slot_ld);
    case HLXSLOT_ST:
        return pmu_sum_all(hlx_slot_st);
    case HLXLD:
        return pmu_sum_all(hlx_slot_ld) *
               (HLX_VEC_SIZE_BYTES / HLX_PMU_BEAT_BYTES);
    case HLXST:
        return pmu_sum_all(hlx_slot_st) *
               (HLX_VEC_SIZE_BYTES / HLX_PMU_BEAT_BYTES);

    default:
        return 0;
    }
}

#define DECL_PMU_EVENT(name, val) case name:
static bool hexagon_pmu_event_implemented(int event)
{
    switch (event) {
    HEX_PMU_IMPLEMENTED_EVENTS
        return true;
    default:
        return false;
    }
}
#undef DECL_PMU_EVENT

static void hexagon_pmu_log_if_unimplemented(HexagonPMUState *pmu, int event)
{
    if (!hexagon_pmu_event_implemented(event) &&
        !hexagon_pmu_test_and_set_unimp_logged(pmu, event)) {
        qemu_log_mask(LOG_UNIMP,
                     "PMU event 0x%x is not implemented; the counter "
                     "watching it will not advance\n", event);
    }
}

static uint32_t hexagon_pmu_counter_value(CPUHexagonState *env, int index)
{
    HexagonCPU *cpu = env_archcpu(env);
    uint16_t event;

    g_assert(bql_locked());
    g_assert(index >= 0 && index < NUM_PMU_CTRS);
    event = hexagon_pmu_get_event(cpu->pmu, index);
    return hexagon_pmu_get_offset(cpu->pmu, index) +
           hexagon_pmu_event_stats(env, event);
}

/*
 * A guest read of a counter.  An event is reported as unimplemented here and
 * not when it is selected: an event takes two register writes (PMUEVTCFG
 * and PMUCFG) to select, so the first can briefly select another event.
 */
uint32_t hexagon_get_pmu_counter(CPUHexagonState *env, int index)
{
    HexagonCPU *cpu = env_archcpu(env);

    hexagon_pmu_log_if_unimplemented(cpu->pmu,
                                     hexagon_pmu_get_event(cpu->pmu, index));
    return hexagon_pmu_counter_value(env, index);
}

/* Guest wrote PMUCNTn directly: rebase without disturbing the live tally. */
static void hexagon_set_pmu_counter(CPUHexagonState *env, uint32_t reg,
                                    uint32_t val)
{
    HexagonCPU *cpu = env_archcpu(env);
    unsigned int index = pmu_index_from_sreg(reg);
    uint16_t event = hexagon_pmu_get_event(cpu->pmu, index);
    uint32_t offset = val - hexagon_pmu_event_stats(env, event);

    hexagon_pmu_set_offset(cpu->pmu, index, offset);
}

/*
 * Guest switched counter @index to a new event: keep the counter's
 * displayed value continuous across the switch (real HW counters aren't
 * reset by re-pointing them at a different event).
 */
static void hexagon_set_pmu_event(CPUHexagonState *env, unsigned int index,
                                  uint16_t event)
{
    HexagonCPU *cpu = env_archcpu(env);
    uint32_t old_value = hexagon_pmu_counter_value(env, index);
    uint32_t new_offset;

    hexagon_pmu_set_event(cpu->pmu, index, event);
    new_offset = old_value - hexagon_pmu_event_stats(env, event);
    hexagon_pmu_set_offset(cpu->pmu, index, new_offset);
}

uint32_t hexagon_pmu_sreg_read(CPUHexagonState *env, uint32_t reg)
{
    HexagonCPU *cpu = env_archcpu(env);

    g_assert(bql_locked());
    if (IS_PMU_CNT_SREG(reg)) {
        return hexagon_get_pmu_counter(env, pmu_index_from_sreg(reg));
    }
    switch (reg) {
    case HEX_SREG_PMUEVTCFG:
    case HEX_SREG_PMUEVTCFG1:
    case HEX_SREG_PMUEVTCFG2: {
        uint32_t val = 0;
        unsigned int base = pmu_evtcfg_base(reg);
        for (unsigned int i = 0; i < 4; i++) {
            uint16_t event = hexagon_pmu_get_event(cpu->pmu, base + i);
            val = deposit32(val, i * 8, 8, event & 0xff);
        }
        return val;
    }
    case HEX_SREG_PMUCFG: {
        uint32_t val = 0;
        for (unsigned int i = 0; i < NUM_PMU_CTRS; i++) {
            uint16_t event = hexagon_pmu_get_event(cpu->pmu, i);
            val = deposit32(val, pmu_cfg_msb_shift(i), 2, (event >> 8) & 0x3);
        }
        val = deposit32(val, 16, 5, hexagon_pmu_get_thread_mask(cpu->pmu));
        return val;
    }
    case HEX_SREG_PMUSTID0:
    case HEX_SREG_PMUSTID1:
    case HEX_SREG_PMUSTID2:
        return hexagon_pmu_get_stid(cpu->pmu, pmu_stid_index(reg));
    default:
        g_assert_not_reached();
    }
}

void hexagon_pmu_sreg_write(CPUHexagonState *env, uint32_t reg, uint32_t val)
{
    HexagonCPU *cpu = env_archcpu(env);

    g_assert(bql_locked());
    if (IS_PMU_CNT_SREG(reg)) {
        hexagon_set_pmu_counter(env, reg, val);
        return;
    }
    switch (reg) {
    case HEX_SREG_PMUEVTCFG:
    case HEX_SREG_PMUEVTCFG1:
    case HEX_SREG_PMUEVTCFG2: {
        unsigned int base = pmu_evtcfg_base(reg);
        for (unsigned int i = 0; i < 4; i++) {
            unsigned int index = base + i;
            uint16_t old_event = hexagon_pmu_get_event(cpu->pmu, index);
            uint16_t new_event = deposit32(old_event, 0, 8,
                                           extract32(val, i * 8, 8));
            hexagon_set_pmu_event(env, index, new_event);
        }
        break;
    }
    case HEX_SREG_PMUCFG: {
        uint32_t new_thmask = extract32(val, 16, 5);

        if (new_thmask != 0) {
            qemu_log_mask(LOG_UNIMP,
                         "PMUCFG: only thread mode 0 is implemented\n");
        }
        hexagon_pmu_set_thread_mask(cpu->pmu, new_thmask);
        for (unsigned int i = 0; i < NUM_PMU_CTRS; i++) {
            uint16_t old_event = hexagon_pmu_get_event(cpu->pmu, i);
            uint16_t new_event = deposit32(old_event, 8, 2,
                                           extract32(val, pmu_cfg_msb_shift(i),
                                                     2));
            hexagon_set_pmu_event(env, i, new_event);
        }
        break;
    }
    case HEX_SREG_PMUSTID0:
    case HEX_SREG_PMUSTID1:
    case HEX_SREG_PMUSTID2:
        if (val != 0) {
            qemu_log_mask(LOG_UNIMP, "PMUSTID%u is not implemented\n",
                          pmu_stid_index(reg));
        }
        hexagon_pmu_set_stid(cpu->pmu, pmu_stid_index(reg), val);
        break;
    default:
        g_assert_not_reached();
    }
}

uint32_t hexagon_get_pmu_greg(CPUHexagonState *env, uint32_t greg)
{
    uint32_t ssr = env->t_sreg[HEX_SREG_SSR];

    if (!GET_SSR_FIELD(SSR_PE, ssr)) {
        return 0;
    }
    return hexagon_get_pmu_counter(env, pmu_index_from_greg(greg));
}

static void hexagon_resume_thread(CPUHexagonState *env)
{
    CPUState *cs = env_cpu(env);
    clear_wait_mode(env);
    cs = env_cpu(env);
    cpu_reset_interrupt(cs, CPU_INTERRUPT_HALT);
    cs->halted = false;
    cs->exception_index = HEX_EVENT_NONE;
    qemu_cpu_kick(cs);
}

void hexagon_resume_threads(CPUHexagonState *current_env, uint32_t mask)
{
    CPUState *cs;
    CPUHexagonState *env;

    g_assert(bql_locked());
    CPU_FOREACH(cs) {
        env = cpu_env(cs);
        g_assert(env->threadId < THREADS_MAX);
        if ((mask & (0x1 << env->threadId))) {
            if (get_exe_mode(env) == HEX_EXE_MODE_WAIT) {
                hexagon_resume_thread(env);
            }
        }
    }
}

static unsigned hexagon_hvx_context_count(HexagonCPU *cpu)
{
    unsigned n;

    for (n = 0; n < HVX_CONTEXTS_MAX; n++) {
        if (!cpu->hvx_ctx[n]) {
            break;
        }
    }
    return n;
}

static unsigned hexagon_hvx_context_index(HexagonCPU *cpu, uint8_t xa)
{
    unsigned n = hexagon_hvx_context_count(cpu);

    if (n == 0) {
        return 0;
    }
    if (xa >= n) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "SSR.XA %u is out of range for %u HVX contexts\n",
                      xa, n);
    }
    /*
     * The behavior here is unspecified, reference simulator uses
     * mappings that effectively keep the context in-range.  The
     * modulus seems just as good as any.
     */
    return xa % n;
}

/*
 * Diagnostic only.  Called separately from hexagon_hvx_select_context()
 * so migration post_load, where other CPUs' env->hvx may still be stale,
 * doesn't trip a false positive.
 */
static void hexagon_hvx_check_overcommit(CPUHexagonState *env, unsigned idx)
{
    CPUState *cs;
    unsigned users = 0;

    CPU_FOREACH(cs) {
        CPUHexagonState *other = cpu_env(cs);

        if (other->hvx == env->hvx &&
            GET_SSR_FIELD(SSR_XE, other->t_sreg[HEX_SREG_SSR])) {
            users++;
        }
    }

    if (users > 1) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "HVX context %u is enabled for %u hardware threads "
                      "at once, which is undefined\n", idx, users);
    }
}

unsigned hexagon_hvx_select_context(CPUHexagonState *env, uint32_t ssr)
{
    HexagonCPU *cpu = env_archcpu(env);
    unsigned idx = hexagon_hvx_context_index(cpu, GET_SSR_FIELD(SSR_XA, ssr));

    if (cpu->hvx_ctx[0]) {
        env->hvx = &cpu->hvx_ctx[idx]->regs;
    }
    return idx;
}

void hexagon_modify_ssr(CPUHexagonState *env, uint32_t new, uint32_t old)
{
    bool old_EX, old_IE;
    bool new_EX, new_IE;
    uint8_t old_asid, new_asid;

    g_assert(bql_locked());

    old_EX = GET_SSR_FIELD(SSR_EX, old);
    old_IE = GET_SSR_FIELD(SSR_IE, old);
    new_EX = GET_SSR_FIELD(SSR_EX, new);
    new_IE = GET_SSR_FIELD(SSR_IE, new);

    bool xa_changed = GET_SSR_FIELD(SSR_XA, new) != GET_SSR_FIELD(SSR_XA, old);
    bool xe_changed = GET_SSR_FIELD(SSR_XE, new) != GET_SSR_FIELD(SSR_XE, old);

    if (xa_changed || xe_changed) {
        unsigned idx = xa_changed
            ? hexagon_hvx_select_context(env, new)
            : hexagon_hvx_context_index(env_archcpu(env),
                                        GET_SSR_FIELD(SSR_XA, new));

        hexagon_hvx_check_overcommit(env, idx);
    }

    old_asid = GET_SSR_FIELD(SSR_ASID, old);
    new_asid = GET_SSR_FIELD(SSR_ASID, new);
    if (new_asid != old_asid) {
        hex_mmu_asid_change(env);
    }

    /* See if the interrupts have been enabled or we have exited EX mode */
    if ((new_IE && !old_IE) ||
        (!new_EX && old_EX)) {
        hex_interrupt_update(env);
    }
}

void clear_wait_mode(CPUHexagonState *env)
{
    HexagonCPU *cpu;
    uint32_t modectl, thread_wait_mask;

    g_assert(bql_locked());

    cpu = env_archcpu(env);
    if (cpu->globalregs) {
        modectl =
            hexagon_globalreg_read(cpu->globalregs, HEX_SREG_MODECTL,
                                   env->threadId);
        thread_wait_mask = GET_FIELD(MODECTL_W, modectl);
        thread_wait_mask &= ~(0x1 << env->threadId);
        SET_SYSTEM_FIELD(env, HEX_SREG_MODECTL, MODECTL_W, thread_wait_mask);
    }
}

void hexagon_ssr_set_cause(CPUHexagonState *env, uint32_t cause)
{
    uint32_t old, new;

    g_assert(bql_locked());

    old = env->t_sreg[HEX_SREG_SSR];
    SET_SYSTEM_FIELD(env, HEX_SREG_SSR, SSR_EX, 1);
    SET_SYSTEM_FIELD(env, HEX_SREG_SSR, SSR_CAUSE, cause);
    new = env->t_sreg[HEX_SREG_SSR];

    hexagon_modify_ssr(env, new, old);
}


int get_exe_mode(const CPUHexagonState *env)
{
    const HexagonCPU *cpu;
    uint32_t modectl, thread_enabled_mask, thread_wait_mask;
    uint32_t isdbst, debugmode;
    bool E_bit, W_bit, D_bit;

    g_assert(bql_locked());

    cpu = env_archcpu(env);
    modectl = cpu->globalregs ?
        hexagon_globalreg_read(cpu->globalregs, HEX_SREG_MODECTL,
                               env->threadId) : 0;
    thread_enabled_mask = GET_FIELD(MODECTL_E, modectl);
    E_bit = thread_enabled_mask & (0x1 << env->threadId);
    thread_wait_mask = GET_FIELD(MODECTL_W, modectl);
    W_bit = thread_wait_mask & (0x1 << env->threadId);
    if (cpu->cfg.hex_def->hex_version >= HEX_VER_V81) {
        isdbst = cpu->globalregs ?
            hexagon_globalreg_read(cpu->globalregs, HEX_SREG_ISDBST2,
                                   env->threadId) : 0;
        debugmode = GET_FIELD(ISDBST2_DEBUGMODE, isdbst);
    } else {
        isdbst = cpu->globalregs ?
            hexagon_globalreg_read(cpu->globalregs, HEX_SREG_ISDBST,
                                   env->threadId) : 0;
        debugmode = GET_FIELD(ISDBST_DEBUGMODE, isdbst);
    }
    D_bit = debugmode & (0x1 << env->threadId);

    if (!D_bit && !W_bit && !E_bit) {
        return HEX_EXE_MODE_OFF;
    }
    if (!D_bit && !W_bit && E_bit) {
        return HEX_EXE_MODE_RUN;
    }
    if (!D_bit && W_bit && E_bit) {
        return HEX_EXE_MODE_WAIT;
    }
    if (D_bit && !W_bit && E_bit) {
        return HEX_EXE_MODE_DEBUG;
    }
    g_assert_not_reached();
}

static uint32_t set_enable_mask(CPUHexagonState *env)
{
    HexagonCPU *cpu;
    uint32_t modectl, thread_enabled_mask;

    g_assert(bql_locked());

    cpu = env_archcpu(env);
    if (!cpu->globalregs) {
        return 0;
    }
    modectl =
        hexagon_globalreg_read(cpu->globalregs, HEX_SREG_MODECTL,
                               env->threadId);
    thread_enabled_mask = GET_FIELD(MODECTL_E, modectl);
    thread_enabled_mask |= 0x1 << env->threadId;
    SET_SYSTEM_FIELD(env, HEX_SREG_MODECTL, MODECTL_E, thread_enabled_mask);
    hex_interrupt_update(env);
    return thread_enabled_mask;
}

void hexagon_modify_ccr(CPUHexagonState *env, uint32_t new, uint32_t old)
{
    const uint32_t mask = (1u << reg_field_info[CCR_XA3].offset) |
                          (1u << reg_field_info[CCR_XE3].offset);
    bool old_XE3;
    bool new_XE3;
    uint32_t old_XA3;
    uint32_t new_XA3;
    HexagonCPU *cpu;
    HexagonHLXState *pool;

    g_assert(bql_locked());

    if (((old ^ new) & mask) == 0) {
        return;
    }

    old_XE3 = GET_FIELD(CCR_XE3, old);
    new_XE3 = GET_FIELD(CCR_XE3, new);
    old_XA3 = GET_FIELD(CCR_XA3, old);
    new_XA3 = GET_FIELD(CCR_XA3, new);
    cpu = HEXAGON_CPU(env_cpu(env));
    pool = cpu->hlx;

    if (!new_XE3) {
        env->hlx_ctx = NULL;
        return;
    }

    if (!old_XE3 || old_XA3 != new_XA3) {
        env->hlx_ctx = pool ? hexagon_hlx_get_context(pool, new_XA3) : NULL;
        /* A missing XA3 context makes HLX instructions trap. */
    }
}

static uint32_t clear_enable_mask(CPUHexagonState *env)
{
    HexagonCPU *cpu;
    uint32_t modectl, thread_enabled_mask;

    g_assert(bql_locked());

    cpu = env_archcpu(env);
    if (!cpu->globalregs) {
        return 0;
    }
    modectl =
        hexagon_globalreg_read(cpu->globalregs, HEX_SREG_MODECTL,
                               env->threadId);
    thread_enabled_mask = GET_FIELD(MODECTL_E, modectl);
    thread_enabled_mask &= ~(0x1 << env->threadId);
    SET_SYSTEM_FIELD(env, HEX_SREG_MODECTL, MODECTL_E, thread_enabled_mask);
    hex_interrupt_update(env);
    return thread_enabled_mask;
}
static void do_start_thread(CPUState *cs, run_on_cpu_data tbd)
{
    CPUHexagonState *env;

    BQL_LOCK_GUARD();

    env = cpu_env(cs);

    hexagon_cpu_soft_reset(env);

    set_enable_mask(env);

    cs->halted = 0;
    cs->exception_index = HEX_EVENT_NONE;
    cpu_resume(cs);
}

void hexagon_start_threads(CPUHexagonState *current_env, uint32_t mask)
{
    CPUState *cs;

    BQL_LOCK_GUARD();

    CPU_FOREACH(cs) {
        CPUHexagonState *env = cpu_env(cs);
        if (!(mask & (0x1 << env->threadId))) {
            continue;
        }

        if (current_env->threadId != env->threadId) {
            set_enable_mask(env);
            async_safe_run_on_cpu(cs, do_start_thread, RUN_ON_CPU_NULL);
        }
    }
}

/*
 * When we have all threads stopped, the return
 * value to the shell is register 2 from thread 0.
 */
static uint32_t get_thread0_r2(void)
{
    CPUState *cs;
    CPU_FOREACH(cs) {
        CPUHexagonState *thread = cpu_env(cs);
        if (thread->threadId == 0) {
            return thread->gpr[2];
        }
    }
    g_assert_not_reached();
}

void hexagon_stop_thread(CPUHexagonState *env)
{
    uint32_t thread_enabled_mask;
    CPUState *cs;

    BQL_LOCK_GUARD();

    thread_enabled_mask = clear_enable_mask(env);
    cs = env_cpu(env);
    cpu_interrupt(cs, CPU_INTERRUPT_HALT);
    if (!thread_enabled_mask) {
        /* All threads are stopped, request shutdown */
        qemu_system_shutdown_request_with_code(
            SHUTDOWN_CAUSE_GUEST_SHUTDOWN, get_thread0_r2());
    }
}

void hexagon_nmi(CPUHexagonState *env, uint32_t thread_mask)
{
    CPUState *cs;

    BQL_LOCK_GUARD();

    CPU_FOREACH(cs) {
        CPUHexagonState *thread = cpu_env(cs);

        if (!(thread_mask & (1u << thread->threadId)) ||
            get_exe_mode(thread) == HEX_EXE_MODE_OFF) {
            continue;
        }
        cpu_interrupt(cs, CPU_INTERRUPT_NMI);
    }
}

static int sys_in_monitor_mode_ssr(uint32_t ssr)
{
    if ((GET_SSR_FIELD(SSR_EX, ssr) != 0) ||
        ((GET_SSR_FIELD(SSR_EX, ssr) == 0) &&
         (GET_SSR_FIELD(SSR_UM, ssr) == 0))) {
        return 1;
    }
    return 0;
}

static int sys_in_guest_mode_ssr(uint32_t ssr)
{
    if ((GET_SSR_FIELD(SSR_EX, ssr) == 0) &&
        (GET_SSR_FIELD(SSR_UM, ssr) != 0) &&
        (GET_SSR_FIELD(SSR_GM, ssr) != 0)) {
        return 1;
    }
    return 0;
}

static int sys_in_user_mode_ssr(uint32_t ssr)
{
    if ((GET_SSR_FIELD(SSR_EX, ssr) == 0) &&
        (GET_SSR_FIELD(SSR_UM, ssr) != 0) &&
        (GET_SSR_FIELD(SSR_GM, ssr) == 0)) {
        return 1;
    }
    return 0;
}

int get_cpu_mode(const CPUHexagonState *env)
{
    uint32_t ssr = env->t_sreg[HEX_SREG_SSR];

    if (sys_in_monitor_mode_ssr(ssr)) {
        return HEX_CPU_MODE_MONITOR;
    } else if (sys_in_guest_mode_ssr(ssr)) {
        return HEX_CPU_MODE_GUEST;
    } else if (sys_in_user_mode_ssr(ssr)) {
        return HEX_CPU_MODE_USER;
    }
    return HEX_CPU_MODE_MONITOR;
}
