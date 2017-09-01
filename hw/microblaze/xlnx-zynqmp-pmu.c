/*
 * Xilinx Zynq MPSoC PMU (Power Management Unit) emulation
 *
 * Copyright (C) 2017 Xilinx Inc
 * Written by Alistair Francis <alistair.francis@xilinx.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "exec/address-spaces.h"
#include "hw/boards.h"
#include "hw/qdev-properties.h"
#include "cpu.h"
#include "boot.h"

/* Define the PMU device */

#define TYPE_XLNX_ZYNQMP_PMU "xlnx,zynqmp-pmu"
#define XLNX_ZYNQMP_PMU(obj) OBJECT_CHECK(XlnxZynqMPPMUState, (obj), \
                                          TYPE_XLNX_ZYNQMP_PMU)

#define XLNX_ZYNQMP_PMU_ROM_SIZE    0x8000
#define XLNX_ZYNQMP_PMU_ROM_ADDR    0xFFD00000
#define XLNX_ZYNQMP_PMU_RAM_ADDR    0xFFDC0000

typedef struct XlnxZynqMPPMUState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    MicroBlazeCPU cpu;
}  XlnxZynqMPPMUState;

static void xlnx_zynqmp_pmu_init(Object *obj)
{
    XlnxZynqMPPMUState *s = XLNX_ZYNQMP_PMU(obj);

    object_initialize(&s->cpu, sizeof(s->cpu),
                      TYPE_MICROBLAZE_CPU);
    object_property_add_child(obj, "pmu-cpu[*]", OBJECT(&s->cpu),
                              &error_abort);
}

static void xlnx_zynqmp_pmu_realize(DeviceState *dev, Error **errp)
{
    XlnxZynqMPPMUState *s = XLNX_ZYNQMP_PMU(dev);

    object_property_set_uint(OBJECT(&s->cpu), XLNX_ZYNQMP_PMU_ROM_ADDR,
                             "base-vectors", &error_abort);
    object_property_set_bool(OBJECT(&s->cpu), true, "use-stack-protection",
                             &error_abort);
    object_property_set_uint(OBJECT(&s->cpu), 0, "use-fpu", &error_abort);
    object_property_set_uint(OBJECT(&s->cpu), 0, "use-hw-mul", &error_abort);
    object_property_set_bool(OBJECT(&s->cpu), true, "use-barrel",
                             &error_abort);
    object_property_set_bool(OBJECT(&s->cpu), true, "use-msr-instr",
                             &error_abort);
    object_property_set_bool(OBJECT(&s->cpu), true, "use-pcmp-instr",
                             &error_abort);
    object_property_set_bool(OBJECT(&s->cpu), false, "use-mmu", &error_abort);
    object_property_set_bool(OBJECT(&s->cpu), true, "endianness",
                             &error_abort);
    object_property_set_str(OBJECT(&s->cpu), "8.40.b", "version",
                            &error_abort);
    object_property_set_uint(OBJECT(&s->cpu), 0, "pvr", &error_abort);
    object_property_set_bool(OBJECT(&s->cpu), true, "realized", &error_fatal);
}

static void xlnx_zynqmp_pmu_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = xlnx_zynqmp_pmu_realize;
}

static const TypeInfo xlnx_zynqmp_pmu_type_info = {
    .name = TYPE_XLNX_ZYNQMP_PMU,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(XlnxZynqMPPMUState),
    .instance_init = xlnx_zynqmp_pmu_init,
    .class_init = xlnx_zynqmp_pmu_class_init,
};

static void xlnx_zynqmp_pmu_register_types(void)
{
    type_register_static(&xlnx_zynqmp_pmu_type_info);
}

type_init(xlnx_zynqmp_pmu_register_types)

/* Define the PMU Machine */

static void xlnx_zcu102_pmu_init(MachineState *machine)
{
    XlnxZynqMPPMUState *pmu = g_new0(XlnxZynqMPPMUState, 1);
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *pmu_rom = g_new(MemoryRegion, 1);
    MemoryRegion *pmu_ram = g_new(MemoryRegion, 1);

    /* Create the ROM */
    memory_region_init_rom(pmu_rom, NULL, "xlnx-zcu102-pmu.rom",
                           XLNX_ZYNQMP_PMU_ROM_SIZE, &error_fatal);
    memory_region_add_subregion(address_space_mem, XLNX_ZYNQMP_PMU_ROM_ADDR,
                                pmu_rom);

    /* Create the RAM */
    memory_region_init_ram(pmu_ram, NULL, "xlnx-zcu102-pmu.ram",
                           machine->ram_size, &error_fatal);
    memory_region_add_subregion(address_space_mem, XLNX_ZYNQMP_PMU_RAM_ADDR,
                                pmu_ram);

    /* Create the PMU device */
    object_initialize(pmu, sizeof(XlnxZynqMPPMUState), TYPE_XLNX_ZYNQMP_PMU);
    object_property_add_child(OBJECT(machine), "pmu", OBJECT(pmu),
                              &error_abort);
    object_property_set_bool(OBJECT(pmu), true, "realized", &error_fatal);

    /* Load the kernel */
    microblaze_load_kernel(&pmu->cpu, XLNX_ZYNQMP_PMU_RAM_ADDR,
                           machine->ram_size,
                           machine->kernel_filename,
                           machine->dtb,
                           NULL);
}

static void xlnx_zcu102_pmu_machine_init(MachineClass *mc)
{
    mc->desc = "Xilinx ZynqMP ZCU102 PMU machine";
    mc->init = xlnx_zcu102_pmu_init;
}

DEFINE_MACHINE("xlnx-zcu102-pmu", xlnx_zcu102_pmu_machine_init)

