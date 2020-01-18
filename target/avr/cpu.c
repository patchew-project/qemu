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
#include "qapi/error.h"
#include "qemu/qemu-print.h"
#include "exec/exec-all.h"
#include "cpu.h"
#include "disas/dis-asm.h"

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
    qdev_init_gpio_in(DEVICE(cpu), avr_cpu_set_int,
            sizeof(cpu->env.intsrc) * 8);
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

/*
 * Setting features of AVR core type avr1
 * --------------------------------------
 *
 * This type of AVR core is present in the following AVR MCUs:
 *
 * at90s1200, attiny11, attiny12, attiny15, attiny28
 */
static void avr_avr1_initfn(Object *obj)
{
    AVRCPU *cpu = AVR_CPU(obj);
    CPUAVRState *env = &cpu->env;

    avr_set_feature(env, AVR_FEATURE_LPM);
    avr_set_feature(env, AVR_FEATURE_2_BYTE_SP);
    avr_set_feature(env, AVR_FEATURE_2_BYTE_PC);
}

/*
 * Setting features of AVR core type avr2
 * --------------------------------------
 *
 * This type of AVR core is present in the following AVR MCUs:
 *
 * at90s2313, at90s2323, at90s2333, at90s2343, attiny22, attiny26, at90s4414,
 * at90s4433, at90s4434, at90s8515, at90c8534, at90s8535
 */
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

/*
 * Setting features of AVR core type avr25
 * --------------------------------------
 *
 * This type of AVR core is present in the following AVR MCUs:
 *
 * ata5272, ata6616c, attiny13, attiny13a, attiny2313, attiny2313a, attiny24,
 * attiny24a, attiny4313, attiny44, attiny44a, attiny441, attiny84, attiny84a,
 * attiny25, attiny45, attiny85, attiny261, attiny261a, attiny461, attiny461a,
 * attiny861, attiny861a, attiny43u, attiny87, attiny48, attiny88, attiny828,
 * attiny841, at86rf401
 */
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

/*
 * Setting features of AVR core type avr3
 * --------------------------------------
 *
 * This type of AVR core is present in the following AVR MCUs:
 *
 * at43usb355, at76c711
 */
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

/*
 * Setting features of AVR core type avr31
 * --------------------------------------
 *
 * This type of AVR core is present in the following AVR MCUs:
 *
 * atmega103, at43usb320
 */
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

/*
 * Setting features of AVR core type avr35
 * --------------------------------------
 *
 * This type of AVR core is present in the following AVR MCUs:
 *
 * ata5505, ata6617c, ata664251, at90usb82, at90usb162, atmega8u2, atmega16u2,
 * atmega32u2, attiny167, attiny1634
 */
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

/*
 * Setting features of AVR core type avr4
 * --------------------------------------
 *
 * This type of AVR core is present in the following AVR MCUs:
 *
 * ata6285, ata6286, ata6289, ata6612c, atmega8, atmega8a, atmega48, atmega48a,
 * atmega48p, atmega48pa, atmega48pb, atmega88, atmega88a, atmega88p,
 * atmega88pa, atmega88pb, atmega8515, atmega8535, atmega8hva, at90pwm1,
 * at90pwm2, at90pwm2b, at90pwm3, at90pwm3b, at90pwm81
 */
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

/*
 * Setting features of AVR core type avr5
 * --------------------------------------
 *
 * This type of AVR core is present in the following AVR MCUs:
 *
 * ata5702m322, ata5782, ata5790, ata5790n, ata5791, ata5795, ata5831, ata6613c,
 * ata6614q, ata8210, ata8510, atmega16, atmega16a, atmega161, atmega162,
 * atmega163, atmega164a, atmega164p, atmega164pa, atmega165, atmega165a,
 * atmega165p, atmega165pa, atmega168, atmega168a, atmega168p, atmega168pa,
 * atmega168pb, atmega169, atmega169a, atmega169p, atmega169pa, atmega16hvb,
 * atmega16hvbrevb, atmega16m1, atmega16u4, atmega32a, atmega32, atmega323,
 * atmega324a, atmega324p, atmega324pa, atmega325, atmega325a, atmega325p,
 * atmega325pa, atmega3250, atmega3250a, atmega3250p, atmega3250pa, atmega328,
 * atmega328p, atmega328pb, atmega329, atmega329a, atmega329p, atmega329pa,
 * atmega3290, atmega3290a, atmega3290p, atmega3290pa, atmega32c1, atmega32m1,
 * atmega32u4, atmega32u6, atmega406, atmega64, atmega64a, atmega640, atmega644,
 * atmega644a, atmega644p, atmega644pa, atmega645, atmega645a, atmega645p,
 * atmega6450, atmega6450a, atmega6450p, atmega649, atmega649a, atmega649p,
 * atmega6490, atmega16hva, atmega16hva2, atmega32hvb, atmega6490a, atmega6490p,
 * atmega64c1, atmega64m1, atmega64hve, atmega64hve2, atmega64rfr2,
 * atmega644rfr2, atmega32hvbrevb, at90can32, at90can64, at90pwm161, at90pwm216,
 * at90pwm316, at90scr100, at90usb646, at90usb647, at94k, m3000
 */
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

/*
 * Setting features of AVR core type avr51
 * --------------------------------------
 *
 * This type of AVR core is present in the following AVR MCUs:
 *
 * atmega128, atmega128a, atmega1280, atmega1281, atmega1284, atmega1284p,
 * atmega128rfa1, atmega128rfr2, atmega1284rfr2, at90can128, at90usb1286,
 * at90usb1287
 */
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

/*
 * Setting features of AVR core type avr6
 * --------------------------------------
 *
 * This type of AVR core is present in the following AVR MCUs:
 *
 * atmega2560, atmega2561, atmega256rfr2, atmega2564rfr2
 */
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

/*
 * Setting features of AVR core type avrtiny
 * --------------------------------------
 *
 * This type of AVR core is present in the following AVR MCUs:
 *
 * attiny4, attiny5, attiny9, attiny10, attiny20, attiny40
 */
static void avr_avrtiny_initfn(Object *obj)
{
    AVRCPU *cpu = AVR_CPU(obj);
    CPUAVRState *env = &cpu->env;

    avr_set_feature(env, AVR_FEATURE_LPM);
    avr_set_feature(env, AVR_FEATURE_IJMP_ICALL);
    avr_set_feature(env, AVR_FEATURE_BREAK);

    avr_set_feature(env, AVR_FEATURE_2_BYTE_PC);
    avr_set_feature(env, AVR_FEATURE_1_BYTE_SP);
}

/*
 * Setting features of AVR core type xmega2
 * --------------------------------------
 *
 * This type of AVR core is present in the following AVR MCUs:
 *
 * atxmega8e5, atxmega16a4, atxmega16d4, atxmega16e5, atxmega32a4, atxmega32c3,
 * atxmega32d3, atxmega32d4, atxmega16a4u, atxmega16c4, atxmega32a4u,
 * atxmega32c4, atxmega32e5
 */
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

/*
 * Setting features of AVR core type xmega3
 * --------------------------------------
 *
 * This type of AVR core is present in the following AVR MCUs:
 *
 * attiny212, attiny214, attiny412, attiny414, attiny416, attiny417, attiny814,
 * attiny816, attiny817, attiny1614, attiny1616, attiny1617, attiny3214,
 * attiny3216, attiny3217, atmega808, atmega809, atmega1608, atmega1609,
 * atmega3208, atmega3209, atmega4808, atmega4809
 */
static void avr_xmega3_initfn(Object *obj)
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

/*
 * Setting features of AVR core type xmega4
 * --------------------------------------
 *
 * This type of AVR core is present in the following AVR MCUs:
 *
 * atxmega64a3, atxmega64d3, atxmega64a3u, atxmega64a4u, atxmega64b1,
 * atxmega64b3, atxmega64c3, atxmega64d4
 */
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
    avr_set_feature(env, AVR_FEATURE_ELPMX);
    avr_set_feature(env, AVR_FEATURE_ELPM);
    avr_set_feature(env, AVR_FEATURE_JMP_CALL);
    avr_set_feature(env, AVR_FEATURE_LPMX);
    avr_set_feature(env, AVR_FEATURE_MOVW);
    avr_set_feature(env, AVR_FEATURE_MUL);
    avr_set_feature(env, AVR_FEATURE_RMW);
}

/*
 * Setting features of AVR core type xmega5
 * --------------------------------------
 *
 * This type of AVR core is present in the following AVR MCUs:
 *
 * atxmega64a1, atxmega64a1u
 */
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

/*
 * Setting features of AVR core type xmega6
 * --------------------------------------
 *
 * This type of AVR core is present in the following AVR MCUs:
 *
 * atxmega128a3, atxmega128d3, atxmega192a3, atxmega192d3, atxmega256a3,
 * atxmega256a3b, atxmega256a3bu, atxmega256d3, atxmega128a3u, atxmega128b1,
 * atxmega128b3, atxmega128c3, atxmega128d4, atxmega192a3u, atxmega192c3,
 * atxmega256a3u, atxmega256c3, atxmega384c3, atxmega384d3
 */
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

/*
 * Setting features of AVR core type xmega7
 * --------------------------------------
 *
 * This type of AVR core is present in the following AVR MCUs:
 *
 * atxmega128a1, atxmega128a1u, atxmega128a4u
 */
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
        .name = AVR_CPU_TYPE_NAME(model), \
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
    DEFINE_AVR_CPU_TYPE("avrtiny", avr_avrtiny_initfn),
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
    DEFINE_AVR_CPU_TYPE("xmega3", avr_xmega3_initfn),
    DEFINE_AVR_CPU_TYPE("xmega4", avr_xmega4_initfn),
    DEFINE_AVR_CPU_TYPE("xmega5", avr_xmega5_initfn),
    DEFINE_AVR_CPU_TYPE("xmega6", avr_xmega6_initfn),
    DEFINE_AVR_CPU_TYPE("xmega7", avr_xmega7_initfn),
};

const char *avr_flags_to_cpu_type(uint32_t flags, const char *def_cpu_type)
{
    switch (flags & EF_AVR_MACH) {
    case bfd_mach_avr1:
        return AVR_CPU_TYPE_NAME("avr1");
    case bfd_mach_avr2:
        return AVR_CPU_TYPE_NAME("avr2");
    case bfd_mach_avr25:
        return AVR_CPU_TYPE_NAME("avr25");
    case bfd_mach_avr3:
        return AVR_CPU_TYPE_NAME("avr3");
    case bfd_mach_avr31:
        return AVR_CPU_TYPE_NAME("avr31");
    case bfd_mach_avr35:
        return AVR_CPU_TYPE_NAME("avr35");
    case bfd_mach_avr4:
        return AVR_CPU_TYPE_NAME("avr4");
    case bfd_mach_avr5:
        return AVR_CPU_TYPE_NAME("avr5");
    case bfd_mach_avr51:
        return AVR_CPU_TYPE_NAME("avr51");
    case bfd_mach_avr6:
        return AVR_CPU_TYPE_NAME("avr6");
    case bfd_mach_avrtiny:
        return AVR_CPU_TYPE_NAME("avrtiny");
    case bfd_mach_avrxmega2:
        return AVR_CPU_TYPE_NAME("xmega2");
    case bfd_mach_avrxmega3:
        return AVR_CPU_TYPE_NAME("xmega3");
    case bfd_mach_avrxmega4:
        return AVR_CPU_TYPE_NAME("xmega4");
    case bfd_mach_avrxmega5:
        return AVR_CPU_TYPE_NAME("xmega5");
    case bfd_mach_avrxmega6:
        return AVR_CPU_TYPE_NAME("xmega6");
    case bfd_mach_avrxmega7:
        return AVR_CPU_TYPE_NAME("xmega7");
    default:
        return def_cpu_type;
    }
}

DEFINE_TYPES(avr_cpu_type_info)
