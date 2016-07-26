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

#ifndef QEMU_AVR_CPU_QOM_H
#define QEMU_AVR_CPU_QOM_H

#include "qom/cpu.h"

#define TYPE_AVR_CPU "avr"

#define AVR_CPU_CLASS(klass) \
                    OBJECT_CLASS_CHECK(AVRCPUClass, (klass), TYPE_AVR_CPU)
#define AVR_CPU(obj) \
                    OBJECT_CHECK(AVRCPU, (obj), TYPE_AVR_CPU)
#define AVR_CPU_GET_CLASS(obj) \
                    OBJECT_GET_CLASS(AVRCPUClass, (obj), TYPE_AVR_CPU)

/**
*  AVRCPUClass:
*  @parent_realize: The parent class' realize handler.
*  @parent_reset: The parent class' reset handler.
*  @vr: Version Register value.
*
*  A AVR CPU model.
 */
typedef struct AVRCPUClass {
    CPUClass parent_class;

    DeviceRealize parent_realize;
    void (*parent_reset)(CPUState *cpu);
} AVRCPUClass;

/**
*  AVRCPU:
*  @env: #CPUAVRState
*
*  A AVR CPU.
*/
typedef struct AVRCPU {
    /* < private > */
    CPUState parent_obj;
    /* < public > */

    CPUAVRState env;
} AVRCPU;

static inline AVRCPU *avr_env_get_cpu(CPUAVRState *env)
{
    return container_of(env, AVRCPU, env);
}

#define ENV_GET_CPU(e) CPU(avr_env_get_cpu(e))
#define ENV_OFFSET offsetof(AVRCPU, env)

#ifndef CONFIG_USER_ONLY
extern const struct VMStateDescription vms_avr_cpu;
#endif

void avr_cpu_do_interrupt(CPUState *cpu);
bool avr_cpu_exec_interrupt(CPUState *cpu, int int_req);
void avr_cpu_dump_state(CPUState *cs, FILE *f,
                            fprintf_function cpu_fprintf, int flags);
hwaddr avr_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);
int avr_cpu_gdb_read_register(CPUState *cpu, uint8_t *buf, int reg);
int avr_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);

#endif

