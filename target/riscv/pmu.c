/*
 * RISC-V PMU file.
 *
 * Copyright (c) 2021 Western Digital Corporation or its affiliates.
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
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "cpu.h"
#include "pmu.h"
#include "exec/icount.h"
#include "system/device_tree.h"

#define RISCV_TIMEBASE_FREQ 1000000000 /* 1Ghz */

/*
 * To keep it simple, any event can be mapped to any programmable counters in
 * QEMU. The generic cycle & instruction count events can also be monitored
 * using programmable counters. In that case, mcycle & minstret must continue
 * to provide the correct value as well. Heterogeneous PMU per hart is not
 * supported yet. Thus, number of counters are same across all harts.
 */
void riscv_pmu_generate_fdt_node(void *fdt, uint32_t cmask, char *pmu_name)
{
    uint32_t fdt_event_ctr_map[15] = {};

   /*
    * The event encoding is specified in the SBI specification
    * Event idx is a 20bits wide number encoded as follows:
    * event_idx[19:16] = type
    * event_idx[15:0] = code
    * The code field in cache events are encoded as follows:
    * event_idx.code[15:3] = cache_id
    * event_idx.code[2:1] = op_id
    * event_idx.code[0:0] = result_id
    */

   /* SBI_PMU_HW_CPU_CYCLES: 0x01 : type(0x00) */
   fdt_event_ctr_map[0] = cpu_to_be32(0x00000001);
   fdt_event_ctr_map[1] = cpu_to_be32(0x00000001);
   fdt_event_ctr_map[2] = cpu_to_be32(cmask | 1 << 0);

   /* SBI_PMU_HW_INSTRUCTIONS: 0x02 : type(0x00) */
   fdt_event_ctr_map[3] = cpu_to_be32(0x00000002);
   fdt_event_ctr_map[4] = cpu_to_be32(0x00000002);
   fdt_event_ctr_map[5] = cpu_to_be32(cmask | 1 << 2);

   /* SBI_PMU_HW_CACHE_DTLB : 0x03 READ : 0x00 MISS : 0x00 type(0x01) */
   fdt_event_ctr_map[6] = cpu_to_be32(0x00010019);
   fdt_event_ctr_map[7] = cpu_to_be32(0x00010019);
   fdt_event_ctr_map[8] = cpu_to_be32(cmask);

   /* SBI_PMU_HW_CACHE_DTLB : 0x03 WRITE : 0x01 MISS : 0x00 type(0x01) */
   fdt_event_ctr_map[9] = cpu_to_be32(0x0001001B);
   fdt_event_ctr_map[10] = cpu_to_be32(0x0001001B);
   fdt_event_ctr_map[11] = cpu_to_be32(cmask);

   /* SBI_PMU_HW_CACHE_ITLB : 0x04 READ : 0x00 MISS : 0x00 type(0x01) */
   fdt_event_ctr_map[12] = cpu_to_be32(0x00010021);
   fdt_event_ctr_map[13] = cpu_to_be32(0x00010021);
   fdt_event_ctr_map[14] = cpu_to_be32(cmask);

   /* This a OpenSBI specific DT property documented in OpenSBI docs */
   qemu_fdt_setprop(fdt, pmu_name, "riscv,event-to-mhpmcounters",
                    fdt_event_ctr_map, sizeof(fdt_event_ctr_map));
}

static bool riscv_pmu_counter_valid(RISCVCPU *cpu, uint32_t ctr_idx)
{
    CPURISCVState *env = &cpu->env;

    if (!RISCV_PMU_CTR_IS_HPM(ctr_idx)) {
        return true;
    }

    if (!(cpu->pmu_avail_ctrs & BIT(ctr_idx))) {
        return false;
    }

    return env->pmu_vendor_support && env->pmu_vendor_support(env, ctr_idx);
}

static int64_t pmu_icount_ticks_to_ns(int64_t value)
{
    int64_t ret = 0;

    if (icount_enabled()) {
        ret = icount_to_ns(value);
    } else {
        ret = (NANOSECONDS_PER_SECOND / RISCV_TIMEBASE_FREQ) * value;
    }

    return ret;
}

static bool pmu_hpmevent_is_of_set(CPURISCVState *env, uint32_t ctr_idx)
{
    target_ulong mhpmevent_val;
    uint64_t of_bit_mask;

    if (riscv_cpu_mxl(env) == MXL_RV32) {
        mhpmevent_val = env->mhpmeventh_val[ctr_idx];
        of_bit_mask = MHPMEVENTH_BIT_OF;
     } else {
        mhpmevent_val = env->mhpmevent_val[ctr_idx];
        of_bit_mask = MHPMEVENT_BIT_OF;
    }

    return get_field(mhpmevent_val, of_bit_mask);
}

static bool pmu_hpmevent_set_of_if_clear(CPURISCVState *env, uint32_t ctr_idx)
{
    target_ulong *mhpmevent_val;
    uint64_t of_bit_mask;

    if (riscv_cpu_mxl(env) == MXL_RV32) {
        mhpmevent_val = &env->mhpmeventh_val[ctr_idx];
        of_bit_mask = MHPMEVENTH_BIT_OF;
     } else {
        mhpmevent_val = &env->mhpmevent_val[ctr_idx];
        of_bit_mask = MHPMEVENT_BIT_OF;
    }

    if (!get_field(*mhpmevent_val, of_bit_mask)) {
        *mhpmevent_val |= of_bit_mask;
        return true;
    }

    return false;
}

static void pmu_timer_trigger_irq(RISCVCPU *cpu, uint32_t ctr_idx)
{
    CPURISCVState *env = &cpu->env;
    PMUCTRState *counter = &env->pmu_ctrs[ctr_idx];
    uint64_t ctr_val;
    RISCVException excp;

    /* Generate interrupt only if OF bit is clear */
    if (pmu_hpmevent_is_of_set(env, ctr_idx)) {
        return;
    }

    excp = riscv_pmu_ctr_read(env, ctr_idx, &ctr_val);

    if (excp != RISCV_EXCP_NONE) {
        return;
    }

    if (!counter->overflowed) {
        riscv_pmu_setup_timer(env, ctr_val, ctr_idx);
        return;
    }

    if (cpu->pmu_avail_ctrs & BIT(ctr_idx)) {
        if (pmu_hpmevent_set_of_if_clear(env, ctr_idx)) {
            riscv_cpu_update_mip(env, MIP_LCOFIP, BOOL_TO_MASK(1));
        }
    }
    counter->overflowed = false;
}

/* Timer callback for instret and cycle counter overflow */
void riscv_pmu_timer_cb(void *priv)
{
    RISCVCPU *cpu = priv;
    uint32_t ctr_idx;

    for (ctr_idx = 0; ctr_idx < RV_MAX_MHPMCOUNTERS; ctr_idx++) {
        if (riscv_pmu_counter_valid(cpu, ctr_idx)) {
            pmu_timer_trigger_irq(cpu, ctr_idx);
        }
    }
}

int riscv_pmu_setup_timer(CPURISCVState *env, uint64_t value, uint32_t ctr_idx)
{
    uint64_t overflow_delta, overflow_at, curr_ns;
    uint64_t overflow_ns;
    RISCVCPU *cpu = env_archcpu(env);
    PMUCTRState *counter = &env->pmu_ctrs[ctr_idx];

    /* No need to setup a timer if LCOFI is disabled when OF is set */
    if (!riscv_pmu_counter_valid(cpu, ctr_idx) || !cpu->cfg.ext_sscofpmf ||
        pmu_hpmevent_is_of_set(env, ctr_idx)) {
        return -1;
    }

    if (counter->overflowed) {
        pmu_timer_trigger_irq(cpu, ctr_idx);
        return 0;
    }

    if (value) {
        overflow_delta = UINT64_MAX - value + 1;
    } else {
        overflow_delta = UINT64_MAX;
    }

    /*
     * QEMU supports only int64_t timers while RISC-V counters are uint64_t.
     */
    overflow_ns = pmu_icount_ticks_to_ns(overflow_delta);
    curr_ns = (uint64_t)qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    if (uadd64_overflow(curr_ns, overflow_ns, &overflow_at) ||
        overflow_at > INT64_MAX) {
        overflow_at = INT64_MAX;
    }
    timer_mod_anticipate_ns(cpu->pmu_timer, overflow_at);

    return 0;
}


void riscv_pmu_init(RISCVCPU *cpu, Error **errp)
{
    if (cpu->cfg.pmu_mask & (COUNTEREN_CY | COUNTEREN_TM | COUNTEREN_IR)) {
        error_setg(errp, "\"pmu-mask\" contains invalid bits (0-2) set");
        return;
    }

    if (ctpop32(cpu->cfg.pmu_mask) > (RV_MAX_MHPMCOUNTERS - 3)) {
        error_setg(errp, "Number of counters exceeds maximum available");
        return;
    }

    cpu->pmu_avail_ctrs = cpu->cfg.pmu_mask;
}

uint32_t riscv_pmu_csrno_to_ctr_idx(int csrno)
{
    #define CASE_RANGE(low, high, offset) { \
        case (low)...(high): \
            return csrno - (low) + (offset); \
    }
    #define HPMCOUNTER_START (HPM_MINSTRET_IDX + 1)

    switch (csrno) {
    CASE_RANGE(CSR_MHPMEVENT3, CSR_MHPMEVENT31, HPMCOUNTER_START)
    CASE_RANGE(CSR_MHPMEVENT3H, CSR_MHPMEVENT31H, HPMCOUNTER_START)
    CASE_RANGE(CSR_HPMCOUNTER3, CSR_HPMCOUNTER31, HPMCOUNTER_START)
    CASE_RANGE(CSR_HPMCOUNTER3H, CSR_HPMCOUNTER31H, HPMCOUNTER_START)
    CASE_RANGE(CSR_MHPMCOUNTER3, CSR_MHPMCOUNTER31, HPMCOUNTER_START)
    CASE_RANGE(CSR_MHPMCOUNTER3H, CSR_MHPMCOUNTER31H, HPMCOUNTER_START)

    case CSR_MCYCLE:
    case CSR_MCYCLEH:
    case CSR_CYCLE:
    case CSR_CYCLEH:
    case CSR_MCYCLECFG:
    case CSR_MCYCLECFGH:
        return HPM_MCYCLE_IDX;

    case CSR_MINSTRET:
    case CSR_MINSTRETH:
    case CSR_INSTRET:
    case CSR_INSTRETH:
    case CSR_MINSTRETCFG:
    case CSR_MINSTRETCFGH:
        return HPM_MINSTRET_IDX;

    case CSR_TIME:
    case CSR_TIMEH:
        return HPM_MTIME_IDX;

    default:
        g_assert_not_reached();
    }

    #undef HPMCOUNTER_START
    #undef CASE_RANGE
}

static uint64_t get_ticks(bool instructions)
{
    if (icount_enabled()) {
        if (instructions) {
            return icount_get_raw();
        } else {
            return icount_get();
        }
    } else {
        return cpu_get_host_ticks();
    }
}

static bool riscv_pmu_general_ctr_is_running(CPURISCVState *env, uint32_t ctr_idx)
{
    #define PRIV_CASE(priv, nonvirt, nonvirth, virt, virth) { \
        case (priv): \
            if (env->virt_enabled) { \
                mask = (target_ulong) (virt); \
                maskh = (target_ulong) (virth); \
            } else { \
                mask = (target_ulong) (nonvirt); \
                maskh = (target_ulong) (nonvirth); \
            } \
            break; \
        }

    target_ulong event;
    target_ulong eventh;
    target_ulong mask = 0;
    target_ulong maskh = 0;

    if (!riscv_pmu_counter_valid(env_archcpu(env), ctr_idx)) {
        return false;
    }

    if (get_field(env->mcountinhibit, BIT(ctr_idx))) {
        return false;
    }

    if (RISCV_PMU_CTR_IS_HPM(ctr_idx) &&
            (env->mhpmevent_val[ctr_idx] == 0) && (env->mhpmeventh_val[ctr_idx] == 0)) {
        return false;
    }

    if ((riscv_cpu_cfg(env)->ext_smcntrpmf && !RISCV_PMU_CTR_IS_HPM(ctr_idx)) ||
        (riscv_cpu_cfg(env)->ext_sscofpmf && RISCV_PMU_CTR_IS_HPM(ctr_idx))) {
        if (ctr_idx == HPM_MCYCLE_IDX) {
            event = env->mcyclecfg;
            eventh = env->mcyclecfgh;
        } else if (ctr_idx == HPM_MTIME_IDX) {
            return true;
        } else if (ctr_idx == HPM_MINSTRET_IDX) {
            event = env->minstretcfg;
            eventh = env->minstretcfgh;
        } else {
            event = env->mhpmevent_val[ctr_idx];
            eventh = env->mhpmeventh_val[ctr_idx];
        }

        switch (env->priv) {
        PRIV_CASE(PRV_U, MHPMEVENT_BIT_UINH, MHPMEVENTH_BIT_UINH,
                         MHPMEVENT_BIT_VUINH, MHPMEVENTH_BIT_VUINH);
        PRIV_CASE(PRV_S, MHPMEVENT_BIT_SINH, MHPMEVENTH_BIT_SINH,
                         MHPMEVENT_BIT_VSINH, MHPMEVENTH_BIT_VSINH);
        PRIV_CASE(PRV_M, MHPMEVENT_BIT_MINH, MHPMEVENTH_BIT_MINH,
                         MHPMEVENT_BIT_MINH,  MHPMEVENTH_BIT_MINH);
        }

        bool match = !(event & mask);
        bool matchh = !(eventh & maskh);
        return riscv_cpu_mxl(env) == MXL_RV32 ? match && matchh : match;
    } else {
        return true;
    }
    #undef PRIV_CASE
}

static uint64_t riscv_pmu_ctr_delta_general(CPURISCVState *env, uint32_t ctr_idx)
{
    PMUCTRState *counter = &env->pmu_ctrs[ctr_idx];
    if (riscv_pmu_general_ctr_is_running(env, ctr_idx)) {
        return get_ticks(ctr_idx == HPM_MINSTRET_IDX) - counter->mhpmcounter_prev;
    } else {
        /*
         * We assume, what write() is called after each change of
         * inhibited/filtered status.
         *
         * So if counter is inhibited or filtered now, the delta is zero.
         * (By definition of `prev`).
         *
         * See documentation for PMUCTRState.
         */
        return 0;
    }
}

RISCVException riscv_pmu_ctr_read_general(CPURISCVState *env, uint32_t ctr_idx,
                                          uint64_t *value)
{

    PMUCTRState *counter = &env->pmu_ctrs[ctr_idx];
    int64_t delta = riscv_pmu_ctr_delta_general(env, ctr_idx);
    uint64_t result;

    counter->overflowed |=
        uadd64_overflow(counter->mhpmcounter_val, delta, &result);
    *value = result;
    return RISCV_EXCP_NONE;
}

RISCVException riscv_pmu_ctr_read(CPURISCVState *env, uint32_t ctr_idx,
                                  uint64_t *value)
{
    if (RISCV_PMU_CTR_IS_HPM(ctr_idx)) {
        if (!env->pmu_ctr_read) {
            *value = 0;
            return RISCV_EXCP_NONE;
        }

        return env->pmu_ctr_read(env, ctr_idx, value);
    } else {
        return riscv_pmu_ctr_read_general(env, ctr_idx, value);
    }
}

RISCVException riscv_pmu_ctr_write_general(CPURISCVState *env, uint32_t ctr_idx,
                                           uint64_t value)
{
    PMUCTRState *counter = &env->pmu_ctrs[ctr_idx];

    counter->mhpmcounter_prev +=
        riscv_pmu_ctr_delta_general(env, ctr_idx);
    counter->mhpmcounter_val = value;
    return RISCV_EXCP_NONE;
}

RISCVException riscv_pmu_ctr_write(CPURISCVState *env, uint32_t ctr_idx,
                                   uint64_t value)
{
    RISCVException excp;

    if (RISCV_PMU_CTR_IS_HPM(ctr_idx)) {
        if (!env->pmu_ctr_write) {
            value = 0;
            return RISCV_EXCP_NONE;
        }

        excp = env->pmu_ctr_write(env, ctr_idx, value);
    } else  {
        excp = riscv_pmu_ctr_write_general(env, ctr_idx, value);
    }

    if (excp != RISCV_EXCP_NONE) {
        return excp;
    }

    riscv_pmu_setup_timer(env, value, ctr_idx);

    return RISCV_EXCP_NONE;
}

void riscv_pmu_preserve_ctrs(CPURISCVState *env, riscv_pmu_preserved_ctrs_t data)
{
    RISCVException excp;

    for (uint32_t ctr_idx = 0; ctr_idx < RV_MAX_MHPMCOUNTERS; ctr_idx++) {
        excp = riscv_pmu_ctr_read(env, ctr_idx, &data[ctr_idx]);

        if (excp != RISCV_EXCP_NONE) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "Reading the counter %d value is failed "
                          "while changing the privilige mode",
                          ctr_idx);
            continue;
        }

    }
}

void riscv_pmu_restore_ctrs(CPURISCVState *env, riscv_pmu_preserved_ctrs_t data)
{
    RISCVException excp;

    for (uint32_t ctr_idx = 0; ctr_idx < RV_MAX_MHPMCOUNTERS; ctr_idx++) {
        excp = riscv_pmu_ctr_write(env, ctr_idx, data[ctr_idx]);

        if (excp != RISCV_EXCP_NONE) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "Writing to the counter %d value is failed "
                          "while changing the privilige mode",
                          ctr_idx);
            continue;
        }

    }
}
