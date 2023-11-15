/*
 * B-L475E-IOT01A Discovery Kit machine
 * (B-L475E-IOT01A IoT Node)
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2023 Arnaud Minier <arnaud.minier@telecom-paris.fr>
 * Copyright (c) 2023 Ines Varhol <ines.varhol@telecom-paris.fr>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * Heavily inspired by the netduinoplus2 by Alistair Francis.
 * The reference used is the STMicroElectronics UM2153 User manual
 * Discovery kit for IoT node, multi-channel communication with STM32L4.
 * https://www.st.com/en/evaluation-tools/b-l475e-iot01a.html#documentation
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-clock.h"
#include "qemu/error-report.h"
#include "hw/arm/stm32l4x5_soc.h"
#include "hw/arm/boot.h"

/* Main SYSCLK frequency in Hz (80MHz) */
#define SYSCLK_FRQ 80000000ULL

static void b_l475e_iot01a_init(MachineState *machine)
{
    const Stm32l4x5SocClass *sc;
    DeviceState *dev;
    Clock *sysclk;

    /* This clock doesn't need migration because it is fixed-frequency */
    sysclk = clock_new(OBJECT(machine), "SYSCLK");
    clock_set_hz(sysclk, SYSCLK_FRQ);

    dev = qdev_new(TYPE_STM32L4X5XG_SOC);
    sc = STM32L4X5_SOC_GET_CLASS(dev);
    qdev_connect_clock_in(dev, "sysclk", sysclk);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    armv7m_load_kernel(ARM_CPU(first_cpu),
                       machine->kernel_filename,
                       0, sc->flash_size);
}

static void b_l475e_iot01a_machine_init(MachineClass *mc)
{
    static const char *machine_valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-m4"),
        NULL};
    mc->desc = "B-L475E-IOT01A Discovery Kit (Cortex-M4)";
    mc->init = b_l475e_iot01a_init;
    mc->valid_cpu_types = machine_valid_cpu_types;

    /* SRAM pre-allocated as part of the SoC instantiation */
    mc->default_ram_size = 0;
}

DEFINE_MACHINE("b-l475e-iot01a", b_l475e_iot01a_machine_init)
