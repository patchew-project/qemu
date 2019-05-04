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
#include "qemu/qemu-print.h"
#include "qapi/error.h"
#include "cpu.h"
#include "qemu-common.h"
#include "migration/vmstate.h"

static void avr_cpu_set_pc(CPUState *cs, vaddr value)
{
    AVRCPU *cpu = AVR_CPU(cs);

    cpu->env.pc_w = value / 2; /* internally PC points to words */
}

static bool avr_cpu_has_work(CPUState *cs)
{
    AVRCPU *cpu = AVR_CPU(cs);
    CPUAVRState *env = &cpu->env;

    return (cs->interrupt_request & (CPU_INTERRUPT_HARD | CPU_INTERRUPT_RESET))
            && cpu_interrupts_enabled(env);
}

static void avr_cpu_synchronize_from_tb(CPUState *cs, TranslationBlock *tb)
{
    AVRCPU *cpu = AVR_CPU(cs);
    CPUAVRState *env = &cpu->env;

    env->pc_w = tb->pc / 2; /* internally PC points to words */
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

    memset(env->r, 0, sizeof(env->r));

    tlb_flush(s);
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
    Error *local_err = NULL;

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }
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
        env->intsrc |= mask;
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

    cs->env_ptr = &cpu->env;

#ifndef CONFIG_USER_ONLY
    /* Set the number of interrupts supported by the CPU. */
    qdev_init_gpio_in(DEVICE(cpu), avr_cpu_set_int, 57);
#endif
}

static ObjectClass *avr_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;
    char *name;

    if (!cpu_model) {
        return NULL;
    }

    oc = object_class_by_name(cpu_model);
    if (oc != NULL && object_class_dynamic_cast(oc, TYPE_AVR_CPU) != NULL &&
        !object_class_is_abstract(oc)) {
        return oc;
    }

    name = g_strdup_printf(AVR_CPU_TYPE_NAME("%s"), cpu_model);
    oc = object_class_by_name(name);
    g_free(name);
    if (oc != NULL && object_class_dynamic_cast(oc, TYPE_AVR_CPU) != NULL &&
        !object_class_is_abstract(oc)) {
        return oc;
    }

    return NULL;
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
    cc->tcg_initialize = avr_cpu_tcg_init;
    cc->synchronize_from_tb = avr_cpu_synchronize_from_tb;
    cc->gdb_read_register = avr_cpu_gdb_read_register;
    cc->gdb_write_register = avr_cpu_gdb_write_register;
    cc->gdb_num_core_regs = 35;
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

typedef struct AVRCPUInfo {
    const char *name;
    void (*initfn)(Object *obj);
} AVRCPUInfo;

static gint avr_cpu_list_compare(gconstpointer a, gconstpointer b)
{
    ObjectClass *class_a = (ObjectClass *)a;
    ObjectClass *class_b = (ObjectClass *)b;
    const char *name_a;
    const char *name_b;

    name_a = object_class_get_name(class_a);
    name_b = object_class_get_name(class_b);

    return strcmp(name_a, name_b);
}

static void avr_cpu_list_entry(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    const char *typename = object_class_get_name(oc);
    size_t len = strlen(typename);
    size_t suffix_len = strlen(AVR_CPU_TYPE_SUFFIX);

    if (len > suffix_len) {
        qemu_printf("  %.*s\n", (int)(len - suffix_len), typename);
    } else {
        qemu_printf("  %s\n", typename);
    }
}

void avr_cpu_list(void)
{
    GSList *list;
    list = object_class_get_list(TYPE_AVR_CPU, false);
    list = g_slist_sort(list, avr_cpu_list_compare);
    qemu_printf("Available CPUs:\n");
    g_slist_foreach(list, avr_cpu_list_entry, NULL);
    g_slist_free(list);
}

#define DEFINE_AVR_CPU_TYPE(model, initfn) \
    {                                      \
        .parent = TYPE_AVR_CPU,            \
        .instance_init = initfn,           \
        .name = AVR_CPU_TYPE_NAME(model),  \
    }

static const TypeInfo avr_cpu_type_info[] = {
    {
        .name = TYPE_AVR_CPU,
        .parent = TYPE_CPU,
        .instance_size = sizeof(AVRCPU),
        .instance_init = avr_cpu_initfn,
        .class_size = sizeof(AVRCPUClass),
        .class_init = avr_cpu_class_init,
        .abstract = false,
    },
    DEFINE_AVR_CPU_TYPE("avr1", avr_avr1_initfn),
    DEFINE_AVR_CPU_TYPE("avr2", avr_avr2_initfn),
    DEFINE_AVR_CPU_TYPE("avr25", avr_avr25_initfn),
    DEFINE_AVR_CPU_TYPE("avr3", avr_avr3_initfn),
    DEFINE_AVR_CPU_TYPE("avr31", avr_avr31_initfn),
    DEFINE_AVR_CPU_TYPE("avr35", avr_avr35_initfn),
    DEFINE_AVR_CPU_TYPE("avr4", avr_avr4_initfn),
    DEFINE_AVR_CPU_TYPE("avr5", avr_avr5_initfn),
    DEFINE_AVR_CPU_TYPE("avr51", avr_avr51_initfn),
    DEFINE_AVR_CPU_TYPE("avr6", avr_avr6_initfn),
    DEFINE_AVR_CPU_TYPE("xmega2", avr_xmega2_initfn),
    DEFINE_AVR_CPU_TYPE("xmega4", avr_xmega4_initfn),
    DEFINE_AVR_CPU_TYPE("xmega5", avr_xmega5_initfn),
    DEFINE_AVR_CPU_TYPE("xmega6", avr_xmega6_initfn),
    DEFINE_AVR_CPU_TYPE("xmega7", avr_xmega7_initfn),
};

DEFINE_TYPES(avr_cpu_type_info)
