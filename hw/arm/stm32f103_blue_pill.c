/*
 * STM32F103C8 Blue Pill development board Machine Model
 *
 * Copyright (c) 2018 Priit Laes <plaes@plaes.org>
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
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/arm/arm.h"
#include "exec/address-spaces.h"

#include "hw/arm/stm32f103_soc.h"

typedef struct {
    MachineState parent;

    STM32F103State stm32f103;
} STM32BluePillMachineState;

#define TYPE_STM32BLUEPILL_MACHINE MACHINE_TYPE_NAME("stm32bluepill")

#define STM32BLUEPILL_MACHINE(obj) \
    OBJECT_CHECK(STM32BluePillMachineState, obj, TYPE_STM32BLUEPILL_MACHINE)

static void stm32bluepill_init(MachineState *machine)
{
    STM32BluePillMachineState *s = STM32BLUEPILL_MACHINE(machine);
    Object *soc = OBJECT(&s->stm32f103);

    sysbus_init_child_obj(OBJECT(machine), "stm32f103-soc", soc,
                          sizeof(s->stm32f103), TYPE_STM32F103_SOC);
    object_property_set_bool(soc, true, "realized", &error_fatal);

    armv7m_load_kernel(ARM_CPU(first_cpu), machine->kernel_filename,
                       FLASH_SIZE);
}

static void stm32bluepill_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "STM32F103 Blue Pill development board";
    mc->init = stm32bluepill_init;
    mc->max_cpus = 1;
}

static const TypeInfo stm32bluepill_info = {
    .name = TYPE_STM32BLUEPILL_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(STM32BluePillMachineState),
    .class_init = stm32bluepill_machine_class_init,
};

static void stm32bluepill_machine_init(void)
{
    type_register_static(&stm32bluepill_info);
}

type_init(stm32bluepill_machine_init);
