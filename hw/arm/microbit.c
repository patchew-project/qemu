/*
 * BBC micro:bit machine
 *
 * Copyright 2018 Joel Stanley <joel@jms.id.au>
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/boards.h"

#include "hw/arm/nrf51_soc.h"

static void microbit_init(MachineState *machine)
{
    DeviceState *dev;
    NRF51State *soc;

    dev = qdev_create(NULL, TYPE_NRF51_SOC);
    if (machine->kernel_filename) {
        qdev_prop_set_string(dev, "kernel-filename", machine->kernel_filename);
    }
    object_property_set_bool(OBJECT(dev), true, "realized", &error_fatal);

    soc = NRF51_SOC(dev);
    object_property_add_alias(OBJECT(machine), "button-a", OBJECT(soc->nvic),
            "unnamed-gpio-in[17]", &error_fatal);
    object_property_add_alias(OBJECT(machine), "button-b", OBJECT(soc->nvic),
            "unnamed-gpio-in[26]", &error_fatal);
}

static void microbit_machine_init(MachineClass *mc)
{
    mc->desc = "BBC micro:bit";
    mc->init = microbit_init;
    mc->ignore_memory_transaction_failures = true;
}
DEFINE_MACHINE("microbit", microbit_machine_init);
