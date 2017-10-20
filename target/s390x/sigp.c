/*
 * s390x SIGP instruction handling
 *
 * Copyright (c) 2009 Alexander Graf <agraf@suse.de>
 * Copyright IBM Corp. 2012
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "cpu.h"
#include "internal.h"
#include "sysemu/hw_accel.h"
#include "exec/address-spaces.h"
#include "sysemu/sysemu.h"
#include "trace.h"

QemuMutex qemu_sigp_mutex;

typedef struct SigpInfo {
    uint64_t param;
    int cc;
    uint64_t *status_reg;
} SigpInfo;

static void set_sigp_status(SigpInfo *si, uint64_t status)
{
    *si->status_reg &= 0xffffffff00000000ULL;
    *si->status_reg |= status;
    si->cc = SIGP_CC_STATUS_STORED;
}

static void sigp_start(CPUState *cs, run_on_cpu_data arg)
{
    S390CPU *cpu = S390_CPU(cs);
    SigpInfo *si = arg.host_ptr;

    if (s390_cpu_get_state(cpu) != CPU_STATE_STOPPED) {
        si->cc = SIGP_CC_ORDER_CODE_ACCEPTED;
        return;
    }

    s390_cpu_set_state(CPU_STATE_OPERATING, cpu);
    si->cc = SIGP_CC_ORDER_CODE_ACCEPTED;
}

static void sigp_stop(CPUState *cs, run_on_cpu_data arg)
{
    S390CPU *cpu = S390_CPU(cs);
    SigpInfo *si = arg.host_ptr;

    if (s390_cpu_get_state(cpu) != CPU_STATE_OPERATING) {
        si->cc = SIGP_CC_ORDER_CODE_ACCEPTED;
        return;
    }

    /* disabled wait - sleeping in user space */
    if (cs->halted) {
        s390_cpu_set_state(CPU_STATE_STOPPED, cpu);
    } else {
        /* execute the stop function */
        cpu->env.sigp_order = SIGP_STOP;
        cpu_inject_stop(cpu);
    }
    si->cc = SIGP_CC_ORDER_CODE_ACCEPTED;
}

static void sigp_stop_and_store_status(CPUState *cs, run_on_cpu_data arg)
{
    S390CPU *cpu = S390_CPU(cs);
    SigpInfo *si = arg.host_ptr;

    /* disabled wait - sleeping in user space */
    if (s390_cpu_get_state(cpu) == CPU_STATE_OPERATING && cs->halted) {
        s390_cpu_set_state(CPU_STATE_STOPPED, cpu);
    }

    switch (s390_cpu_get_state(cpu)) {
    case CPU_STATE_OPERATING:
        cpu->env.sigp_order = SIGP_STOP_STORE_STATUS;
        cpu_inject_stop(cpu);
        /* store will be performed in do_stop_interrup() */
        break;
    case CPU_STATE_STOPPED:
        /* already stopped, just store the status */
        cpu_synchronize_state(cs);
        s390_store_status(cpu, S390_STORE_STATUS_DEF_ADDR, true);
        break;
    }
    si->cc = SIGP_CC_ORDER_CODE_ACCEPTED;
}

static void sigp_store_status_at_address(CPUState *cs, run_on_cpu_data arg)
{
    S390CPU *cpu = S390_CPU(cs);
    SigpInfo *si = arg.host_ptr;
    uint32_t address = si->param & 0x7ffffe00u;

    /* cpu has to be stopped */
    if (s390_cpu_get_state(cpu) != CPU_STATE_STOPPED) {
        set_sigp_status(si, SIGP_STAT_INCORRECT_STATE);
        return;
    }

    cpu_synchronize_state(cs);

    if (s390_store_status(cpu, address, false)) {
        set_sigp_status(si, SIGP_STAT_INVALID_PARAMETER);
        return;
    }
    si->cc = SIGP_CC_ORDER_CODE_ACCEPTED;
}

#define ADTL_SAVE_LC_MASK  0xfUL
static void sigp_store_adtl_status(CPUState *cs, run_on_cpu_data arg)
{
    S390CPU *cpu = S390_CPU(cs);
    SigpInfo *si = arg.host_ptr;
    uint8_t lc = si->param & ADTL_SAVE_LC_MASK;
    hwaddr addr = si->param & ~ADTL_SAVE_LC_MASK;
    hwaddr len = 1UL << (lc ? lc : 10);

    if (!s390_has_feat(S390_FEAT_VECTOR) &&
        !s390_has_feat(S390_FEAT_GUARDED_STORAGE)) {
        set_sigp_status(si, SIGP_STAT_INVALID_ORDER);
        return;
    }

    /* cpu has to be stopped */
    if (s390_cpu_get_state(cpu) != CPU_STATE_STOPPED) {
        set_sigp_status(si, SIGP_STAT_INCORRECT_STATE);
        return;
    }

    /* address must be aligned to length */
    if (addr & (len - 1)) {
        set_sigp_status(si, SIGP_STAT_INVALID_PARAMETER);
        return;
    }

    /* no GS: only lc == 0 is valid */
    if (!s390_has_feat(S390_FEAT_GUARDED_STORAGE) &&
        lc != 0) {
        set_sigp_status(si, SIGP_STAT_INVALID_PARAMETER);
        return;
    }

    /* GS: 0, 10, 11, 12 are valid */
    if (s390_has_feat(S390_FEAT_GUARDED_STORAGE) &&
        lc != 0 &&
        lc != 10 &&
        lc != 11 &&
        lc != 12) {
        set_sigp_status(si, SIGP_STAT_INVALID_PARAMETER);
        return;
    }

    cpu_synchronize_state(cs);

    if (s390_store_adtl_status(cpu, addr, len)) {
        set_sigp_status(si, SIGP_STAT_INVALID_PARAMETER);
        return;
    }
    si->cc = SIGP_CC_ORDER_CODE_ACCEPTED;
}

static void sigp_restart(CPUState *cs, run_on_cpu_data arg)
{
    S390CPU *cpu = S390_CPU(cs);
    SigpInfo *si = arg.host_ptr;

    switch (s390_cpu_get_state(cpu)) {
    case CPU_STATE_STOPPED:
        /* the restart irq has to be delivered prior to any other pending irq */
        cpu_synchronize_state(cs);
        do_restart_interrupt(&cpu->env);
        s390_cpu_set_state(CPU_STATE_OPERATING, cpu);
        break;
    case CPU_STATE_OPERATING:
        cpu_inject_restart(cpu);
        break;
    }
    si->cc = SIGP_CC_ORDER_CODE_ACCEPTED;
}

static void sigp_initial_cpu_reset(CPUState *cs, run_on_cpu_data arg)
{
    S390CPU *cpu = S390_CPU(cs);
    S390CPUClass *scc = S390_CPU_GET_CLASS(cpu);
    SigpInfo *si = arg.host_ptr;

    cpu_synchronize_state(cs);
    scc->initial_cpu_reset(cs);
    cpu_synchronize_post_reset(cs);
    si->cc = SIGP_CC_ORDER_CODE_ACCEPTED;
}

static void sigp_cpu_reset(CPUState *cs, run_on_cpu_data arg)
{
    S390CPU *cpu = S390_CPU(cs);
    S390CPUClass *scc = S390_CPU_GET_CLASS(cpu);
    SigpInfo *si = arg.host_ptr;

    cpu_synchronize_state(cs);
    scc->cpu_reset(cs);
    cpu_synchronize_post_reset(cs);
    si->cc = SIGP_CC_ORDER_CODE_ACCEPTED;
}

static void sigp_set_prefix(CPUState *cs, run_on_cpu_data arg)
{
    S390CPU *cpu = S390_CPU(cs);
    SigpInfo *si = arg.host_ptr;
    uint32_t addr = si->param & 0x7fffe000u;

    cpu_synchronize_state(cs);

    if (!address_space_access_valid(&address_space_memory, addr,
                                    sizeof(struct LowCore), false)) {
        set_sigp_status(si, SIGP_STAT_INVALID_PARAMETER);
        return;
    }

    /* cpu has to be stopped */
    if (s390_cpu_get_state(cpu) != CPU_STATE_STOPPED) {
        set_sigp_status(si, SIGP_STAT_INCORRECT_STATE);
        return;
    }

    cpu->env.psa = addr;
    cpu_synchronize_post_init(cs);
    si->cc = SIGP_CC_ORDER_CODE_ACCEPTED;
}

static int handle_sigp_single_dst(S390CPU *dst_cpu, uint8_t order,
                                  uint64_t param, uint64_t *status_reg)
{
    SigpInfo si = {
        .param = param,
        .status_reg = status_reg,
    };

    /* cpu available? */
    if (dst_cpu == NULL) {
        return SIGP_CC_NOT_OPERATIONAL;
    }

    /* only resets can break pending orders */
    if (dst_cpu->env.sigp_order != 0 &&
        order != SIGP_CPU_RESET &&
        order != SIGP_INITIAL_CPU_RESET) {
        return SIGP_CC_BUSY;
    }

    switch (order) {
    case SIGP_START:
        run_on_cpu(CPU(dst_cpu), sigp_start, RUN_ON_CPU_HOST_PTR(&si));
        break;
    case SIGP_STOP:
        run_on_cpu(CPU(dst_cpu), sigp_stop, RUN_ON_CPU_HOST_PTR(&si));
        break;
    case SIGP_RESTART:
        run_on_cpu(CPU(dst_cpu), sigp_restart, RUN_ON_CPU_HOST_PTR(&si));
        break;
    case SIGP_STOP_STORE_STATUS:
        run_on_cpu(CPU(dst_cpu), sigp_stop_and_store_status, RUN_ON_CPU_HOST_PTR(&si));
        break;
    case SIGP_STORE_STATUS_ADDR:
        run_on_cpu(CPU(dst_cpu), sigp_store_status_at_address, RUN_ON_CPU_HOST_PTR(&si));
        break;
    case SIGP_STORE_ADTL_STATUS:
        run_on_cpu(CPU(dst_cpu), sigp_store_adtl_status, RUN_ON_CPU_HOST_PTR(&si));
        break;
    case SIGP_SET_PREFIX:
        run_on_cpu(CPU(dst_cpu), sigp_set_prefix, RUN_ON_CPU_HOST_PTR(&si));
        break;
    case SIGP_INITIAL_CPU_RESET:
        run_on_cpu(CPU(dst_cpu), sigp_initial_cpu_reset, RUN_ON_CPU_HOST_PTR(&si));
        break;
    case SIGP_CPU_RESET:
        run_on_cpu(CPU(dst_cpu), sigp_cpu_reset, RUN_ON_CPU_HOST_PTR(&si));
        break;
    default:
        set_sigp_status(&si, SIGP_STAT_INVALID_ORDER);
    }

    return si.cc;
}

static int sigp_set_architecture(S390CPU *cpu, uint32_t param,
                                 uint64_t *status_reg)
{
    CPUState *cur_cs;
    S390CPU *cur_cpu;
    bool all_stopped = true;

    CPU_FOREACH(cur_cs) {
        cur_cpu = S390_CPU(cur_cs);

        if (cur_cpu == cpu) {
            continue;
        }
        if (s390_cpu_get_state(cur_cpu) != CPU_STATE_STOPPED) {
            all_stopped = false;
        }
    }

    *status_reg &= 0xffffffff00000000ULL;

    /* Reject set arch order, with czam we're always in z/Arch mode. */
    *status_reg |= (all_stopped ? SIGP_STAT_INVALID_PARAMETER :
                    SIGP_STAT_INCORRECT_STATE);
    return SIGP_CC_STATUS_STORED;
}

int handle_sigp(CPUS390XState *env, uint8_t order, uint64_t r1, uint64_t r3)
{
    uint64_t *status_reg = &env->regs[r1];
    uint64_t param = (r1 % 2) ? env->regs[r1] : env->regs[r1 + 1];
    S390CPU *cpu = s390_env_get_cpu(env);
    S390CPU *dst_cpu = NULL;
    int ret;

    if (qemu_mutex_trylock(&qemu_sigp_mutex)) {
        ret = SIGP_CC_BUSY;
        goto out;
    }

    switch (order) {
    case SIGP_SET_ARCH:
        ret = sigp_set_architecture(cpu, param, status_reg);
        break;
    default:
        /* all other sigp orders target a single vcpu */
        dst_cpu = s390_cpu_addr2state(env->regs[r3]);
        ret = handle_sigp_single_dst(dst_cpu, order, param, status_reg);
    }
    qemu_mutex_unlock(&qemu_sigp_mutex);

out:
    trace_sigp_finished(order, CPU(cpu)->cpu_index,
                        dst_cpu ? CPU(dst_cpu)->cpu_index : -1, ret);
    g_assert(ret >= 0);

    return ret;
}

int s390_cpu_restart(S390CPU *cpu)
{
    SigpInfo si = {};

    if (tcg_enabled()) {
        /* FIXME TCG */
        return -ENOSYS;
    }

    run_on_cpu(CPU(cpu), sigp_restart, RUN_ON_CPU_HOST_PTR(&si));
    return 0;
}

void do_stop_interrupt(CPUS390XState *env)
{
    S390CPU *cpu = s390_env_get_cpu(env);

    if (s390_cpu_set_state(CPU_STATE_STOPPED, cpu) == 0) {
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
    }
    if (cpu->env.sigp_order == SIGP_STOP_STORE_STATUS) {
        s390_store_status(cpu, S390_STORE_STATUS_DEF_ADDR, true);
    }
    env->sigp_order = 0;
}

void s390_init_sigp(void)
{
    qemu_mutex_init(&qemu_sigp_mutex);
}
