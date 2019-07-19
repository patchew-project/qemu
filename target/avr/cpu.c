/*
 * QEMU AVR CPU
 *
 * Copyright (c) 2019 Michael Rolnik
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
#include "qemu/log.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
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

static void avr_cpu_reset(CPUState *cs)
{
    AVRCPU *cpu = AVR_CPU(cs);
    AVRCPUClass *mcc = AVR_CPU_GET_CLASS(cpu);
    CPUAVRState *env = &cpu->env;

    mcc->parent_reset(cs);

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

    env->skip = 0;

    memset(env->r, 0, sizeof(env->r));

    tlb_flush(cs);
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
    AVRCPU *cpu = AVR_CPU(obj);

    cpu_set_cpustate_pointers(cpu);

#ifndef CONFIG_USER_ONLY
    /* Set the number of interrupts supported by the CPU. */
    qdev_init_gpio_in(DEVICE(cpu), avr_cpu_set_int, 57);
#endif
}

static ObjectClass *avr_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;

    oc = object_class_by_name(cpu_model);
    if (object_class_dynamic_cast(oc, TYPE_AVR_CPU) == NULL ||
        object_class_is_abstract(oc)) {
        oc = NULL;
    }
    return oc;
}

static void avr_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    AVRCPU *cpu = AVR_CPU(cs);
    CPUAVRState *env = &cpu->env;
    int i;

    qemu_fprintf(f, "\n");
    qemu_fprintf(f, "PC:    %06x\n", env->pc_w);
    qemu_fprintf(f, "SP:      %04x\n", env->sp);
    qemu_fprintf(f, "rampD:     %02x\n", env->rampD >> 16);
    qemu_fprintf(f, "rampX:     %02x\n", env->rampX >> 16);
    qemu_fprintf(f, "rampY:     %02x\n", env->rampY >> 16);
    qemu_fprintf(f, "rampZ:     %02x\n", env->rampZ >> 16);
    qemu_fprintf(f, "EIND:      %02x\n", env->eind >> 16);
    qemu_fprintf(f, "X:       %02x%02x\n", env->r[27], env->r[26]);
    qemu_fprintf(f, "Y:       %02x%02x\n", env->r[29], env->r[28]);
    qemu_fprintf(f, "Z:       %02x%02x\n", env->r[31], env->r[30]);
    qemu_fprintf(f, "SREG:    [ %c %c %c %c %c %c %c %c ]\n",
                        env->sregI ? 'I' : '-',
                        env->sregT ? 'T' : '-',
                        env->sregH ? 'H' : '-',
                        env->sregS ? 'S' : '-',
                        env->sregV ? 'V' : '-',
                        env->sregN ? '-' : 'N', /* Zf has negative logic */
                        env->sregZ ? 'Z' : '-',
                        env->sregC ? 'I' : '-');
    qemu_fprintf(f, "SKIP:    %02x\n", env->skip);

    qemu_fprintf(f, "\n");
    for (i = 0; i < ARRAY_SIZE(env->r); i++) {
        qemu_fprintf(f, "R[%02d]:  %02x   ", i, env->r[i]);

        if ((i % 8) == 7) {
            qemu_fprintf(f, "\n");
        }
    }
    qemu_fprintf(f, "\n");
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
    cc->tlb_fill = avr_cpu_tlb_fill;
    cc->tcg_initialize = avr_cpu_tcg_init;
    cc->synchronize_from_tb = avr_cpu_synchronize_from_tb;
    cc->gdb_read_register = avr_cpu_gdb_read_register;
    cc->gdb_write_register = avr_cpu_gdb_write_register;
    cc->gdb_num_core_regs = 35;
    cc->gdb_core_xml_file = "avr-cpu.xml";
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


static void avr_cpu_list_entry(gpointer data, gpointer user_data)
{
    const char *typename = object_class_get_name(OBJECT_CLASS(data));

    qemu_printf("%s\n", typename);
}

void avr_cpu_list(void)
{
    GSList *list;
    list = object_class_get_list_sorted(TYPE_AVR_CPU, false);
    g_slist_foreach(list, avr_cpu_list_entry, NULL);
    g_slist_free(list);
}

#define DEFINE_AVR_CPU_TYPE(model, initfn) \
    { \
        .parent = TYPE_AVR_CPU, \
        .instance_init = initfn, \
        .name = model "-avr-cpu", \
    }

static const TypeInfo avr_cpu_type_info[] = {
    {
        .name = TYPE_AVR_CPU,
        .parent = TYPE_CPU,
        .instance_size = sizeof(AVRCPU),
        .instance_init = avr_cpu_initfn,
        .class_size = sizeof(AVRCPUClass),
        .class_init = avr_cpu_class_init,
        .abstract = true,
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
