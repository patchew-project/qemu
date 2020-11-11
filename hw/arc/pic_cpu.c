/*
 * ARC Programmable Interrupt Controller support.
 *
 * Copyright (c) 2020 Synppsys Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * http://www.gnu.org/licenses/lgpl-2.1.html
 */


#include "qemu/osdep.h"
#include "cpu.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include "qemu/log.h"
#include "hw/arc/cpudevs.h"

/*
 * ARC pic handler
 */
static void arc_pic_cpu_handler(void *opaque, int irq, int level)
{
    ARCCPU *cpu = (ARCCPU *) opaque;
    CPUState *cs = CPU(cpu);
    CPUARCState *env = &cpu->env;
    int i;
    bool clear = false;
    uint32_t irq_bit;

    /* Assert if this handler is called in a system without interrupts. */
    assert(cpu->cfg.has_interrupts);

    /* Assert if the IRQ is not within the cpu configuration bounds. */
    assert(irq >= 16 && irq < (cpu->cfg.number_of_interrupts + 15));

    irq_bit = 1 << env->irq_bank[irq].priority;
    if (level) {
        /*
         * An interrupt is enabled, update irq_priority_pendig and rise
         * the qemu interrupt line.
         */
        env->irq_bank[irq].pending = 1;
        qatomic_or(&env->irq_priority_pending, irq_bit);
        cpu_interrupt(cs, CPU_INTERRUPT_HARD);
    } else {
        env->irq_bank[irq].pending = 0;

        /*
         * First, check if we still have any pending interrupt at the
         * given priority.
         */
        clear = true;
        for (i = 16; i < cpu->cfg.number_of_interrupts; i++) {
            if (env->irq_bank[i].pending
                && env->irq_bank[i].priority == env->irq_bank[irq].priority) {
                clear = false;
                break;
            }
        }

        /* If not, update (clear) irq_priority_pending. */
        if (clear) {
            qatomic_and(&env->irq_priority_pending, ~irq_bit);
        }

        /*
         * If we don't have any pending priority, lower the qemu irq
         * line. N.B. we can also check more here like IE bit, but we
         * need to add a cpu_interrupt call when we enable the
         * interrupts (e.g., sleep, seti).
         */
        if (!env->irq_priority_pending) {
            cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
        }
    }
    qemu_log_mask(CPU_LOG_INT,
                  "[IRQ] level = %d, clear = %d, irq = %d, priority = %d, "
                  "pending = %08x, pc = %08x\n",
                  level, clear, irq, env->irq_bank[irq].priority,
                  env->irq_priority_pending, env->pc);
}

/*
 * ARC PIC initialization helper
 */
void cpu_arc_pic_init(ARCCPU *cpu)
{
    CPUARCState *env = &cpu->env;
    int i;
    qemu_irq *qi;

    qi = qemu_allocate_irqs(arc_pic_cpu_handler, cpu,
                            16 + cpu->cfg.number_of_interrupts);

    for (i = 0; i < cpu->cfg.number_of_interrupts; i++) {
        env->irq[16 + i] = qi[16 + i];
    }
}


/*-*-indent-tabs-mode:nil;tab-width:4;indent-line-function:'insert-tab'-*-*/
/* vim: set ts=4 sw=4 et: */
