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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "cpu.h"
#include "hw/hw.h"
#include "sysemu/sysemu.h"
#include "sysemu/qtest.h"
#include "ui/console.h"
#include "hw/boards.h"
#include "hw/devices.h"
#include "hw/loader.h"
#include "qemu/error-report.h"
#include "exec/address-spaces.h"
#include "include/hw/sysbus.h"

#define SIZE_RAM    0x00020000

static void sample_init(MachineState *machine)
{
    MemoryRegion *mem;
    MemoryRegion *ram;

    ARCCPU *cpu_arc ATTRIBUTE_UNUSED;

    mem = g_new(MemoryRegion, 1);
    ram = g_new(MemoryRegion, 1);

    cpu_arc = cpu_arc_init("any");

    memory_region_allocate_system_memory(mem, NULL, "arc.mem", SIZE_RAM);

    memory_region_init_ram(ram, NULL, "ram", SIZE_RAM, &error_fatal);
    memory_region_add_subregion(get_system_memory(), PHYS_BASE_RAM, ram);
    vmstate_register_ram_global(ram);

    char const *firmware = NULL;
    char const *filename;

    if (machine->firmware) {
        firmware = machine->firmware;
    }

    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, firmware);
    if (!filename) {
        error_report("Could not find flash image file '%s'", firmware);
        exit(1);
    }

    load_image_targphys(filename, PHYS_BASE_RAM + 0x100, SIZE_RAM);
}

static void sample_machine_init(MachineClass *mc)
{
    mc->desc = "ARC sample/example board";
    mc->init = sample_init;
    mc->is_default = 1;
}

DEFINE_MACHINE("sample", sample_machine_init)

