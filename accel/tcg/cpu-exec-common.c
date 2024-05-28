/*
 *  emulator main execution loop
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "sysemu/cpus.h"
#include "sysemu/tcg.h"
#include "qemu/plugin.h"
#include "internal-common.h"

bool tcg_allowed;

/* exit the current TB, but without causing any exception to be raised */
void cpu_loop_exit_noexc(CPUState *cpu)
{
    cpu->exception_index = -1;
    cpu_loop_exit(cpu);
}

void cpu_loop_exit(CPUState *cpu)
{
    /* Undo the setting in cpu_tb_exec.  */
    cpu->neg.can_do_io = true;
    /* Undo any setting in generated code.  */
    qemu_plugin_disable_mem_helpers(cpu);
    siglongjmp(cpu->jmp_env, 1);
}

void cpu_loop_exit_restore(CPUState *cpu, uintptr_t pc)
{
    if (pc) {
        cpu_restore_state(cpu, pc);
    }
    cpu_loop_exit(cpu);
}

void cpu_loop_exit_atomic(CPUState *cpu, uintptr_t pc)
{
    /* Prevent looping if already executing in a serial context. */
    g_assert(!cpu_in_serial_context(cpu));
    cpu->exception_index = EXCP_ATOMIC;
    cpu_loop_exit_restore(cpu, pc);
}

#ifdef CONFIG_PLUGIN
static void qemu_plugin_vcpu_init__async(CPUState *cpu, run_on_cpu_data unused)
{
    qemu_plugin_vcpu_init_hook(cpu);
}
#endif

bool tcg_exec_realize_assigned(CPUState *cpu, Error **errp)
{
#ifdef CONFIG_PLUGIN
    cpu->plugin_state = qemu_plugin_create_vcpu_state();
    /* Plugin initialization must wait until the cpu start executing code */
    async_run_on_cpu(cpu, qemu_plugin_vcpu_init__async, RUN_ON_CPU_NULL);
#endif

    return true;
}

/* undo the initializations in reverse order */
void tcg_exec_unrealize_assigned(CPUState *cpu)
{
#ifdef CONFIG_PLUGIN
    /* Call the plugin hook before clearing the cpu is fully unrealized */
    qemu_plugin_vcpu_exit_hook(cpu);
#endif
}
