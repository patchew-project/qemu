/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "system/address-spaces.h"
#include "system/system.h"
#include "hw/arm/stm32f407_soc.h"
#include "hw/qdev-clock.h"
#include "hw/sd/sd.h"
#include "hw/boards.h"
#include "qom/object.h"
#include "hw/block/flash.h"
#include "hw/misc/unimp.h"


static const uint32_t syscfg_addr = 0x40013800;
static const uint32_t exti_addr = 0x40013C00;
static const int syscfg_irq = 71;
static const int exti_irq[] = {
    6, 7, 8, 9, 10, 23, 23, 23, 23, 23, 40,
    40, 40, 40, 40, 40
};


static void stm32f407_soc_initfn(Object *obj)
{
    int i;

    STM32F407State *s = STM32F407_SOC(obj);

    object_initialize_child(obj, "armv7m", &s->armv7m, TYPE_ARMV7M);

    object_initialize_child(obj, "syscfg", &s->syscfg, TYPE_STM32F4XX_SYSCFG);
    object_initialize_child(obj, "exti", &s->exti, TYPE_STM32F4XX_EXTI);

    s->sysclk = qdev_init_clock_in(DEVICE(s), "sysclk", NULL, NULL, 0);
    s->refclk = qdev_init_clock_in(DEVICE(s), "refclk", NULL, NULL, 0);
}

static void stm32f407_soc_realize(DeviceState *dev_soc, Error **errp)
{
    STM32F407State *s = STM32F407_SOC(dev_soc);
    DeviceState *dev, *armv7m;
    SysBusDevice *busdev;
    DriveInfo *dinfo;
    int i, j;

    MemoryRegion *system_memory = get_system_memory();

    /*
     * We use s->refclk internally and only define it with qdev_init_clock_in()
     * so it is correctly parented and not leaked on an init/deinit; it is not
     * intended as an externally exposed clock.
     */
    if (clock_has_source(s->refclk)) {
        error_setg(errp, "refclk clock must not be wired up by the board code");
        return;
    }

    if (!clock_has_source(s->sysclk)) {
        error_setg(errp, "sysclk clock must be wired up by the board code");
        return;
    }

    /*
     * TODO: ideally we should model the SoC RCC and its ability to
     * change the sysclk frequency and define different sysclk sources.
     */

    /* The refclk always runs at frequency HCLK / 8 */
    clock_set_mul_div(s->refclk, 8, 1);
    clock_set_source(s->refclk, s->sysclk);


    armv7m = DEVICE(&s->armv7m);
    qdev_prop_set_uint32(armv7m, "num-irq", 98);
    qdev_prop_set_string(armv7m, "cpu-type", ARM_CPU_TYPE_NAME("cortex-m4"));
    qdev_prop_set_bit(armv7m, "enable-bitband", true);
    qdev_connect_clock_in(armv7m, "cpuclk", s->sysclk);
    qdev_connect_clock_in(armv7m, "refclk", s->refclk);
    object_property_set_link(OBJECT(&s->armv7m), "memory",
                             OBJECT(system_memory), &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->armv7m), errp)) {
        return;
    }
    /* System configuration controller */
    dev = DEVICE(&s->syscfg);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->syscfg), errp)) {
        return;
    }
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(busdev, 0, syscfg_addr);
    sysbus_connect_irq(busdev, 0, qdev_get_gpio_in(armv7m, syscfg_irq));

    /* EXTI device */
    dev = DEVICE(&s->exti);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->exti), errp)) {
        return;
    }
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(busdev, 0, exti_addr);
    for (i = 0; i < 16; i++) {
        sysbus_connect_irq(busdev, i, qdev_get_gpio_in(armv7m, exti_irq[i]));
    }
    for (i = 0; i < 16; i++) {
        qdev_connect_gpio_out(DEVICE(&s->syscfg), i, qdev_get_gpio_in(dev, i));
    }

}

static void stm32f407_soc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = stm32f407_soc_realize;
}

static const TypeInfo stm32f407_soc_info = {
    .name          = TYPE_STM32F407_SOC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(STM32F407State),
    .instance_init = stm32f407_soc_initfn,
    .class_init    = stm32f407_soc_class_init,
};

static void stm32f407_soc_types(void)
{
    type_register_static(&stm32f407_soc_info);
}

type_init(stm32f407_soc_types)
