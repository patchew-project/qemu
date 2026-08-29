/*
 * Raspberry Pi Pico machine
 *
 * Copyright (c) 2021 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/arm/boot.h"
#include "hw/arm/machines-qom.h"
#include "hw/arm/rp2040.h"
#include "hw/core/boards.h"
#include "hw/core/qdev-properties.h"
#include "system/address-spaces.h"
#include "system/system.h"
#include "qom/object.h"

#define TYPE_RASPI_PICO_MACHINE MACHINE_TYPE_NAME("raspi-pico")
OBJECT_DECLARE_SIMPLE_TYPE(RaspiPicoMachineState, RASPI_PICO_MACHINE)

#define PICO_FLASH_SIZE (2 * MiB)

struct RaspiPicoMachineState {
    MachineState parent_obj;

    RP2040State soc;
    MemoryRegion flash;
};

static void raspi_pico_init(MachineState *machine)
{
    RaspiPicoMachineState *s = RASPI_PICO_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();

    object_initialize_child(OBJECT(machine), "soc", &s->soc, TYPE_RP2040);
    qdev_prop_set_chr(DEVICE(&s->soc), "serial0", serial_hd(0));
    qdev_prop_set_chr(DEVICE(&s->soc), "serial1", serial_hd(1));
    if (machine->firmware) {
        qdev_prop_set_string(DEVICE(&s->soc), "bootrom-file",
                             machine->firmware);
    }
    object_property_set_link(OBJECT(&s->soc), "memory",
                             OBJECT(system_memory), &error_fatal);

    memory_region_init_rom(&s->flash, NULL, "raspi-pico.flash",
                           PICO_FLASH_SIZE, &error_fatal);
    memory_region_add_subregion(system_memory, RP2040_XIP_BASE, &s->flash);

    sysbus_realize(SYS_BUS_DEVICE(&s->soc), &error_fatal);

    armv7m_load_kernel(s->soc.armv7m.cpu, machine->kernel_filename,
                       RP2040_XIP_BASE, PICO_FLASH_SIZE);
}

static void raspi_pico_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Raspberry Pi Pico (Cortex-M0+)";
    mc->init = raspi_pico_init;
    mc->default_cpus = 1;
    mc->min_cpus = 1;
    mc->max_cpus = 1;
    mc->default_ram_size = 0;
    mc->no_parallel = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
}

static const TypeInfo raspi_pico_machine_info = {
    .name = TYPE_RASPI_PICO_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(RaspiPicoMachineState),
    .class_init = raspi_pico_machine_class_init,
    .interfaces = arm_machine_interfaces,
};

static void raspi_pico_machine_init(void)
{
    type_register_static(&raspi_pico_machine_info);
}
type_init(raspi_pico_machine_init)
