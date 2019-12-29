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

typedef struct ArduinoMachineState {
    /*< private >*/
    MachineState parent_obj;
    /*< public >*/
    AtmegaMcuState mcu;
    MemoryRegion extram;
} ArduinoMachineState;

typedef struct ArduinoMachineClass {
    /*< private >*/
    MachineClass parent_class;
    /*< public >*/
    const char *mcu_type;
    uint64_t xtal_hz;
    size_t extram_size;
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
                          amc->mcu_type);
    object_property_set_uint(OBJECT(&ams->mcu), amc->xtal_hz,
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

    mc->init = arduino_machine_init;
    mc->default_cpus = 1;
    mc->min_cpus = mc->default_cpus;
    mc->max_cpus = mc->default_cpus;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->no_parallel = 1;
}

static void arduino_duemilanove_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    ArduinoMachineClass *amc = ARDUINO_MACHINE_CLASS(oc);

    /* https://www.arduino.cc/en/Main/ArduinoBoardDuemilanove */
    mc->desc        = "Arduino Duemilanove (ATmega168)",
    mc->alias       = "2009";
    amc->mcu_type   = TYPE_ATMEGA168_MCU;
    amc->xtal_hz    = 16 * 1000 * 1000;
};

static void arduino_uno_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    ArduinoMachineClass *amc = ARDUINO_MACHINE_CLASS(oc);

    /* https://store.arduino.cc/arduino-uno-rev3 */
    mc->desc        = "Arduino UNO (ATmega328P)";
    mc->alias       = "uno";
    amc->mcu_type   = TYPE_ATMEGA328_MCU;
    amc->xtal_hz    = 16 * 1000 * 1000;
};

static void arduino_mega_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    ArduinoMachineClass *amc = ARDUINO_MACHINE_CLASS(oc);

    /* https://www.arduino.cc/en/Main/ArduinoBoardMega */
    mc->desc        = "Arduino Mega (ATmega1280)";
    mc->alias       = "mega";
    amc->mcu_type   = TYPE_ATMEGA1280_MCU;
    amc->xtal_hz    = 16 * 1000 * 1000;
};

static void arduino_mega2560_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    ArduinoMachineClass *amc = ARDUINO_MACHINE_CLASS(oc);

    /* https://store.arduino.cc/arduino-mega-2560-rev3 */
    mc->desc        = "Arduino Mega 2560 (ATmega2560)";
    mc->alias       = "mega2560";
    mc->is_default  = true;
    amc->mcu_type   = TYPE_ATMEGA2560_MCU;
    amc->xtal_hz    = 16 * 1000 * 1000; /* CSTCE16M0V53-R0 */
};

static const TypeInfo arduino_machine_types[] = {
    {
        .name          = MACHINE_TYPE_NAME("arduino-duemilanove"),
        .parent        = TYPE_ARDUINO_MACHINE,
        .class_init    = arduino_duemilanove_class_init,
    }, {
        .name          = MACHINE_TYPE_NAME("arduino-uno"),
        .parent        = TYPE_ARDUINO_MACHINE,
        .class_init    = arduino_uno_class_init,
    }, {
        .name          = MACHINE_TYPE_NAME("arduino-mega"),
        .parent        = TYPE_ARDUINO_MACHINE,
        .class_init    = arduino_mega_class_init,
    }, {
        .name          = MACHINE_TYPE_NAME("arduino-mega-2560-v3"),
        .parent        = TYPE_ARDUINO_MACHINE,
        .class_init    = arduino_mega2560_class_init,
    }, {
        .name           = TYPE_ARDUINO_MACHINE,
        .parent         = TYPE_MACHINE,
        .instance_size  = sizeof(ArduinoMachineState),
        .class_size     = sizeof(ArduinoMachineClass),
        .class_init     = arduino_machine_class_init,
        .abstract       = true,
    }
};

DEFINE_TYPES(arduino_machine_types)
