/*
 * i386 TCG cpu class initialization
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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
#include "qemu/units.h"
#include "cpu.h"
#include "tcg-cpu-type.h"

#include "helper-tcg.h"
#include "sysemu/sysemu.h"

#ifndef CONFIG_USER_ONLY
#include "exec/address-spaces.h"
#endif

/* Frob eflags into and out of the CPU temporary format.  */

static void x86_cpu_exec_enter(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    CC_SRC = env->eflags & (CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
    env->df = 1 - (2 * ((env->eflags >> 10) & 1));
    CC_OP = CC_OP_EFLAGS;
    env->eflags &= ~(DF_MASK | CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
}

static void x86_cpu_exec_exit(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    env->eflags = cpu_compute_eflags(env);
}

static void x86_cpu_synchronize_from_tb(CPUState *cs, TranslationBlock *tb)
{
    X86CPU *cpu = X86_CPU(cs);

    cpu->env.eip = tb->pc - tb->cs_base;
}

#ifndef CONFIG_USER_ONLY

static void x86_cpu_machine_done(Notifier *n, void *unused)
{
    X86CPU *cpu = container_of(n, X86CPU, machine_done);
    MemoryRegion *smram =
        (MemoryRegion *) object_resolve_path("/machine/smram", NULL);

    if (smram) {
        cpu->smram = g_new(MemoryRegion, 1);
        memory_region_init_alias(cpu->smram, OBJECT(cpu), "smram",
                                 smram, 0, 4 * GiB);
        memory_region_set_enabled(cpu->smram, true);
        memory_region_add_subregion_overlap(cpu->cpu_as_root, 0,
                                            cpu->smram, 1);
    }
}

static void tcg_cpu_realizefn(DeviceState *dev, Error **errp)
{
    X86CPU *cpu = X86_CPU(dev);
    CPUState *cs = CPU(dev);

    /*
     * also for TCG the realize order is important,
     * as the memory regions initialized here are needed
     * in x86_cpu_realizefn()
     */
    cpu->cpu_as_mem = g_new(MemoryRegion, 1);
    cpu->cpu_as_root = g_new(MemoryRegion, 1);

    /* Outer container... */
    memory_region_init(cpu->cpu_as_root, OBJECT(cpu), "memory", ~0ull);
    memory_region_set_enabled(cpu->cpu_as_root, true);

    /*
     * ... with two regions inside: normal system memory with low
     * priority, and...
     */
    memory_region_init_alias(cpu->cpu_as_mem, OBJECT(cpu), "memory",
                             get_system_memory(), 0, ~0ull);
    memory_region_add_subregion_overlap(cpu->cpu_as_root, 0, cpu->cpu_as_mem, 0);
    memory_region_set_enabled(cpu->cpu_as_mem, true);

    cs->num_ases = 2;
    cpu_address_space_init(cs, 0, "cpu-memory", cs->memory);
    cpu_address_space_init(cs, 1, "cpu-smm", cpu->cpu_as_root);

    /* ... SMRAM with higher priority, linked from /machine/smram.  */
    cpu->machine_done.notify = x86_cpu_machine_done;
    qemu_add_machine_init_done_notifier(&cpu->machine_done);

    /* call the generic x86_cpu_realizefn() after regions initialized */
    x86_cpu_realizefn(dev, errp);
}
#endif /* !CONFIG_USER_ONLY */

static void tcg_cpu_common_class_init(ObjectClass *oc, void *data)
{
    CPUClass *cc = CPU_CLASS(oc);
#ifndef CONFIG_USER_ONLY
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = tcg_cpu_realizefn;
#endif /* !CONFIG_USER_ONLY */

    cc->do_interrupt = x86_cpu_do_interrupt;
    cc->cpu_exec_interrupt = x86_cpu_exec_interrupt;
    cc->synchronize_from_tb = x86_cpu_synchronize_from_tb;
    cc->cpu_exec_enter = x86_cpu_exec_enter;
    cc->cpu_exec_exit = x86_cpu_exec_exit;
    cc->tcg_initialize = tcg_x86_init;
    cc->tlb_fill = x86_cpu_tlb_fill;
#ifndef CONFIG_USER_ONLY
    cc->debug_excp_handler = breakpoint_handler;
#endif /* !CONFIG_USER_ONLY */
}

/*
 * TCG-specific defaults that override all CPU models when using TCG
 */
static PropValue tcg_default_props[] = {
    { "vme", "off" },
    { NULL, NULL },
};

static void tcg_cpu_initfn(Object *obj)
{
    X86CPU *cpu = X86_CPU(obj);

    /* Special cases not set in the X86CPUDefinition structs: */
    x86_cpu_apply_props(cpu, tcg_default_props);
}

static const TypeInfo tcg_cpu_type_info = {
    .name = X86_CPU_TYPE_NAME("tcg"),
    .parent = TYPE_X86_CPU,

    .instance_init = tcg_cpu_initfn,
    .class_init = tcg_cpu_common_class_init,
};

static void tcg_cpu_register_base_type(void)
{
    type_register_static(&tcg_cpu_type_info);
}

type_init(tcg_cpu_register_base_type);

void tcg_cpu_type_init(void)
{
    if (tcg_enabled()) {
        x86_cpu_register_cpu_models(X86_CPU_TYPE_NAME("tcg"));
    }
}

accel_cpu_init(tcg_cpu_type_init);
