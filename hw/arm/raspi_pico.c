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


static void pipico_machine_init(MachineState *machine)
{
    PiPicoMachineState *s = PIPICO_MACHINE(machine);

    /* Setup the SOC */
    object_initialize_child(OBJECT(machine), "soc", &s->soc, TYPE_RP2040);
    sysbus_realize(SYS_BUS_DEVICE(&s->soc), &error_fatal);
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
