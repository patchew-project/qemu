
/*
 * Test Machine for the IBM PPE42 processor
 *
 * Copyright (c) 2025, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "system/address-spaces.h"
#include "hw/boards.h"
#include "hw/ppc/ppc.h"
#include "system/system.h"
#include "system/reset.h"
#include "system/kvm.h"

static void main_cpu_reset(void *opaque)
{
    PowerPCCPU *cpu = opaque;

    cpu_reset(CPU(cpu));
}

static void ppe42_machine_init(MachineState *machine)
{
    PowerPCCPU *cpu;
    CPUPPCState *env;

    if (kvm_enabled()) {
        error_report("machine %s does not support the KVM accelerator",
                     MACHINE_GET_CLASS(machine)->name);
        exit(EXIT_FAILURE);
    }

    /* init CPU */
    cpu = POWERPC_CPU(cpu_create(machine->cpu_type));
    env = &cpu->env;
    if (PPC_INPUT(env) != PPC_FLAGS_INPUT_PPE42) {
        error_report("Incompatible CPU, only PPE42 bus supported");
        exit(1);
    }

    qemu_register_reset(main_cpu_reset, cpu);

    /* This sets the decrementer timebase */
    ppc_booke_timers_init(cpu, 37500000, PPC_TIMER_PPE);

    /* RAM */
    if (machine->ram_size > 2 * GiB) {
        error_report("RAM size more than 2 GiB is not supported");
        exit(1);
    }
    memory_region_add_subregion(get_system_memory(), 0xfff80000, machine->ram);
}


static void ppe42_machine_class_init(MachineClass *mc)
{
    mc->desc = "PPE42 Test Machine";
    mc->init = ppe42_machine_init;
    mc->default_cpu_type = POWERPC_CPU_TYPE_NAME("PPE42XM");
    mc->default_ram_id = "ram";
    mc->default_ram_size = 1 * MiB;
}

DEFINE_MACHINE("ppe42_machine", ppe42_machine_class_init)
