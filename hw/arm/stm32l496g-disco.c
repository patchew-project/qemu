/*
 * STM32L496G-DISCO Discovery Kit machine
 *
 * Copyright (c) 2026 EXALT Technologies
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * The reference used is the STMicroelectronics UM2160 user manual for the
 * Discovery kit with STM32L496AG MCU and the RM0351 reference manual.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/arm/armv7m.h"
#include "hw/arm/boot.h"
#include "hw/arm/stm32l4x5_soc.h"
#include "hw/core/boards.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/irq.h"
#include "hw/ssi/ssi.h"
#include "system/blockdev.h"

#define TYPE_STM32L496G_DISCO MACHINE_TYPE_NAME("stm32l496g-disco")
OBJECT_DECLARE_SIMPLE_TYPE(Stm32l496gDiscoMachineState,
                           STM32L496G_DISCO)

typedef struct Stm32l496gDiscoMachineState {
    MachineState parent_obj;

    Stm32l4x5SocState soc;
} Stm32l496gDiscoMachineState;

static void stm32l496g_disco_init(MachineState *machine)
{
    Stm32l496gDiscoMachineState *s = STM32L496G_DISCO(machine);
    const Stm32l4x5SocClass *sc;
    DeviceState *flash;
    DriveInfo *dinfo;
    BusState *bus;
    qemu_irq flash_cs;

    object_initialize_child(OBJECT(machine), "soc", &s->soc,
                            TYPE_STM32L496XG_SOC);
    sysbus_realize(SYS_BUS_DEVICE(&s->soc), &error_fatal);

    bus = qdev_get_child_bus(DEVICE(&s->soc.quadspi), "ssi");
    flash = qdev_new("mx25r6435");
    dinfo = drive_get(IF_MTD, 0, 0);
    if (dinfo) {
        qdev_prop_set_drive_err(flash, "drive", blk_by_legacy_dinfo(dinfo),
                                &error_fatal);
    }
    qdev_realize_and_unref(flash, bus, &error_fatal);

    flash_cs = qdev_get_gpio_in_named(flash, SSI_GPIO_CS, 0);
    qdev_connect_gpio_out_named(DEVICE(&s->soc.quadspi), "cs", 0,
                                flash_cs);
    qemu_set_irq(flash_cs, 1);

    sc = STM32L4X5_SOC_GET_CLASS(&s->soc);
    armv7m_load_kernel(s->soc.armv7m.cpu, machine->kernel_filename, 0,
                       sc->flash_size);
}

static void stm32l496g_disco_machine_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    static const char *machine_valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-m4"),
        NULL
    };

    mc->desc = "STM32L496G-DISCO Discovery Kit (Cortex-M4)";
    mc->init = stm32l496g_disco_init;
    mc->valid_cpu_types = machine_valid_cpu_types;

    /* SRAM is allocated by the SoC. */
    mc->default_ram_size = 0;
}

static void stm32l496g_disco_machine_register_types(void)
{
    static const TypeInfo machine_type = {
        .name = TYPE_STM32L496G_DISCO,
        .parent = TYPE_MACHINE,
        .instance_size = sizeof(Stm32l496gDiscoMachineState),
        .class_init = stm32l496g_disco_machine_init,
    };

    type_register_static(&machine_type);
}

type_init(stm32l496g_disco_machine_register_types)
