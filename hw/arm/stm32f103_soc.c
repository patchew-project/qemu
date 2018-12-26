/*
 * STM32 F103 SoC (or MCU)
 *
 * Copyright 2018 Priit Laes <plaes@plaes.org>
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "exec/address-spaces.h"

#include "hw/arm/stm32f103_soc.h"

#define FLASH_BASE      0x08000000
#define SRAM_BASE       0x20000000

static void stm32f103_soc_init(Object *obj)
{
    STM32F103State *s = STM32F103_SOC(obj);

    sysbus_init_child_obj(obj, "armv7m", &s->cpu, sizeof(s->cpu),
                      TYPE_ARMV7M);
}

static void stm32f103_soc_realize(DeviceState *dev_soc, Error **errp)
{
    STM32F103State *s = STM32F103_SOC(dev_soc);
    Error *err = NULL;

    /*
     * XXX: Region 0x1FFF F000 - 0x1FFF F7FF is called "System Memory"
     * containing boot loader used to reprogram flash by using USART1.
     */
    MemoryRegion *system_memory = get_system_memory();

    memory_region_init_rom(&s->flash, NULL, "stm32.flash", FLASH_SIZE,
                           &error_fatal);
    memory_region_add_subregion(system_memory, FLASH_BASE, &s->flash);
    /*
     * TODO: based on BOOT pin, 0x00000000 - 0x0007FFFF is aliased to
     * either Flash or system memory. We currently hardcode it to flash.
     */
    memory_region_init_alias(&s->flash_alias, NULL, "stm32.flash_alias",
                             &s->flash, 0, FLASH_SIZE);
    memory_region_add_subregion(system_memory, 0, &s->flash_alias);

    memory_region_init_ram(&s->sram, NULL, "stm32.sram", SRAM_SIZE,
                           &error_fatal);
    memory_region_add_subregion(system_memory, SRAM_BASE, &s->sram);

    qdev_prop_set_bit(DEVICE(&s->cpu), "enable-bitband", true);
    qdev_prop_set_uint32(DEVICE(&s->cpu), "num-irq", 80);
    qdev_prop_set_string(DEVICE(&s->cpu), "cpu-type", ARM_CPU_TYPE_NAME("cortex-m3"));

    object_property_set_link(OBJECT(&s->cpu), OBJECT(system_memory),
                                    "memory", &error_abort);
    object_property_set_bool(OBJECT(&s->cpu), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }
}

static Property stm32f103_soc_properties[] = {
    DEFINE_PROP_UINT32("flash-size", STM32F103State, flash_size, FLASH_SIZE),
    DEFINE_PROP_UINT32("sram-size", STM32F103State, sram_size, SRAM_SIZE),
    DEFINE_PROP_END_OF_LIST(),
};

static void stm32f103_soc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->props   = stm32f103_soc_properties;
    dc->realize = stm32f103_soc_realize;
}

static const TypeInfo stm32f103_soc_info = {
    .name          = TYPE_STM32F103_SOC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(STM32F103State),
    .instance_init = stm32f103_soc_init,
    .class_init    = stm32f103_soc_class_init,
};

static void stm32f103_soc_types(void)
{
    type_register_static(&stm32f103_soc_info);
}
type_init(stm32f103_soc_types)
