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
#include "qemu/cutils.h"
#include "system/address-spaces.h"
#include "system/system.h"
#include "qom/object.h"

#define TYPE_RASPI_PICO_MACHINE MACHINE_TYPE_NAME("raspi-pico")
OBJECT_DECLARE_SIMPLE_TYPE(RaspiPicoMachineState, RASPI_PICO_MACHINE)

struct RaspiPicoMachineState {
    MachineState parent_obj;

    RP2040State soc;
    char *flash_file;
    char *flash_uid;
    uint64_t flash_uid_value;
    bool flash_uid_set;
    char *rosc_random_seed;
    uint64_t rosc_random_seed_value;
    bool rosc_random_seed_set;
    bool strict_uart_pins;
};

static char *raspi_pico_get_flash_file(Object *obj, Error **errp)
{
    RaspiPicoMachineState *s = RASPI_PICO_MACHINE(obj);

    return g_strdup(s->flash_file ?: "");
}

static void raspi_pico_set_flash_file(Object *obj, const char *value,
                                      Error **errp)
{
    RaspiPicoMachineState *s = RASPI_PICO_MACHINE(obj);

    g_free(s->flash_file);
    s->flash_file = g_strdup(value);
}

static char *raspi_pico_get_flash_uid(Object *obj, Error **errp)
{
    RaspiPicoMachineState *s = RASPI_PICO_MACHINE(obj);

    return g_strdup(s->flash_uid ?: "");
}

static void raspi_pico_set_flash_uid(Object *obj, const char *value,
                                     Error **errp)
{
    RaspiPicoMachineState *s = RASPI_PICO_MACHINE(obj);
    const char *p;
    uint64_t uid;
    int i;

    if (!value || !*value) {
        g_free(s->flash_uid);
        s->flash_uid = NULL;
        s->flash_uid_value = 0;
        s->flash_uid_set = false;
        return;
    }

    p = g_str_has_prefix(value, "0x") || g_str_has_prefix(value, "0X") ?
        value + 2 : value;
    if (strlen(p) != 16) {
        error_setg(errp, "flash-uid must be exactly 16 hexadecimal digits");
        return;
    }
    for (i = 0; i < 16; i++) {
        if (!g_ascii_isxdigit(p[i])) {
            error_setg(errp, "invalid flash-uid '%s'", value);
            return;
        }
    }
    if (qemu_strtou64(p, NULL, 16, &uid) < 0) {
        error_setg(errp, "invalid flash-uid '%s'", value);
        return;
    }

    g_free(s->flash_uid);
    s->flash_uid = g_strdup(value);
    s->flash_uid_value = uid;
    s->flash_uid_set = true;
}

static char *raspi_pico_get_rosc_random_seed(Object *obj, Error **errp)
{
    RaspiPicoMachineState *s = RASPI_PICO_MACHINE(obj);

    return g_strdup(s->rosc_random_seed ?: "");
}

static void raspi_pico_set_rosc_random_seed(Object *obj, const char *value,
                                            Error **errp)
{
    RaspiPicoMachineState *s = RASPI_PICO_MACHINE(obj);
    uint64_t seed;

    if (!value || !*value) {
        g_free(s->rosc_random_seed);
        s->rosc_random_seed = NULL;
        s->rosc_random_seed_value = 0;
        s->rosc_random_seed_set = false;
        return;
    }

    if (qemu_strtou64(value, NULL, 0, &seed) < 0) {
        error_setg(errp, "invalid ROSC random seed '%s'", value);
        return;
    }

    g_free(s->rosc_random_seed);
    s->rosc_random_seed = g_strdup(value);
    s->rosc_random_seed_value = seed;
    s->rosc_random_seed_set = true;
}

static void raspi_pico_init(MachineState *machine)
{
    RaspiPicoMachineState *s = RASPI_PICO_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();

    object_initialize_child(OBJECT(machine), "soc", &s->soc, TYPE_RP2040);
    qdev_prop_set_chr(DEVICE(&s->soc), "serial0", serial_hd(0));
    qdev_prop_set_chr(DEVICE(&s->soc), "serial1", serial_hd(1));
    qdev_prop_set_bit(DEVICE(&s->soc), "strict-uart-pins",
                      s->strict_uart_pins);
    qdev_prop_set_uint64(DEVICE(&s->soc.rosc), "random-seed",
                         s->rosc_random_seed_value);
    qdev_prop_set_bit(DEVICE(&s->soc.rosc), "random-seed-set",
                      s->rosc_random_seed_set);
    /*
     * BOOTSEL is not pressed by default on a Pico board, so the mask ROM sees
     * the QSPI SS input deasserted and tries to boot from external flash.
     */
    qdev_prop_set_uint32(DEVICE(&s->soc.sio), "gpio-hi-in", 1u << 1);
    if (s->flash_file) {
        qdev_prop_set_string(DEVICE(&s->soc.xip), "flash-file",
                             s->flash_file);
    }
    if (s->flash_uid_set) {
        qdev_prop_set_uint64(DEVICE(&s->soc.xip), "flash-uid",
                             s->flash_uid_value);
    }
    if (machine->firmware) {
        qdev_prop_set_string(DEVICE(&s->soc), "bootrom-file",
                             machine->firmware);
    }
    object_property_set_link(OBJECT(&s->soc), "memory",
                             OBJECT(system_memory), &error_fatal);

    sysbus_realize(SYS_BUS_DEVICE(&s->soc), &error_fatal);

    /*
     * For now, -kernel images are loaded directly into the XIP window.
     * rp2040_xip_load_image() accepts ELF images first, then raw images.
     */
    rp2040_xip_load_image(&s->soc.xip, machine->kernel_filename,
                          &error_fatal);
    armv7m_load_kernel(s->soc.armv7m[0].cpu, NULL, RP2040_XIP_BASE, 2 * MiB);
    rp2040_xip_set_writable(&s->soc.xip, false);
}

static void raspi_pico_machine_finalize(Object *obj)
{
    RaspiPicoMachineState *s = RASPI_PICO_MACHINE(obj);

    g_free(s->flash_file);
    g_free(s->flash_uid);
    g_free(s->rosc_random_seed);
}

static bool raspi_pico_get_strict_uart_pins(Object *obj, Error **errp)
{
    RaspiPicoMachineState *s = RASPI_PICO_MACHINE(obj);

    return s->strict_uart_pins;
}

static void raspi_pico_set_strict_uart_pins(Object *obj, bool value,
                                            Error **errp)
{
    RaspiPicoMachineState *s = RASPI_PICO_MACHINE(obj);

    s->strict_uart_pins = value;
}

static void raspi_pico_machine_initfn(Object *obj)
{
    RaspiPicoMachineState *s = RASPI_PICO_MACHINE(obj);

    s->strict_uart_pins = true;
}

static void raspi_pico_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Raspberry Pi Pico (Cortex-M0+)";
    mc->init = raspi_pico_init;
    mc->default_cpus = RP2040_NUM_CORES;
    mc->min_cpus = RP2040_NUM_CORES;
    mc->max_cpus = RP2040_NUM_CORES;
    mc->no_parallel = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;

    object_class_property_add_str(oc, "flash-file",
                                  raspi_pico_get_flash_file,
                                  raspi_pico_set_flash_file);
    object_class_property_set_description(oc, "flash-file",
                                          "Load initial XIP flash contents "
                                          "from a raw host file");
    object_class_property_add_str(oc, "flash-uid",
                                  raspi_pico_get_flash_uid,
                                  raspi_pico_set_flash_uid);
    object_class_property_set_description(oc, "flash-uid",
                                          "Set the emulated external flash "
                                          "64-bit unique ID as 16 hex digits");
    object_class_property_add_str(oc, "rosc-random-seed",
                                  raspi_pico_get_rosc_random_seed,
                                  raspi_pico_set_rosc_random_seed);
    object_class_property_set_description(oc, "rosc-random-seed",
                                          "Use a deterministic seed for the "
                                          "ROSC RANDOMBIT stream; if unset, "
                                          "QEMU guest entropy is used");
    object_class_property_add_bool(oc, "strict-uart-pins",
                                   raspi_pico_get_strict_uart_pins,
                                   raspi_pico_set_strict_uart_pins);
    object_class_property_set_description(oc, "strict-uart-pins",
                                          "Require GPIO0/GPIO1 IO_BANK0 "
                                          "FUNCSEL=UART before UART0 reaches "
                                          "the host serial backend");
}

static const TypeInfo raspi_pico_machine_info = {
    .name = TYPE_RASPI_PICO_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(RaspiPicoMachineState),
    .instance_init = raspi_pico_machine_initfn,
    .class_init = raspi_pico_machine_class_init,
    .instance_finalize = raspi_pico_machine_finalize,
    .interfaces = arm_machine_interfaces,
};

static void raspi_pico_machine_init(void)
{
    type_register_static(&raspi_pico_machine_info);
}
type_init(raspi_pico_machine_init)
