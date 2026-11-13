/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch emulation helpers for CSRs
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "cpu.h"
#include "internals.h"
#include "qemu/host-utils.h"
#include "exec/helper-proto.h"
#include "exec/cputlb.h"
#include "accel/tcg/cpu-ldst.h"
#include "hw/core/irq.h"
#include "cpu-csr.h"
#include "cpu-mmu.h"

target_ulong HELPER(csrwr_stlbps)(CPULoongArchState *env,
                                 target_ulong val, uint32_t vm_level)
{
    CPUSysState *sys = get_sys(env, vm_level);
    int64_t old_v = sys->CSR_STLBPS;

    /*
     * The real hardware only supports the min tlb_ps is 12
     * tlb_ps=0 may cause undefined-behavior.
     */
    uint8_t tlb_ps = FIELD_EX64(val, CSR_STLBPS, PS);
    if (!check_ps(env, tlb_ps)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Attempted set ps %d\n", tlb_ps);
    } else {
        /* Only update PS field, reserved bit keeps zero */
        val = FIELD_DP64(val, CSR_STLBPS, RESERVE, 0);
        sys->CSR_STLBPS = val;
    }

    return old_v;
}

target_ulong HELPER(csrrd_pgd)(CPULoongArchState *env, uint32_t vm_level)
{
    int64_t v;
    CPUSysState *sys = get_sys(env, vm_level);

    if (sys->CSR_TLBRERA & 0x1) {
        v = sys->CSR_TLBRBADV;
    } else {
        v = sys->CSR_BADV;
    }

    if ((v >> 63) & 0x1) {
        v = sys->CSR_PGDH;
    } else {
        v = sys->CSR_PGDL;
    }

    return v;
}

target_ulong HELPER(csrrd_cpuid)(CPULoongArchState *env, uint32_t vm_level)
{
    LoongArchCPU *lac = env_archcpu(env);
    CPUSysState *sys = get_sys(env, vm_level);

    if (vm_level) {
        return sys->CSR_CPUID;
    }

    sys->CSR_CPUID = CPU(lac)->cpu_index;

    return sys->CSR_CPUID;
}

target_ulong HELPER(csrrd_tval)(CPULoongArchState *env, uint32_t vm_level)
{
    CPUTimerState *timer = vm_level ? env_guest_timer(env) : env_timer(env);
    return cpu_loongarch_get_timer_ticks(timer);
}

target_ulong HELPER(csrrd_msgir)(CPULoongArchState *env, uint32_t vm_level)
{
    int irq, new;
    CPUSysState *sys = get_sys(env, vm_level);

    irq = find_first_bit((unsigned long *)sys->CSR_MSGIS, 256);
    if (irq < 256) {
        clear_bit(irq, (unsigned long *)sys->CSR_MSGIS);
        new = find_first_bit((unsigned long *)sys->CSR_MSGIS, 256);
        if (new < 256) {
            return irq;
        }

        sys->CSR_ESTAT = FIELD_DP64(sys->CSR_ESTAT, CSR_ESTAT, MSGINT, 0);
    } else {
        /* bit 31 set 1 for no invalid irq */
        irq = BIT(31);
    }

    return irq;
}

target_ulong HELPER(csrwr_estat)(CPULoongArchState *env,
                                target_ulong val, uint32_t vm_level)
{
    CPUSysState *sys = get_sys(env, vm_level);
    int64_t old_v = sys->CSR_ESTAT;

    /* When guest = 0, only IS[1:0] can be written.
     * When guest = 1, ecode and esubcode of
     * VM_LEVEL1 can be written by VM_LEVEL0.
     */
    sys->CSR_ESTAT = deposit64(sys->CSR_ESTAT, 0, 2, val);
    if (vm_level) {
        sys->CSR_ESTAT = deposit64(sys->CSR_ESTAT, 2, 11,
                                   extract64(val, 2, 11));
        sys->CSR_ESTAT = deposit64(sys->CSR_ESTAT, 16, 15,
                                   extract64(val, 16, 15));
    }
    /*
     * Software interrupts (SWI0/SWI1) are latched in CSR.ESTAT.IS[1:0].
     * Make sure the CPU interrupt request state tracks the pending bits,
     * matching the behavior of loongarch_cpu_set_irq().
     */
    if (sys->CSR_ESTAT != old_v) {
        bql_lock();
        loongarch_cpu_update_irq(env_archcpu(env), vm_level);
        bql_unlock();
    }

    return old_v;
}

target_ulong HELPER(csrwr_asid)(CPULoongArchState *env,
                               target_ulong val, uint32_t vm_level)
{
    CPUSysState *sys = get_sys(env, vm_level);
    int64_t old_v = sys->CSR_ASID;

    /* Only ASID filed of CSR_ASID can be written */
    sys->CSR_ASID = deposit64(sys->CSR_ASID, 0, 10, val);
    if (old_v != sys->CSR_ASID) {
        tlb_flush(env_cpu(env));
    }
    return old_v;
}

target_ulong HELPER(csrwr_tcfg)(CPULoongArchState *env,
                               target_ulong val, uint32_t vm_level)
{
    CPUTimerState *timer = vm_level ? env_guest_timer(env) : env_timer(env);
    CPUSysState *sys = container_of(timer, CPUSysState, timer_state);
    int64_t old_v = sys->CSR_TCFG;

    cpu_loongarch_set_timer_config(timer, val);

    return old_v;
}

target_ulong HELPER(csrwr_ticlr)(CPULoongArchState *env,
                                target_ulong val, uint32_t vm_level)
{
    CPUTimerState *timer = vm_level ? env_guest_timer(env) : env_timer(env);
    int64_t old_v = 0;

    if (val & 0x1) {
        bql_lock();
        loongarch_cpu_set_irq(LOONGARCH_CPU(timer->cs), timer->irq, 0);
        bql_unlock();
    }
    return old_v;
}

target_ulong HELPER(csrwr_pwcl)(CPULoongArchState *env,
                               target_ulong val, uint32_t vm_level)
{
    uint8_t shift, ptbase;
    CPUSysState *sys = get_sys(env, vm_level);
    int64_t old_v = sys->CSR_PWCL;

    /*
     * The real hardware only supports 64bit PTE width now, 128bit or others
     * treated as illegal.
     */
    shift = FIELD_EX64(val, CSR_PWCL, PTEWIDTH);
    ptbase = FIELD_EX64(val, CSR_PWCL, PTBASE);
    if (shift) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Attempted set pte width with %d bit\n", 64 << shift);
        val = FIELD_DP64(val, CSR_PWCL, PTEWIDTH, 0);
    }
    if (!check_ps(env, ptbase)) {
         qemu_log_mask(LOG_GUEST_ERROR,
                      "Attempted set ptbase 2^%d\n", ptbase);
    }
    sys->CSR_PWCL = val;
    return old_v;
}

target_ulong HELPER(csrwr_pwch)(CPULoongArchState *env,
                               target_ulong val, uint32_t vm_level)
{
    uint8_t host_has_ptw;
    CPUSysState *sys = get_sys(env, vm_level);
    int64_t old_v = sys->CSR_PWCH;

    val = FIELD_DP64(val, CSR_PWCH, RESERVE, 0);
    //host can't read guest cpucfg
    host_has_ptw = FIELD_EX32(env->cpucfg[2], CPUCFG2, HPTW);
    if (!host_has_ptw) {
        val = FIELD_DP64(val, CSR_PWCH, HPTW_EN, 0);
    }

    sys->CSR_PWCH = val;
    return old_v;
}

target_ulong HELPER(csrwr_gstat)(CPULoongArchState *env,
                                target_ulong val, uint32_t vm_level)
{
    CPUSysState *sys = get_sys(env, VM_LEVEL0);
    int64_t old_v = sys->CSR_GSTAT;
    uint8_t old_gid = FIELD_EX64(sys->CSR_GSTAT, CSR_GSTAT, GID);

    sys->CSR_GSTAT = FIELD_DP64(sys->CSR_GSTAT, CSR_GSTAT, PVM,
                                 FIELD_EX64(val, CSR_GSTAT, PVM));
    sys->CSR_GSTAT = FIELD_DP64(sys->CSR_GSTAT, CSR_GSTAT, GID,
                                 FIELD_EX64(val, CSR_GSTAT, GID));

    if (old_gid != FIELD_EX64(sys->CSR_GSTAT, CSR_GSTAT, GID)) {
        tlb_flush(env_cpu(env));
    }

    return old_v;
}

target_ulong HELPER(csrwr_gtlbc)(CPULoongArchState *env,
                                target_ulong val, uint32_t vm_level)
{
    CPUSysState *sys = get_sys(env, VM_LEVEL0);
    int64_t old_v = sys->CSR_GTLBC;
    uint8_t old_use_tgid = FIELD_EX64(old_v, CSR_GTLBC, USETGID);
    uint8_t old_tgid = FIELD_EX64(old_v, CSR_GTLBC, TGID);

    sys->CSR_GTLBC = val;
    if (old_use_tgid != FIELD_EX64(sys->CSR_GTLBC, CSR_GTLBC, USETGID) ||
        old_tgid != FIELD_EX64(sys->CSR_GTLBC, CSR_GTLBC, TGID)) {
        tlb_flush(env_cpu(env));
    }

    return old_v;
}

target_ulong HELPER(csrwr_gintc)(CPULoongArchState *env,
                                target_ulong val, uint32_t vm_level)
{
    CPUSysState *host = get_sys(env, VM_LEVEL0);
    CPUSysState *guest = get_sys(env, VM_LEVEL1);
    int64_t old_v = host->CSR_GINTC;
    uint8_t hwis = FIELD_EX64(val, CSR_GINTC, HWIS);
    /* TODO: hwip and hwic support */

    host->CSR_GINTC = val & 0xffff00;
    guest->CSR_ESTAT = deposit64(guest->CSR_ESTAT, 2, 8,
                                 hwis);

    return old_v;
}

