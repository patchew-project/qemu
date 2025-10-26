/* SPDX-License-Identifier: GPL-2.0-or-later */

/*
 * Copyright (c) liang yan <yanl1229@rt-thread.org>
 * Copyright (c) Yihao Fan <fanyihao@rt-thread.org>
 * The reference used is the STMicroElectronics RM0090 Reference manual
 * stm32f4spark implementation is derived from netduinoplus2
 * https://github.com/RT-Thread-Studio/sdk-bsp-stm32f407-spark
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-clock.h"
#include "qemu/error-report.h"
#include "hw/arm/stm32f407_soc.h"
#include "hw/arm/boot.h"


/* Main SYSCLK frequency in Hz (72MHz) */
#define SYSCLK_FRQ 72000000ULL


static void stm32f4spark_init(MachineState *machine)
{
    DeviceState *dev;
    Clock *sysclk;

    /* This clock doesn't need migration because it is fixed-frequency */
    sysclk = clock_new(OBJECT(machine), "SYSCLK");
    clock_set_hz(sysclk, SYSCLK_FRQ);

    dev = qdev_new(TYPE_STM32F407_SOC);
    object_property_add_child(OBJECT(machine), "soc", OBJECT(dev));
    qdev_connect_clock_in(dev, "sysclk", sysclk);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    armv7m_load_kernel(ARM_CPU(first_cpu),
                       machine->kernel_filename,
                       0, FLASH_SIZE);
}

static void stm32f4spark_machine_init(MachineClass *mc)
{
    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-m4"),
        NULL
    };

    mc->desc = "ST RT-spark (Cortex-M4)";
    mc->init = stm32f4spark_init;
    mc->valid_cpu_types = valid_cpu_types;
}

DEFINE_MACHINE("rt-spark", stm32f4spark_machine_init)
