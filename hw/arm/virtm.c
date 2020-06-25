/*
 * Virtual ARM Cortex M
 *
 * Copyright © 2020, Keith Packard <keithp@keithp.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/arm/boot.h"
#include "hw/boards.h"
#include "exec/address-spaces.h"
#include "hw/arm/armv7m.h"
#include "hw/misc/unimp.h"
#include "cpu.h"

#define NUM_IRQ_LINES 32
#define ROM_BASE  0x00000000
#define ROM_SIZE  0x20000000
#define RAM_BASE    0x20000000
#define RAM_SIZE    0x20000000

static const char *valid_cpus[] = {
    ARM_CPU_TYPE_NAME("cortex-m0"),
    ARM_CPU_TYPE_NAME("cortex-m3"),
    ARM_CPU_TYPE_NAME("cortex-m33"),
    ARM_CPU_TYPE_NAME("cortex-m4"),
    ARM_CPU_TYPE_NAME("cortex-m7"),
};

static bool cpu_type_valid(const char *cpu)
{
    int i;

    return true;
    for (i = 0; i < ARRAY_SIZE(valid_cpus); i++) {
        if (strcmp(cpu, valid_cpus[i]) == 0) {
            return true;
        }
    }
    return false;
}

static void machvirtm_init(MachineState *ms)
{
    DeviceState *nvic;

    if (!cpu_type_valid(ms->cpu_type)) {
        error_report("virtm: CPU type %s not supported", ms->cpu_type);
        exit(1);
    }

    MemoryRegion *ram = g_new(MemoryRegion, 1);
    MemoryRegion *rom = g_new(MemoryRegion, 1);
    MemoryRegion *system_memory = get_system_memory();

    /* Flash programming is done via the SCU, so pretend it is ROM.  */
    memory_region_init_rom(rom, NULL, "virtm.rom", ROM_SIZE,
                           &error_fatal);
    memory_region_add_subregion(system_memory, ROM_BASE, rom);

    memory_region_init_ram(ram, NULL, "virtm.ram", RAM_SIZE,
                           &error_fatal);
    memory_region_add_subregion(system_memory, RAM_BASE, ram);

    nvic = qdev_new(TYPE_ARMV7M);
    qdev_prop_set_uint32(nvic, "num-irq", NUM_IRQ_LINES);
    qdev_prop_set_string(nvic, "cpu-type", ms->cpu_type);
    qdev_prop_set_bit(nvic, "enable-bitband", true);
    object_property_set_link(OBJECT(nvic), OBJECT(get_system_memory()),
                                     "memory", &error_abort);
    /* This will exit with an error if the user passed us a bad cpu_type */
    sysbus_realize_and_unref(SYS_BUS_DEVICE(nvic), &error_fatal);

    armv7m_load_kernel(ARM_CPU(first_cpu), ms->kernel_filename, ROM_SIZE);
}

static void virtm_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Virtual Cortex-M";
    mc->init = machvirtm_init;
    mc->ignore_memory_transaction_failures = true;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-m3");
}

static const TypeInfo virtm_type = {
    .name = MACHINE_TYPE_NAME("virtm"),
    .parent = TYPE_MACHINE,
    .class_init = virtm_class_init,
};

static void virtm_machine_init(void)
{
    type_register_static(&virtm_type);
}

type_init(virtm_machine_init)
