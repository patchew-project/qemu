/*
 * QEMU MIPS timer support
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/mips/cpudevs.h"
#include "qemu/timer.h"
#include "sysemu/kvm.h"
#include "internal.h"

static uint32_t tick_to_count(MIPSCPU *cpu, uint64_t ticks)
{
    return ticks / cpu->cp0_count_rate;
}

static uint32_t tick_substract_to_count(MIPSCPU *cpu,
                                        uint32_t count, uint64_t ticks)
{
    return count - tick_to_count(cpu, ticks);
}

/* MIPS R4K timer */
static void cpu_mips_timer_update(CPUMIPSState *env)
{
    MIPSCPU *cpu = env_archcpu(env);
    uint64_t now_ns, next_ns;
    uint32_t wait;
    uint64_t now_ticks;

    now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    now_ticks = clock_ns_to_ticks(cpu->clock, now_ns);
    wait = tick_substract_to_count(cpu, env->CP0_Compare - env->CP0_Count,
                                   now_ticks);
    next_ns = now_ns + (uint64_t)wait * clock_ticks_to_ns(cpu->clock,
                                                          cpu->cp0_count_rate);
    timer_mod(env->timer, next_ns);
}

/* Expire the timer.  */
static void cpu_mips_timer_expire(CPUMIPSState *env)
{
    cpu_mips_timer_update(env);
    if (env->insn_flags & ISA_MIPS_R2) {
        env->CP0_Cause |= 1 << CP0Ca_TI;
    }
    qemu_irq_raise(env->irq[(env->CP0_IntCtl >> CP0IntCtl_IPTI) & 0x7]);
}

uint32_t cpu_mips_get_count(CPUMIPSState *env)
{
    if (env->CP0_Cause & (1 << CP0Ca_DC)) {
        return env->CP0_Count;
    } else {
        MIPSCPU *cpu = env_archcpu(env);
        uint64_t now_ns;

        now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        if (timer_pending(env->timer)
            && timer_expired(env->timer, now_ns)) {
            /* The timer has already expired.  */
            cpu_mips_timer_expire(env);
        }

        return env->CP0_Count + tick_to_count(cpu,
                                              clock_ns_to_ticks(cpu->clock,
                                                                now_ns));
    }
}

void cpu_mips_store_count(CPUMIPSState *env, uint32_t count)
{
    MIPSCPU *cpu = env_archcpu(env);

    /*
     * This gets called from cpu_state_reset(), potentially before timer init.
     * So env->timer may be NULL, which is also the case with KVM enabled so
     * treat timer as disabled in that case.
     */
    if (env->CP0_Cause & (1 << CP0Ca_DC) || !env->timer) {
        env->CP0_Count = count;
    } else {
        uint64_t cp0_count_ticks;

        cp0_count_ticks = clock_ns_to_ticks(cpu->clock,
                                            qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
        /* Store new count register */
        env->CP0_Count = tick_substract_to_count(cpu, count, cp0_count_ticks);

        /* Update timer timer */
        cpu_mips_timer_update(env);
    }
}

void cpu_mips_store_compare(CPUMIPSState *env, uint32_t value)
{
    env->CP0_Compare = value;
    if (!(env->CP0_Cause & (1 << CP0Ca_DC))) {
        cpu_mips_timer_update(env);
    }
    if (env->insn_flags & ISA_MIPS_R2) {
        env->CP0_Cause &= ~(1 << CP0Ca_TI);
    }
    qemu_irq_lower(env->irq[(env->CP0_IntCtl >> CP0IntCtl_IPTI) & 0x7]);
}

void cpu_mips_start_count(CPUMIPSState *env)
{
    cpu_mips_store_count(env, env->CP0_Count);
}

void cpu_mips_stop_count(CPUMIPSState *env)
{
    MIPSCPU *cpu = env_archcpu(env);
    uint64_t cp0_count_ticks;

    cp0_count_ticks = clock_ns_to_ticks(cpu->clock,
                                        qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
    /* Store the current value */
    env->CP0_Count += tick_to_count(cpu, cp0_count_ticks);
}

static void mips_timer_cb(void *opaque)
{
    CPUMIPSState *env;

    env = opaque;

    if (env->CP0_Cause & (1 << CP0Ca_DC)) {
        return;
    }

    /*
     * ??? This callback should occur when the counter is exactly equal to
     * the comparator value.  Offset the count by one to avoid immediately
     * retriggering the callback before any virtual time has passed.
     */
    env->CP0_Count++;
    cpu_mips_timer_expire(env);
    env->CP0_Count--;
}

void cpu_mips_clock_init(MIPSCPU *cpu)
{
    CPUMIPSState *env = &cpu->env;

    /*
     * If we're in KVM mode, don't create the periodic timer, that is handled in
     * kernel.
     */
    if (!kvm_enabled()) {
        env->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, &mips_timer_cb, env);
    }
}
