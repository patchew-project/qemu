/*
 * Raspberry Pi Pico emulation
 *
 * Copyright (c) 2021 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "hw/arm/rp2040.h"
#include "qemu/error-report.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/arm/boot.h"
#include "qom/object.h"

struct PiPicoMachineState {
    /*< private >*/
    MachineState parent_obj;
    /*< public >*/
    RP2040State soc;
    MemoryRegion flash;
};
typedef struct PiPicoMachineState PiPicoMachineState;

struct PiPicoMachineClass {
    /*< private >*/
    MachineClass parent_obj;
    /*< public >*/
};

typedef struct PiPicoMachineClass PiPicoMachineClass;

#define TYPE_PIPICO_MACHINE       MACHINE_TYPE_NAME("raspi-pico")
DECLARE_OBJ_CHECKERS(PiPicoMachineState, PiPicoMachineClass,
                     PIPICO_MACHINE, TYPE_PIPICO_MACHINE)

#define RP2040_XIP_BASE   0x10000000

static void pipico_machine_init(MachineState *machine)
{
    PiPicoMachineState *s = PIPICO_MACHINE(machine);
    MemoryRegion *sysmem = get_system_memory();
    Error **errp = &error_fatal;

    /* Setup the SOC */
    object_initialize_child(OBJECT(machine), "soc", &s->soc, TYPE_RP2040);
    object_property_set_link(OBJECT(&s->soc), "memory", OBJECT(sysmem), errp);

    /*
     * The flash device it external to the SoC and mounted on the
     * PiPico board itself. We will "load" the actual contents with
     * armv7_load_kernel later although we still rely on the SoC's
     * mask ROM to get to it.
     */
    const uint32_t flash_size = 256 * KiB;
    memory_region_init_rom(&s->flash, NULL, "pico.flash0", flash_size, errp);
    memory_region_add_subregion(sysmem, RP2040_XIP_BASE, &s->flash);


    sysbus_realize(SYS_BUS_DEVICE(&s->soc), &error_fatal);

    /* This assumes the "kernel" is positioned in the XIP Flash block */
    armv7m_load_kernel(ARM_CPU(first_cpu), machine->kernel_filename, RP2040_XIP_BASE);
}

static void pipico_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = g_strdup_printf("Raspberry Pi Pico");
    mc->init = pipico_machine_init;
    mc->block_default_type = IF_PFLASH;
    mc->no_parallel = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->no_sdcard = 1;
    mc->min_cpus = 2;
    mc->default_cpus = 2;
    mc->max_cpus = 2;
};


static const TypeInfo pipico_machine_types[] = {
    {
        .name           = TYPE_PIPICO_MACHINE,
        .parent         = TYPE_MACHINE,
        .instance_size  = sizeof(PiPicoMachineState),
        .class_size     = sizeof(PiPicoMachineClass),
        .class_init     = pipico_machine_class_init,
    }
};

DEFINE_TYPES(pipico_machine_types)
