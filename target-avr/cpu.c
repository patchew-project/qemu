/*
 * QEMU AVR CPU
 *
 * Copyright (c) 2016 Michael Rolnik
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
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "qemu-common.h"
#include "migration/vmstate.h"
#include "machine.h"

static void avr_cpu_set_pc(CPUState *cs, vaddr value)
{
    AVRCPU   *cpu = AVR_CPU(cs);

    cpu->env.pc_w = value / 2;    /*  internally PC points to words   */
}

static bool avr_cpu_has_work(CPUState *cs)
{
    AVRCPU *cpu = AVR_CPU(cs);
    CPUAVRState *env = &cpu->env;

    return      (cs->interrupt_request
                &   (CPU_INTERRUPT_HARD
                    | CPU_INTERRUPT_RESET))
            &&  cpu_interrupts_enabled(env);
}

static void avr_cpu_synchronize_from_tb(CPUState *cs, TranslationBlock *tb)
{
    AVRCPU      *cpu = AVR_CPU(cs);
    CPUAVRState *env = &cpu->env;

    env->pc_w = tb->pc / 2;   /*  internally PC points to words   */
}

static void avr_cpu_reset(CPUState *s)
{
    AVRCPU *cpu = AVR_CPU(s);
    AVRCPUClass *mcc = AVR_CPU_GET_CLASS(cpu);
    CPUAVRState *env = &cpu->env;

    mcc->parent_reset(s);

    env->pc_w = 0;
    env->sregI = 1;
    env->sregC = 0;
    env->sregZ = 0;
    env->sregN = 0;
    env->sregV = 0;
    env->sregS = 0;
    env->sregH = 0;
    env->sregT = 0;

    env->rampD = 0;
    env->rampX = 0;
    env->rampY = 0;
    env->rampZ = 0;
    env->eind = 0;
    env->sp = 0;

    memset(env->io, 0, sizeof(env->io));
    memset(env->r, 0, sizeof(env->r));

    tlb_flush(s, 1);
}

static void avr_cpu_disas_set_info(CPUState *cpu, disassemble_info *info)
{
    info->mach = bfd_arch_avr;
    info->print_insn = NULL;
}

static void avr_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    AVRCPUClass *mcc = AVR_CPU_GET_CLASS(dev);

    qemu_init_vcpu(cs);
    cpu_reset(cs);

    mcc->parent_realize(dev, errp);
}

static void avr_cpu_set_int(void *opaque, int irq, int level)
{
    AVRCPU *cpu = opaque;
    CPUAVRState *env = &cpu->env;
    CPUState *cs = CPU(cpu);

    uint64_t mask = (1ull << irq);
    if (level) {
        env->intsrc |=  mask;
        cpu_interrupt(cs, CPU_INTERRUPT_HARD);
    } else {
        env->intsrc &= ~mask;
        if (env->intsrc == 0) {
            cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
        }
    }
}

static void avr_cpu_initfn(Object *obj)
{
    CPUState *cs = CPU(obj);
    AVRCPU *cpu = AVR_CPU(obj);
    static int inited;

    cs->env_ptr = &cpu->env;
    cpu_exec_init(cs, &error_abort);

#ifndef CONFIG_USER_ONLY
    qdev_init_gpio_in(DEVICE(cpu), avr_cpu_set_int, 37);
#endif

    if (tcg_enabled() && !inited) {
        inited = 1;
        avr_translate_init();
    }
}

static ObjectClass *avr_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;
    char *typename;
    char **cpuname;

    if (!cpu_model) {
        return NULL;
    }

    cpuname = g_strsplit(cpu_model, ",", 1);
    typename = g_strdup_printf("%s-" TYPE_AVR_CPU, cpuname[0]);
    oc = object_class_by_name(typename);

    g_strfreev(cpuname);
    g_free(typename);

    if (!oc
        ||  !object_class_dynamic_cast(oc, TYPE_AVR_CPU)
        ||  object_class_is_abstract(oc)) {
        return NULL;
    }

    return oc;
}

static void avr_cpu_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    AVRCPUClass *mcc = AVR_CPU_CLASS(oc);

    mcc->parent_realize = dc->realize;
    dc->realize = avr_cpu_realizefn;

    mcc->parent_reset = cc->reset;
    cc->reset = avr_cpu_reset;

    cc->class_by_name = avr_cpu_class_by_name;

    cc->has_work = avr_cpu_has_work;
    cc->do_interrupt = avr_cpu_do_interrupt;
    cc->cpu_exec_interrupt = avr_cpu_exec_interrupt;
    cc->dump_state = avr_cpu_dump_state;
    cc->set_pc = avr_cpu_set_pc;
#if !defined(CONFIG_USER_ONLY)
    cc->memory_rw_debug = avr_cpu_memory_rw_debug;
#endif
#ifdef CONFIG_USER_ONLY
    cc->handle_mmu_fault = avr_cpu_handle_mmu_fault;
#else
    cc->get_phys_page_debug = avr_cpu_get_phys_page_debug;
    cc->vmsd = &vms_avr_cpu;
#endif
    cc->disas_set_info = avr_cpu_disas_set_info;
    cc->synchronize_from_tb = avr_cpu_synchronize_from_tb;
    cc->gdb_read_register = avr_cpu_gdb_read_register;
    cc->gdb_write_register = avr_cpu_gdb_write_register;
    cc->gdb_num_core_regs = 35;

    /*
     * Reason: avr_cpu_initfn() calls cpu_exec_init(), which saves
     * the object in cpus -> dangling pointer after final
     * object_unref().
     */
    dc->cannot_destroy_with_object_finalize_yet = true;
}

static void avr_avr1_initfn(Object *obj)
{
    AVRCPU *cpu = AVR_CPU(obj);
    CPUAVRState *env = &cpu->env;

    avr_set_feature(env, AVR_FEATURE_LPM);
    avr_set_feature(env, AVR_FEATURE_2_BYTE_SP);
    avr_set_feature(env, AVR_FEATURE_2_BYTE_PC);
}

static void avr_avr2_initfn(Object *obj)
{
    AVRCPU *cpu = AVR_CPU(obj);
    CPUAVRState *env = &cpu->env;

    avr_set_feature(env, AVR_FEATURE_LPM);
    avr_set_feature(env, AVR_FEATURE_IJMP_ICALL);
    avr_set_feature(env, AVR_FEATURE_ADIW_SBIW);
    avr_set_feature(env, AVR_FEATURE_SRAM);
    avr_set_feature(env, AVR_FEATURE_BREAK);

    avr_set_feature(env, AVR_FEATURE_2_BYTE_PC);
    avr_set_feature(env, AVR_FEATURE_2_BYTE_SP);
}

static void avr_avr25_initfn(Object *obj)
{
    AVRCPU *cpu = AVR_CPU(obj);
    CPUAVRState *env = &cpu->env;

    avr_set_feature(env, AVR_FEATURE_LPM);
    avr_set_feature(env, AVR_FEATURE_IJMP_ICALL);
    avr_set_feature(env, AVR_FEATURE_ADIW_SBIW);
    avr_set_feature(env, AVR_FEATURE_SRAM);
    avr_set_feature(env, AVR_FEATURE_BREAK);

    avr_set_feature(env, AVR_FEATURE_2_BYTE_PC);
    avr_set_feature(env, AVR_FEATURE_2_BYTE_SP);
    avr_set_feature(env, AVR_FEATURE_LPMX);
    avr_set_feature(env, AVR_FEATURE_MOVW);
}

static void avr_avr3_initfn(Object *obj)
{
    AVRCPU *cpu = AVR_CPU(obj);
    CPUAVRState *env = &cpu->env;

    avr_set_feature(env, AVR_FEATURE_LPM);
    avr_set_feature(env, AVR_FEATURE_IJMP_ICALL);
    avr_set_feature(env, AVR_FEATURE_ADIW_SBIW);
    avr_set_feature(env, AVR_FEATURE_SRAM);
    avr_set_feature(env, AVR_FEATURE_BREAK);

    avr_set_feature(env, AVR_FEATURE_2_BYTE_PC);
    avr_set_feature(env, AVR_FEATURE_2_BYTE_SP);
    avr_set_feature(env, AVR_FEATURE_JMP_CALL);
}

static void avr_avr31_initfn(Object *obj)
{
    AVRCPU *cpu = AVR_CPU(obj);
    CPUAVRState *env = &cpu->env;

    avr_set_feature(env, AVR_FEATURE_LPM);
    avr_set_feature(env, AVR_FEATURE_IJMP_ICALL);
    avr_set_feature(env, AVR_FEATURE_ADIW_SBIW);
    avr_set_feature(env, AVR_FEATURE_SRAM);
    avr_set_feature(env, AVR_FEATURE_BREAK);

    avr_set_feature(env, AVR_FEATURE_2_BYTE_PC);
    avr_set_feature(env, AVR_FEATURE_2_BYTE_SP);
    avr_set_feature(env, AVR_FEATURE_RAMPZ);
    avr_set_feature(env, AVR_FEATURE_ELPM);
    avr_set_feature(env, AVR_FEATURE_JMP_CALL);
}

static void avr_avr35_initfn(Object *obj)
{
    AVRCPU *cpu = AVR_CPU(obj);
    CPUAVRState *env = &cpu->env;

    avr_set_feature(env, AVR_FEATURE_LPM);
    avr_set_feature(env, AVR_FEATURE_IJMP_ICALL);
    avr_set_feature(env, AVR_FEATURE_ADIW_SBIW);
    avr_set_feature(env, AVR_FEATURE_SRAM);
    avr_set_feature(env, AVR_FEATURE_BREAK);

    avr_set_feature(env, AVR_FEATURE_2_BYTE_PC);
    avr_set_feature(env, AVR_FEATURE_2_BYTE_SP);
    avr_set_feature(env, AVR_FEATURE_JMP_CALL);
    avr_set_feature(env, AVR_FEATURE_LPMX);
    avr_set_feature(env, AVR_FEATURE_MOVW);
}

static void avr_avr4_initfn(Object *obj)
{
    AVRCPU *cpu = AVR_CPU(obj);
    CPUAVRState *env = &cpu->env;

    avr_set_feature(env, AVR_FEATURE_LPM);
    avr_set_feature(env, AVR_FEATURE_IJMP_ICALL);
    avr_set_feature(env, AVR_FEATURE_ADIW_SBIW);
    avr_set_feature(env, AVR_FEATURE_SRAM);
    avr_set_feature(env, AVR_FEATURE_BREAK);

    avr_set_feature(env, AVR_FEATURE_2_BYTE_PC);
    avr_set_feature(env, AVR_FEATURE_2_BYTE_SP);
    avr_set_feature(env, AVR_FEATURE_LPMX);
    avr_set_feature(env, AVR_FEATURE_MOVW);
    avr_set_feature(env, AVR_FEATURE_MUL);
}

static void avr_avr5_initfn(Object *obj)
{
    AVRCPU *cpu = AVR_CPU(obj);
    CPUAVRState *env = &cpu->env;

    avr_set_feature(env, AVR_FEATURE_LPM);
    avr_set_feature(env, AVR_FEATURE_IJMP_ICALL);
    avr_set_feature(env, AVR_FEATURE_ADIW_SBIW);
    avr_set_feature(env, AVR_FEATURE_SRAM);
    avr_set_feature(env, AVR_FEATURE_BREAK);

    avr_set_feature(env, AVR_FEATURE_2_BYTE_PC);
    avr_set_feature(env, AVR_FEATURE_2_BYTE_SP);
    avr_set_feature(env, AVR_FEATURE_JMP_CALL);
    avr_set_feature(env, AVR_FEATURE_LPMX);
    avr_set_feature(env, AVR_FEATURE_MOVW);
    avr_set_feature(env, AVR_FEATURE_MUL);
}

static void avr_avr51_initfn(Object *obj)
{
    AVRCPU *cpu = AVR_CPU(obj);
    CPUAVRState *env = &cpu->env;

    avr_set_feature(env, AVR_FEATURE_LPM);
    avr_set_feature(env, AVR_FEATURE_IJMP_ICALL);
    avr_set_feature(env, AVR_FEATURE_ADIW_SBIW);
    avr_set_feature(env, AVR_FEATURE_SRAM);
    avr_set_feature(env, AVR_FEATURE_BREAK);

    avr_set_feature(env, AVR_FEATURE_2_BYTE_PC);
    avr_set_feature(env, AVR_FEATURE_2_BYTE_SP);
    avr_set_feature(env, AVR_FEATURE_RAMPZ);
    avr_set_feature(env, AVR_FEATURE_ELPMX);
    avr_set_feature(env, AVR_FEATURE_ELPM);
    avr_set_feature(env, AVR_FEATURE_JMP_CALL);
    avr_set_feature(env, AVR_FEATURE_LPMX);
    avr_set_feature(env, AVR_FEATURE_MOVW);
    avr_set_feature(env, AVR_FEATURE_MUL);
}

static void avr_avr6_initfn(Object *obj)
{
    AVRCPU *cpu = AVR_CPU(obj);
    CPUAVRState *env = &cpu->env;

    avr_set_feature(env, AVR_FEATURE_LPM);
    avr_set_feature(env, AVR_FEATURE_IJMP_ICALL);
    avr_set_feature(env, AVR_FEATURE_ADIW_SBIW);
    avr_set_feature(env, AVR_FEATURE_SRAM);
    avr_set_feature(env, AVR_FEATURE_BREAK);

    avr_set_feature(env, AVR_FEATURE_3_BYTE_PC);
    avr_set_feature(env, AVR_FEATURE_2_BYTE_SP);
    avr_set_feature(env, AVR_FEATURE_RAMPZ);
    avr_set_feature(env, AVR_FEATURE_EIJMP_EICALL);
    avr_set_feature(env, AVR_FEATURE_ELPMX);
    avr_set_feature(env, AVR_FEATURE_ELPM);
    avr_set_feature(env, AVR_FEATURE_JMP_CALL);
    avr_set_feature(env, AVR_FEATURE_LPMX);
    avr_set_feature(env, AVR_FEATURE_MOVW);
    avr_set_feature(env, AVR_FEATURE_MUL);
}

static void avr_xmega2_initfn(Object *obj)
{
    AVRCPU *cpu = AVR_CPU(obj);
    CPUAVRState *env = &cpu->env;

    avr_set_feature(env, AVR_FEATURE_LPM);
    avr_set_feature(env, AVR_FEATURE_IJMP_ICALL);
    avr_set_feature(env, AVR_FEATURE_ADIW_SBIW);
    avr_set_feature(env, AVR_FEATURE_SRAM);
    avr_set_feature(env, AVR_FEATURE_BREAK);

    avr_set_feature(env, AVR_FEATURE_2_BYTE_PC);
    avr_set_feature(env, AVR_FEATURE_2_BYTE_SP);
    avr_set_feature(env, AVR_FEATURE_JMP_CALL);
    avr_set_feature(env, AVR_FEATURE_LPMX);
    avr_set_feature(env, AVR_FEATURE_MOVW);
    avr_set_feature(env, AVR_FEATURE_MUL);
    avr_set_feature(env, AVR_FEATURE_RMW);
}

static void avr_xmega4_initfn(Object *obj)
{
    AVRCPU *cpu = AVR_CPU(obj);
    CPUAVRState *env = &cpu->env;

    avr_set_feature(env, AVR_FEATURE_LPM);
    avr_set_feature(env, AVR_FEATURE_IJMP_ICALL);
    avr_set_feature(env, AVR_FEATURE_ADIW_SBIW);
    avr_set_feature(env, AVR_FEATURE_SRAM);
    avr_set_feature(env, AVR_FEATURE_BREAK);

    avr_set_feature(env, AVR_FEATURE_2_BYTE_PC);
    avr_set_feature(env, AVR_FEATURE_2_BYTE_SP);
    avr_set_feature(env, AVR_FEATURE_RAMPZ);
    avr_set_feature(env, AVR_FEATURE_ELPMX);
    avr_set_feature(env, AVR_FEATURE_ELPM);
    avr_set_feature(env, AVR_FEATURE_JMP_CALL);
    avr_set_feature(env, AVR_FEATURE_LPMX);
    avr_set_feature(env, AVR_FEATURE_MOVW);
    avr_set_feature(env, AVR_FEATURE_MUL);
    avr_set_feature(env, AVR_FEATURE_RMW);
}

static void avr_xmega5_initfn(Object *obj)
{
    AVRCPU *cpu = AVR_CPU(obj);
    CPUAVRState *env = &cpu->env;

    avr_set_feature(env, AVR_FEATURE_LPM);
    avr_set_feature(env, AVR_FEATURE_IJMP_ICALL);
    avr_set_feature(env, AVR_FEATURE_ADIW_SBIW);
    avr_set_feature(env, AVR_FEATURE_SRAM);
    avr_set_feature(env, AVR_FEATURE_BREAK);

    avr_set_feature(env, AVR_FEATURE_2_BYTE_PC);
    avr_set_feature(env, AVR_FEATURE_2_BYTE_SP);
    avr_set_feature(env, AVR_FEATURE_RAMPD);
    avr_set_feature(env, AVR_FEATURE_RAMPX);
    avr_set_feature(env, AVR_FEATURE_RAMPY);
    avr_set_feature(env, AVR_FEATURE_RAMPZ);
    avr_set_feature(env, AVR_FEATURE_ELPMX);
    avr_set_feature(env, AVR_FEATURE_ELPM);
    avr_set_feature(env, AVR_FEATURE_JMP_CALL);
    avr_set_feature(env, AVR_FEATURE_LPMX);
    avr_set_feature(env, AVR_FEATURE_MOVW);
    avr_set_feature(env, AVR_FEATURE_MUL);
    avr_set_feature(env, AVR_FEATURE_RMW);
}

static void avr_xmega6_initfn(Object *obj)
{
    AVRCPU *cpu = AVR_CPU(obj);
    CPUAVRState *env = &cpu->env;

    avr_set_feature(env, AVR_FEATURE_LPM);
    avr_set_feature(env, AVR_FEATURE_IJMP_ICALL);
    avr_set_feature(env, AVR_FEATURE_ADIW_SBIW);
    avr_set_feature(env, AVR_FEATURE_SRAM);
    avr_set_feature(env, AVR_FEATURE_BREAK);

    avr_set_feature(env, AVR_FEATURE_3_BYTE_PC);
    avr_set_feature(env, AVR_FEATURE_2_BYTE_SP);
    avr_set_feature(env, AVR_FEATURE_RAMPZ);
    avr_set_feature(env, AVR_FEATURE_EIJMP_EICALL);
    avr_set_feature(env, AVR_FEATURE_ELPMX);
    avr_set_feature(env, AVR_FEATURE_ELPM);
    avr_set_feature(env, AVR_FEATURE_JMP_CALL);
    avr_set_feature(env, AVR_FEATURE_LPMX);
    avr_set_feature(env, AVR_FEATURE_MOVW);
    avr_set_feature(env, AVR_FEATURE_MUL);
    avr_set_feature(env, AVR_FEATURE_RMW);
}

static void avr_xmega7_initfn(Object *obj)
{
    AVRCPU *cpu = AVR_CPU(obj);
    CPUAVRState *env = &cpu->env;

    avr_set_feature(env, AVR_FEATURE_LPM);
    avr_set_feature(env, AVR_FEATURE_IJMP_ICALL);
    avr_set_feature(env, AVR_FEATURE_ADIW_SBIW);
    avr_set_feature(env, AVR_FEATURE_SRAM);
    avr_set_feature(env, AVR_FEATURE_BREAK);

    avr_set_feature(env, AVR_FEATURE_3_BYTE_PC);
    avr_set_feature(env, AVR_FEATURE_2_BYTE_SP);
    avr_set_feature(env, AVR_FEATURE_RAMPD);
    avr_set_feature(env, AVR_FEATURE_RAMPX);
    avr_set_feature(env, AVR_FEATURE_RAMPY);
    avr_set_feature(env, AVR_FEATURE_RAMPZ);
    avr_set_feature(env, AVR_FEATURE_EIJMP_EICALL);
    avr_set_feature(env, AVR_FEATURE_ELPMX);
    avr_set_feature(env, AVR_FEATURE_ELPM);
    avr_set_feature(env, AVR_FEATURE_JMP_CALL);
    avr_set_feature(env, AVR_FEATURE_LPMX);
    avr_set_feature(env, AVR_FEATURE_MOVW);
    avr_set_feature(env, AVR_FEATURE_MUL);
    avr_set_feature(env, AVR_FEATURE_RMW);
}

static void avr_any_initfn(Object *obj)
{
    /* Set cpu feature flags */
}

typedef struct AVRCPUInfo {
    const char     *name;
    void (*initfn)(Object *obj);
} AVRCPUInfo;

static const AVRCPUInfo avr_cpus[] = {
    {.name = "avr1", .initfn = avr_avr1_initfn},
    {.name = "avr2", .initfn = avr_avr2_initfn},
    {.name = "avr25", .initfn = avr_avr25_initfn},
    {.name = "avr3", .initfn = avr_avr3_initfn},
    {.name = "avr31", .initfn = avr_avr31_initfn},
    {.name = "avr35", .initfn = avr_avr35_initfn},
    {.name = "avr4", .initfn = avr_avr4_initfn},
    {.name = "avr5", .initfn = avr_avr5_initfn},
    {.name = "avr51", .initfn = avr_avr51_initfn},
    {.name = "avr6", .initfn = avr_avr6_initfn},
    {.name = "xmega2", .initfn = avr_xmega2_initfn},
    {.name = "xmega4", .initfn = avr_xmega4_initfn},
    {.name = "xmega5", .initfn = avr_xmega5_initfn},
    {.name = "xmega6", .initfn = avr_xmega6_initfn},
    {.name = "xmega7", .initfn = avr_xmega7_initfn},
    {.name = "any", .initfn = avr_any_initfn },
};

static gint avr_cpu_list_compare(gconstpointer a, gconstpointer b)
{
    ObjectClass *class_a = (ObjectClass *)a;
    ObjectClass *class_b = (ObjectClass *)b;
    const char *name_a;
    const char *name_b;

    name_a = object_class_get_name(class_a);
    name_b = object_class_get_name(class_b);
    if (strcmp(name_a, "any-" TYPE_AVR_CPU) == 0) {
        return 1;
    } else if (strcmp(name_b, "any-" TYPE_AVR_CPU) == 0) {
        return -1;
    } else {
        return strcmp(name_a, name_b);
    }
}

static void avr_cpu_list_entry(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    CPUListState *s = user_data;
    const char *typename;
    char *name;

    typename = object_class_get_name(oc);
    name = g_strndup(typename, strlen(typename) - strlen("-" TYPE_AVR_CPU));
    (*s->cpu_fprintf)(s->file, "  %s\n", name);
    g_free(name);
}

void avr_cpu_list(FILE *f, fprintf_function cpu_fprintf)
{
    CPUListState s = {
        .file = f,
        .cpu_fprintf = cpu_fprintf,
    };
    GSList *list;

    list = object_class_get_list(TYPE_AVR_CPU, false);
    list = g_slist_sort(list, avr_cpu_list_compare);
    (*cpu_fprintf)(f, "Available CPUs:\n");
    g_slist_foreach(list, avr_cpu_list_entry, &s);
    g_slist_free(list);
}

AVRCPU *cpu_avr_init(const char *cpu_model)
{
    return AVR_CPU(cpu_generic_init(TYPE_AVR_CPU, cpu_model));
}

static void cpu_register(const AVRCPUInfo *info)
{
    TypeInfo type_info = {
        .parent = TYPE_AVR_CPU,
        .instance_size = sizeof(AVRCPU),
        .instance_init = info->initfn,
        .class_size = sizeof(AVRCPUClass),
    };

    type_info.name = g_strdup_printf("%s-" TYPE_AVR_CPU, info->name);
    type_register(&type_info);
    g_free((void *)type_info.name);
}

static const TypeInfo avr_cpu_type_info = {
    .name = TYPE_AVR_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(AVRCPU),
    .instance_init = avr_cpu_initfn,
    .class_size = sizeof(AVRCPUClass),
    .class_init = avr_cpu_class_init,
    .abstract = true,
};

static void avr_cpu_register_types(void)
{
    int i;
    type_register_static(&avr_cpu_type_info);

    for (i = 0; i < ARRAY_SIZE(avr_cpus); i++) {
        cpu_register(&avr_cpus[i]);
    }
}

type_init(avr_cpu_register_types)

