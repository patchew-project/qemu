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

#include "hw/intc/xlnx-zynqmp-ipi.h"
#include "hw/intc/xlnx-pmu-iomod-intc.h"
#include "hw/timer/xlnx-pmu-iomod-pit.h"
#include "hw/gpio/xlnx-pmu-iomod-gp.h"

/* Define the PMU device */

#define TYPE_XLNX_ZYNQMP_PMU_SOC "xlnx,zynqmp-pmu-soc"
#define XLNX_ZYNQMP_PMU_SOC(obj) OBJECT_CHECK(XlnxZynqMPPMUSoCState, (obj), \
                                              TYPE_XLNX_ZYNQMP_PMU_SOC)

#define XLNX_ZYNQMP_PMU_ROM_SIZE    0x8000
#define XLNX_ZYNQMP_PMU_ROM_ADDR    0xFFD00000
#define XLNX_ZYNQMP_PMU_RAM_ADDR    0xFFDC0000

#define XLNX_ZYNQMP_PMU_INTC_ADDR   0xFFD40000

#define XLNX_ZYNQMP_PMU_NUM_IPIS    4
#define XLNX_ZYNQMP_PMU_NUM_PITS    4

#define XLNX_ZYNQMP_PMU_NUM_IOMOD_GPIS    4
#define XLNX_ZYNQMP_PMU_NUM_IOMOD_GPOS    4

static const uint64_t ipi_addr[XLNX_ZYNQMP_PMU_NUM_IPIS] = {
    0xFF340000, 0xFF350000, 0xFF360000, 0xFF370000,
};
static const uint64_t ipi_irq[XLNX_ZYNQMP_PMU_NUM_IPIS] = {
    19, 20, 21, 22,
};

static const uint64_t pit_addr[XLNX_ZYNQMP_PMU_NUM_PITS] = {
    0xFFD40040, 0xFFD40050, 0xFFD40060, 0xFFD40070,
};
static const uint64_t pit_irq[XLNX_ZYNQMP_PMU_NUM_PITS] = {
    3, 4, 5, 6,
};

static const uint64_t iomod_gpi_addr[XLNX_ZYNQMP_PMU_NUM_IOMOD_GPIS] = {
    0xFFD40020, 0xFFD40024, 0xFFD40028, 0xFFD4002C,
};
static const uint64_t iomod_gpi_irq[XLNX_ZYNQMP_PMU_NUM_IOMOD_GPIS] = {
    11, 12, 13, 14,
};

static const uint64_t iomod_gpo_addr[XLNX_ZYNQMP_PMU_NUM_IOMOD_GPOS] = {
    0xFFD40010, 0xFFD40014, 0xFFD40018, 0xFFD4001C,
};

typedef struct XlnxZynqMPPMUSoCState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    MicroBlazeCPU cpu;
    XlnxPMUIOIntc intc;
}  XlnxZynqMPPMUSoCState;


static void xlnx_zynqmp_pmu_soc_init(Object *obj)
{
    XlnxZynqMPPMUSoCState *s = XLNX_ZYNQMP_PMU_SOC(obj);

    object_initialize(&s->cpu, sizeof(s->cpu),
                      TYPE_MICROBLAZE_CPU);
    object_property_add_child(obj, "pmu-cpu", OBJECT(&s->cpu),
                              &error_abort);

    object_initialize(&s->intc, sizeof(s->intc), TYPE_XLNX_PMU_IO_INTC);
    qdev_set_parent_bus(DEVICE(&s->intc), sysbus_get_default());
}

static void xlnx_zynqmp_pmu_soc_realize(DeviceState *dev, Error **errp)
{
    XlnxZynqMPPMUSoCState *s = XLNX_ZYNQMP_PMU_SOC(dev);
    Error *err = NULL;

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
    object_property_set_bool(OBJECT(&s->cpu), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    object_property_set_uint(OBJECT(&s->intc), 0x10, "intc-intr-size",
                             &error_abort);
    object_property_set_uint(OBJECT(&s->intc), 0x0, "intc-level-edge",
                             &error_abort);
    object_property_set_uint(OBJECT(&s->intc), 0xffff, "intc-positive",
                             &error_abort);
    object_property_set_bool(OBJECT(&s->intc), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->intc), 0, XLNX_ZYNQMP_PMU_INTC_ADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->intc), 0,
                       qdev_get_gpio_in(DEVICE(&s->cpu), MB_CPU_IRQ));
}

static void xlnx_zynqmp_pmu_soc_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = xlnx_zynqmp_pmu_soc_realize;
}

static const TypeInfo xlnx_zynqmp_pmu_soc_type_info = {
    .name = TYPE_XLNX_ZYNQMP_PMU_SOC,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(XlnxZynqMPPMUSoCState),
    .instance_init = xlnx_zynqmp_pmu_soc_init,
    .class_init = xlnx_zynqmp_pmu_soc_class_init,
};

static void xlnx_zynqmp_pmu_soc_register_types(void)
{
    type_register_static(&xlnx_zynqmp_pmu_soc_type_info);
}

type_init(xlnx_zynqmp_pmu_soc_register_types)

/* Define the PMU Machine */

static void xlnx_zynqmp_pmu_init(MachineState *machine)
{
    XlnxZynqMPPMUSoCState *pmu = g_new0(XlnxZynqMPPMUSoCState, 1);
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *pmu_rom = g_new(MemoryRegion, 1);
    MemoryRegion *pmu_ram = g_new(MemoryRegion, 1);
    XlnxZynqMPIPI *ipi[XLNX_ZYNQMP_PMU_NUM_IPIS];
    XlnxPMUIOGPIO *iomod_gpi[XLNX_ZYNQMP_PMU_NUM_IOMOD_GPIS];
    XlnxPMUIOGPIO *iomod_gpo[XLNX_ZYNQMP_PMU_NUM_IOMOD_GPOS];
    XlnxPMUPIT *pit[XLNX_ZYNQMP_PMU_NUM_PITS];
    qemu_irq irq[32];
    qemu_irq tmp_irq;
    int i;

    /* Create the ROM */
    memory_region_init_rom(pmu_rom, NULL, "xlnx-zynqmp-pmu.rom",
                           XLNX_ZYNQMP_PMU_ROM_SIZE, &error_fatal);
    memory_region_add_subregion(address_space_mem, XLNX_ZYNQMP_PMU_ROM_ADDR,
                                pmu_rom);

    /* Create the RAM */
    memory_region_init_ram(pmu_ram, NULL, "xlnx-zynqmp-pmu.ram",
                           machine->ram_size, &error_fatal);
    memory_region_add_subregion(address_space_mem, XLNX_ZYNQMP_PMU_RAM_ADDR,
                                pmu_ram);

    /* Create the PMU device */
    object_initialize(pmu, sizeof(XlnxZynqMPPMUSoCState), TYPE_XLNX_ZYNQMP_PMU_SOC);
    object_property_add_child(OBJECT(machine), "pmu", OBJECT(pmu),
                              &error_abort);
    object_property_set_bool(OBJECT(pmu), true, "realized", &error_fatal);

    for (i = 0; i < 32; i++) {
        irq[i] = qdev_get_gpio_in(DEVICE(&pmu->intc), i);
    }

    /* Create and connect the IPI device */
    for (i = 0; i < XLNX_ZYNQMP_PMU_NUM_IPIS; i++) {
        ipi[i] = g_new0(XlnxZynqMPIPI, 1);
        object_initialize(ipi[i], sizeof(XlnxZynqMPIPI), TYPE_XLNX_ZYNQMP_IPI);
        qdev_set_parent_bus(DEVICE(ipi[i]), sysbus_get_default());
    }

    for (i = 0; i < XLNX_ZYNQMP_PMU_NUM_IPIS; i++) {
        object_property_set_bool(OBJECT(ipi[i]), true, "realized",
                                 &error_abort);
        sysbus_mmio_map(SYS_BUS_DEVICE(ipi[i]), 0, ipi_addr[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(ipi[i]), 0, irq[ipi_irq[i]]);
    }

    /* Create and connect the IOMOD GPI device */
    for (i = 0; i < XLNX_ZYNQMP_PMU_NUM_IOMOD_GPIS; i++) {
        iomod_gpi[i] = g_new0(XlnxPMUIOGPIO, 1);
        object_initialize(iomod_gpi[i], sizeof(XlnxPMUIOGPIO),
                          TYPE_XLNX_ZYNQMP_IOMOD_GPIO);
        qdev_set_parent_bus(DEVICE(iomod_gpi[i]), sysbus_get_default());
    }

    for (i = 0; i < XLNX_ZYNQMP_PMU_NUM_IOMOD_GPIS; i++) {
        object_property_set_bool(OBJECT(iomod_gpi[i]), true, "input",
                                 &error_abort);
        object_property_set_uint(OBJECT(iomod_gpi[i]), 0x20, "size",
                                 &error_abort);
        object_property_set_bool(OBJECT(iomod_gpi[i]), true, "realized",
                                 &error_abort);
        sysbus_mmio_map(SYS_BUS_DEVICE(iomod_gpi[i]), 0, iomod_gpi_addr[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(iomod_gpi[i]), 0,
                           irq[iomod_gpi_irq[i]]);
        /* The other GPIO lines connect to the ARM side of the SoC. When we
         * have a way to model MicroBlaze QEMU and ARM QEMU together we can
         * connect the GPIO lines.
         */
    }

    /* Create and connect the IOMOD GPO device */
    for (i = 0; i < XLNX_ZYNQMP_PMU_NUM_IOMOD_GPOS; i++) {
        iomod_gpo[i] = g_new0(XlnxPMUIOGPIO, 1);
        object_initialize(iomod_gpo[i], sizeof(XlnxPMUIOGPIO),
                          TYPE_XLNX_ZYNQMP_IOMOD_GPIO);
        qdev_set_parent_bus(DEVICE(iomod_gpo[i]), sysbus_get_default());
    }

    for (i = 0; i < XLNX_ZYNQMP_PMU_NUM_IOMOD_GPOS; i++) {
        object_property_set_bool(OBJECT(iomod_gpo[i]), false, "input",
                                 &error_abort);
        if (i) {
            object_property_set_uint(OBJECT(iomod_gpo[i]), 0x20, "size",
                                     &error_abort);
        } else {
            object_property_set_uint(OBJECT(iomod_gpo[i]), 0x09, "size",
                                     &error_abort);
        }
            object_property_set_uint(OBJECT(iomod_gpo[i]), 0x00, "gpo-init",
                                     &error_abort);
        object_property_set_bool(OBJECT(iomod_gpo[i]), true, "realized",
                                 &error_abort);
        sysbus_mmio_map(SYS_BUS_DEVICE(iomod_gpo[i]), 0, iomod_gpo_addr[i]);
    }

    /* Create and connect the IOMOD PIT devices */
    for (i = 0; i < XLNX_ZYNQMP_PMU_NUM_PITS; i++) {
        pit[i] = g_new0(XlnxPMUPIT, 1);
        object_initialize(pit[i], sizeof(XlnxPMUPIT),
                          TYPE_XLNX_ZYNQMP_IOMODULE_PIT);
        qdev_set_parent_bus(DEVICE(pit[i]), sysbus_get_default());
    }

    for (i = 0; i < XLNX_ZYNQMP_PMU_NUM_PITS; i++) {
        object_property_set_bool(OBJECT(pit[i]), true, "realized",
                                 &error_abort);
        sysbus_mmio_map(SYS_BUS_DEVICE(pit[i]), 0, pit_addr[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(pit[i]), 0, irq[pit_irq[i]]);
    }

    /* PIT1 hits into PIT0 */
    tmp_irq = qdev_get_gpio_in_named(DEVICE(pit[0]), "ps_hit_in", 0);
    qdev_connect_gpio_out_named(DEVICE(pit[1]), "ps_hit_out", 0, tmp_irq);

    /* PIT3 hits into PIT2 */
    tmp_irq = qdev_get_gpio_in_named(DEVICE(pit[2]), "ps_hit_in", 0);
    qdev_connect_gpio_out_named(DEVICE(pit[3]), "ps_hit_out", 0, tmp_irq);

    /* GP01 goes into PIT0 */
    tmp_irq = qdev_get_gpio_in_named(DEVICE(pit[0]), "ps_config", 0);
    qdev_connect_gpio_out(DEVICE(iomod_gpo[1]), 2, tmp_irq);

    /* GP01 goes into PIT2 */
    tmp_irq = qdev_get_gpio_in_named(DEVICE(pit[2]), "ps_config", 0);
    qdev_connect_gpio_out(DEVICE(iomod_gpo[1]), 6, tmp_irq);

    /* Load the kernel */
    microblaze_load_kernel(&pmu->cpu, XLNX_ZYNQMP_PMU_RAM_ADDR,
                           machine->ram_size,
                           machine->initrd_filename,
                           machine->dtb,
                           NULL);
}

static void xlnx_zynqmp_pmu_machine_init(MachineClass *mc)
{
    mc->desc = "Xilinx ZynqMP PMU machine";
    mc->init = xlnx_zynqmp_pmu_init;
}

DEFINE_MACHINE("xlnx-zynqmp-pmu", xlnx_zynqmp_pmu_machine_init)

