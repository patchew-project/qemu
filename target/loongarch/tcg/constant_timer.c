/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch constant timer support
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "cpu.h"
#include "internals.h"
#include "cpu-csr.h"

#define TIMER_PERIOD                10 /* 10 ns period for 100 MHz frequency */
#define CONSTANT_TIMER_TICK_MASK    0xfffffffffffcUL
#define CONSTANT_TIMER_ENABLE       0x1UL

uint64_t cpu_loongarch_get_constant_timer_counter(LoongArchCPU *cpu)
{
    return qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) / TIMER_PERIOD;
}

uint64_t cpu_loongarch_get_constant_timer_ticks(LoongArchCPU *cpu, bool guest)
{
    CPULoongArchState *env = &cpu->env;
    uint64_t now, expire;
    CPUSysState *sys = sys_state_if(env, guest);

    if (guest && env_vm_level(env) != LOONGARCH_VM_LEVEL_GUEST) {
        return sys->CSR_TVAL;
    }

    now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    expire = timer_expire_time_ns(guest ? &cpu->guest_timer : &cpu->timer);

    return (expire - now) / TIMER_PERIOD;
}

void cpu_loongarch_store_constant_timer_config(LoongArchCPU *cpu,
                                               uint64_t value, bool guest)
{
    CPULoongArchState *env = &cpu->env;
    uint64_t now, next;
    CPUSysState *sys = sys_state_if(env, guest);
    QEMUTimer *timer = guest ? &cpu->guest_timer : &cpu->timer;

    sys->CSR_TCFG = value;
    if (guest && env_vm_level(env) != LOONGARCH_VM_LEVEL_GUEST) {
        return;
    }

    if (value & CONSTANT_TIMER_ENABLE) {
        now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        next = now + (value & CONSTANT_TIMER_TICK_MASK) * TIMER_PERIOD;
        timer_mod(timer, next);
    } else {
        timer_del(timer);
    }
}

void cpu_loongarch_set_guest_timer(LoongArchCPU *cpu, bool on)
{
    CPULoongArchState *env = &cpu->env;
    CPUSysState *guest = &env->sys_states[LOONGARCH_VM_LEVEL_GUEST];
    uint64_t now, next, ticks;

    if (!(guest->CSR_TCFG & CONSTANT_TIMER_ENABLE)) {
        return;
    }

    if (on) {
        now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        ticks = guest->CSR_TVAL ? guest->CSR_TVAL :
                                  (guest->CSR_TCFG & CONSTANT_TIMER_TICK_MASK);
        guest->CSR_TVAL = 0;
        next = now + ticks * TIMER_PERIOD;
        timer_mod(&cpu->guest_timer, next);
    } else {
        guest->CSR_TVAL = cpu_loongarch_get_constant_timer_ticks(cpu, true);
        timer_del(&cpu->guest_timer);
    }
}

void loongarch_constant_timer_cb(void *opaque)
{
    LoongArchCPU *cpu  = opaque;
    CPULoongArchState *env = &cpu->env;
    CPUSysState *host = &env->sys_states[LOONGARCH_VM_LEVEL_HOST];
    uint64_t now, next;

    if (FIELD_EX64(host->CSR_TCFG, CSR_TCFG, PERIODIC)) {
        now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        next = now + (host->CSR_TCFG & CONSTANT_TIMER_TICK_MASK) *
                     TIMER_PERIOD;
        timer_mod(&cpu->timer, next);
    } else {
        host->CSR_TCFG = FIELD_DP64(host->CSR_TCFG, CSR_TCFG, EN, 0);
    }

    loongarch_cpu_set_irq(opaque, IRQ_TIMER, 1);
}

void loongarch_constant_timer_cb_guest(void *opaque)
{
    LoongArchCPU *cpu = opaque;
    CPULoongArchState *env = &cpu->env;
    CPUSysState *guest = &env->sys_states[LOONGARCH_VM_LEVEL_GUEST];
    uint64_t now, next;

    if (FIELD_EX64(guest->CSR_TCFG, CSR_TCFG, PERIODIC)) {
        now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        next = now + (guest->CSR_TCFG & CONSTANT_TIMER_TICK_MASK) *
                     TIMER_PERIOD;
        timer_mod(&cpu->guest_timer, next);
    } else {
        guest->CSR_TCFG = FIELD_DP64(guest->CSR_TCFG, CSR_TCFG, EN, 0);
    }

    loongarch_cpu_set_irq_guest(opaque, IRQ_TIMER, 1);
}
