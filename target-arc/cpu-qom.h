/*
 * QEMU ARC CPU
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

#ifndef QEMU_ARC_CPU_QOM_H
#define QEMU_ARC_CPU_QOM_H

#include "qom/cpu.h"

#define TYPE_ARC_CPU            "arc"

#define ARC_CPU_CLASS(klass)    \
                    OBJECT_CLASS_CHECK(ARCCPUClass, (klass), TYPE_ARC_CPU)
#define ARC_CPU(obj)            \
                    OBJECT_CHECK(ARCCPU, (obj), TYPE_ARC_CPU)
#define ARC_CPU_GET_CLASS(obj)  \
                    OBJECT_GET_CLASS(ARCCPUClass, (obj), TYPE_ARC_CPU)

/**
*  ARCCPUClass:
*  @parent_realize: The parent class' realize handler.
*  @parent_reset: The parent class' reset handler.
*  @vr: Version Register value.
*
*  A ARC CPU model.
*/
typedef struct ARCCPUClass {
    CPUClass        parent_class;

    DeviceRealize   parent_realize;
    void (*parent_reset)(CPUState *cpu);
} ARCCPUClass;

/**
*  ARCCPU:
*  @env: #CPUARCState
*
*  A ARC CPU.
*/
typedef struct ARCCPU {
    /*< private >*/
    CPUState        parent_obj;
    /*< public >*/

    CPUARCState     env;
} ARCCPU;

static inline ARCCPU *arc_env_get_cpu(CPUARCState *env)
{
    return container_of(env, ARCCPU, env);
}

#define ENV_GET_CPU(e)          CPU(arc_env_get_cpu(e))
#define ENV_OFFSET              offsetof(ARCCPU, env)

#ifndef CONFIG_USER_ONLY
extern const struct VMStateDescription vms_arc_cpu;
#endif

void arc_cpu_do_interrupt(CPUState *cpu);
bool arc_cpu_exec_interrupt(CPUState *cpu, int int_req);
void arc_cpu_dump_state(CPUState *cs, FILE *f,
                            fprintf_function cpu_fprintf, int flags);
hwaddr arc_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);
int arc_cpu_gdb_read_register(CPUState *cpu, uint8_t *buf, int reg);
int arc_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);

#endif
