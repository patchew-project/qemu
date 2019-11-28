/*
 * QEMU Arduino boards
 *
 * Copyright (c) 2019 Philippe Mathieu-Daudé
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/* TODO: Implement the use of EXTRAM */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "elf.h"
#include "atmega.h"

typedef struct ArduinoBoardConfig {
    const char *name;
    const char *desc;
    const char *alias;
    const char *mcu_type;
    uint64_t xtal_hz;
    size_t extram_size;
    bool is_default;
} ArduinoBoardConfig;

static const ArduinoBoardConfig arduino_boards[] = {
    {
        /* https://www.arduino.cc/en/Main/ArduinoBoardDuemilanove */
        .name       = MACHINE_TYPE_NAME("arduino-duemilanove"),
        .desc       = "Arduino Duemilanove (ATmega168)",
        .alias      = "2009",
        .mcu_type    = TYPE_ATMEGA168,
        .xtal_hz    = 16 * 1000 * 1000,
    }, {
        /* https://store.arduino.cc/arduino-uno-rev3 */
        .name       = MACHINE_TYPE_NAME("arduino-uno"),
        .desc       = "Arduino Duemilanove (ATmega328P)",
        .alias      = "UNO",
        .mcu_type    = TYPE_ATMEGA328,
        .xtal_hz    = 16 * 1000 * 1000,
    }, {
        /* https://www.arduino.cc/en/Main/ArduinoBoardMega */
        .name       = MACHINE_TYPE_NAME("arduino-mega"),
        .desc       = "Arduino Mega (ATmega1280)",
        .alias      = "MEGA",
        .mcu_type    = TYPE_ATMEGA1280,
        .xtal_hz    = 16 * 1000 * 1000,
    }, {
        /* https://store.arduino.cc/arduino-mega-2560-rev3 */
        .name       = MACHINE_TYPE_NAME("arduino-mega-2560-v3"),
        .desc       = "Arduino Mega 2560 (ATmega2560)",
        .alias      = "MEGA2560",
        .mcu_type    = TYPE_ATMEGA2560,
        .xtal_hz    = 16 * 1000 * 1000, /* CSTCE16M0V53-R0 */
        .is_default = true,
    },
};

typedef struct ArduinoMachineState {
    /*< private >*/
    MachineState parent_obj;
    /*< public >*/
    AtmegaState mcu;
    MemoryRegion extram;
} ArduinoMachineState;

typedef struct ArduinoMachineClass {
    /*< private >*/
    MachineClass parent_class;
    /*< public >*/
    const ArduinoBoardConfig *config;
} ArduinoMachineClass;

#define TYPE_ARDUINO_MACHINE \
        MACHINE_TYPE_NAME("arduino")
#define ARDUINO_MACHINE(obj) \
        OBJECT_CHECK(ArduinoMachineState, (obj), TYPE_ARDUINO_MACHINE)
#define ARDUINO_MACHINE_CLASS(klass) \
        OBJECT_CLASS_CHECK(ArduinoMachineClass, (klass), TYPE_ARDUINO_MACHINE)
#define ARDUINO_MACHINE_GET_CLASS(obj) \
        OBJECT_GET_CLASS(ArduinoMachineClass, (obj), TYPE_ARDUINO_MACHINE)

static void load_firmware(const char *firmware, uint64_t flash_size)
{
    const char *filename;
    int bytes_loaded;

    /* Load firmware (contents of flash) trying to auto-detect format */
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, firmware);
    if (filename == NULL) {
        error_report("Unable to find %s", firmware);
        exit(1);
    }

    bytes_loaded = load_elf(filename, NULL, NULL, NULL, NULL, NULL, NULL,
                            0, EM_NONE, 0, 0);
    if (bytes_loaded < 0) {
        bytes_loaded = load_image_targphys(filename, OFFSET_CODE, flash_size);
    }
    if (bytes_loaded < 0) {
        error_report("Unable to load firmware image %s as ELF or raw binary",
                     firmware);
        exit(1);
    }
}

static void arduino_machine_init(MachineState *machine)
{
    ArduinoMachineClass *amc = ARDUINO_MACHINE_GET_CLASS(machine);
    ArduinoMachineState *ams = ARDUINO_MACHINE(machine);

    sysbus_init_child_obj(OBJECT(machine), "mcu", &ams->mcu, sizeof(ams->mcu),
                          amc->config->mcu_type);
    object_property_set_uint(OBJECT(&ams->mcu), amc->config->xtal_hz,
                             "xtal-frequency-hz", &error_abort);
    object_property_set_bool(OBJECT(&ams->mcu), true, "realized",
                             &error_abort);

    if (machine->firmware) {
        load_firmware(machine->firmware, memory_region_size(&ams->mcu.flash));
    }
}

static void arduino_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    ArduinoMachineClass *amc = ARDUINO_MACHINE_CLASS(oc);
    const ArduinoBoardConfig *cfg = data;

    mc->desc = cfg->desc;
    mc->alias = cfg->alias;
    mc->init = arduino_machine_init;
    mc->default_cpus = 1;
    mc->min_cpus = mc->default_cpus;
    mc->max_cpus = mc->default_cpus;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->no_parallel = 1;
    mc->is_default = cfg->is_default;
    mc->default_ram_size = cfg->extram_size;
    amc->config = cfg;
}

static const TypeInfo arduino_machine_type = {
    .name = TYPE_ARDUINO_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(ArduinoMachineState),
    .class_size = sizeof(ArduinoMachineClass),
    .abstract = true,
};

static void arduino_machine_types(void)
{
    size_t i;

    type_register_static(&arduino_machine_type);
    for (i = 0; i < ARRAY_SIZE(arduino_boards); ++i) {
        TypeInfo ti = {
            .name       = arduino_boards[i].name,
            .parent     = TYPE_ARDUINO_MACHINE,
            .class_init = arduino_machine_class_init,
            .class_data = (void *)&arduino_boards[i],
        };
        type_register(&ti);
    }
}

type_init(arduino_machine_types)
