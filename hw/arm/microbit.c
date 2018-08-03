/*
 * BBC micro:bit machine
 * http://tech.microbit.org/hardware/
 *
 * Copyright 2018 Joel Stanley <joel@jms.id.au>
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/arm/arm.h"
#include "exec/address-spaces.h"

#include "hw/arm/nrf51_soc.h"

typedef struct {
    MachineState parent;

    NRF51State nrf51;
} MICROBITMachineState;

#define TYPE_MICROBIT_MACHINE "microbit"

#define MICROBIT_MACHINE(obj) \
    OBJECT_CHECK(MICROBITMachineState, obj, TYPE_MICROBIT_MACHINE)

static void microbit_init(MachineState *machine)
{
    MICROBITMachineState *s = g_new(MICROBITMachineState, 1);
    MemoryRegion *system_memory = get_system_memory();
    Object *soc;

    object_initialize(&s->nrf51, sizeof(s->nrf51), TYPE_NRF51_SOC);
    soc = OBJECT(&s->nrf51);
    object_property_add_child(OBJECT(machine), "nrf51", soc, &error_fatal);
    object_property_set_link(soc, OBJECT(system_memory),
                             "memory", &error_abort);

    object_property_set_bool(soc, true, "realized", &error_abort);

    arm_m_profile_load_kernel(ARM_CPU(first_cpu), machine->kernel_filename,
            NRF51_SOC(soc)->flash_size);
}

static void microbit_machine_init(MachineClass *mc)
{
    mc->desc = "BBC micro:bit";
    mc->init = microbit_init;
    mc->max_cpus = 1;
}
DEFINE_MACHINE("microbit", microbit_machine_init);
