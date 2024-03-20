/*
 * STM32G000 SoC
 *
 * Copyright (c) 2024 Felipe Balbi <felipe@balbi.sh>
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
#include "qemu/module.h"
#include "hw/arm/boot.h"
#include "exec/address-spaces.h"
#include "hw/arm/stm32g000_soc.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-clock.h"
#include "hw/misc/unimp.h"
#include "sysemu/sysemu.h"

/* stm32g000_soc implementation is derived from stm32f100_soc */

struct stm32g0_ip_config {
    const char  *name;
    uint32_t    addr;
    uint32_t    irq;
};

#define STM32G0_DEFINE_IP(n, a, i)    \
{                                     \
    .name = (n),                      \
    .addr = (a),                      \
    .irq = (i),                       \
}

static const struct stm32g0_ip_config usart_config[STM_NUM_USARTS] = {
    STM32G0_DEFINE_IP("USART1", 0x40013800, 27),
    STM32G0_DEFINE_IP("USART2", 0x40004000, 28),
    STM32G0_DEFINE_IP("USART3", 0x40004400, 29),
    STM32G0_DEFINE_IP("USART4", 0x40004800, 29),
    STM32G0_DEFINE_IP("USART5", 0x40004c00, 29),
    STM32G0_DEFINE_IP("USART6", 0x40005000, 29),
    STM32G0_DEFINE_IP("LPUSART1", 0x40008000, 29),
    STM32G0_DEFINE_IP("LPUSART2", 0x40008400, 28),
};

static const struct stm32g0_ip_config spi_config[STM_NUM_SPIS] = {
    STM32G0_DEFINE_IP("SPI1", 0x40013000, 25),
    STM32G0_DEFINE_IP("SPI2", 0x40003800, 26),

    /* Only on STM32G0B1xx and STM32G0C1xx */
    /* STM32G0_DEFINE_IP("SPI3", 0x4003c000, 26), */
};

static void stm32g000_soc_initfn(Object *obj)
{
    STM32G000State *s = STM32G000_SOC(obj);
    int i;

    object_initialize_child(obj, "armv7m", &s->armv7m, TYPE_ARMV7M);

    for (i = 0; i < STM_NUM_USARTS; i++) {
        object_initialize_child(obj, "usart[*]", &s->usart[i],
                                TYPE_STM32F2XX_USART);
    }

    for (i = 0; i < STM_NUM_SPIS; i++) {
        object_initialize_child(obj, "spi[*]", &s->spi[i], TYPE_STM32F2XX_SPI);
    }

    s->sysclk = qdev_init_clock_in(DEVICE(s), "sysclk", NULL, NULL, 0);
    s->refclk = qdev_init_clock_in(DEVICE(s), "refclk", NULL, NULL, 0);
}

static void stm32g000_soc_realize(DeviceState *dev_soc, Error **errp)
{
    STM32G000State *s = STM32G000_SOC(dev_soc);
    DeviceState *dev, *armv7m;
    SysBusDevice *busdev;

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

    /*
     * Init flash region
     * Flash starts at 0x08000000 and then is aliased to boot memory at 0x0
     */
    memory_region_init_rom(&s->flash, OBJECT(dev_soc), "STM32G000.flash",
                           FLASH_SIZE, &error_fatal);
    memory_region_init_alias(&s->flash_alias, OBJECT(dev_soc),
                             "STM32G000.flash.alias", &s->flash, 0, FLASH_SIZE);
    memory_region_add_subregion(system_memory, FLASH_BASE_ADDRESS, &s->flash);
    memory_region_add_subregion(system_memory, 0, &s->flash_alias);

    /* Init SRAM region */
    memory_region_init_ram(&s->sram, NULL, "STM32G000.sram", SRAM_SIZE,
                           &error_fatal);
    memory_region_add_subregion(system_memory, SRAM_BASE_ADDRESS, &s->sram);

    /* Init ARMv7m */
    armv7m = DEVICE(&s->armv7m);
    qdev_prop_set_uint32(armv7m, "num-irq", 32);
    qdev_prop_set_uint8(armv7m, "num-prio-bits", 2);
    qdev_prop_set_string(armv7m, "cpu-type", ARM_CPU_TYPE_NAME("cortex-m0"));
    qdev_prop_set_bit(armv7m, "enable-bitband", true);
    qdev_connect_clock_in(armv7m, "cpuclk", s->sysclk);
    qdev_connect_clock_in(armv7m, "refclk", s->refclk);
    object_property_set_link(OBJECT(&s->armv7m), "memory",
                             OBJECT(get_system_memory()), &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->armv7m), errp)) {
        return;
    }

    /* Attach UART (uses USART registers) and USART controllers */
    for (unsigned i = 0; i < STM_NUM_USARTS; i++) {
        dev = DEVICE(&(s->usart[i]));
        qdev_prop_set_chr(dev, "chardev", serial_hd(i));
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->usart[i]), errp)) {
            return;
        }
        busdev = SYS_BUS_DEVICE(dev);
        sysbus_mmio_map(busdev, 0, usart_config[i].addr);
        sysbus_connect_irq(busdev,
                           0,
                           qdev_get_gpio_in(armv7m, usart_config[i].irq));
    }

    /*
     * SPI 1 and 2
     *
     * REVISIT: STM32G0B1xx and STM32G0C1xx have a 3rd SPI
     */
    for (unsigned i = 0; i < STM_NUM_SPIS; i++) {
        dev = DEVICE(&(s->spi[i]));
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->spi[i]), errp)) {
            return;
        }
        busdev = SYS_BUS_DEVICE(dev);
        sysbus_mmio_map(busdev, 0, spi_config[i].addr);
        sysbus_connect_irq(busdev,
                           0,
                           qdev_get_gpio_in(armv7m, spi_config[i].irq));
    }

    /* Review addresses */
    create_unimplemented_device("timer[2]",  0x40000000, 0x400);
    create_unimplemented_device("timer[3]",  0x40000400, 0x400);
    create_unimplemented_device("timer[4]",  0x40000800, 0x400);
    create_unimplemented_device("timer[6]",  0x40001000, 0x400);
    create_unimplemented_device("timer[7]",  0x40001400, 0x400);
    create_unimplemented_device("RTC",       0x40002800, 0x400);
    create_unimplemented_device("WWDG",      0x40002c00, 0x400);
    create_unimplemented_device("IWDG",      0x40003000, 0x400);
    create_unimplemented_device("USB",       0x40005000, 0x400);
    create_unimplemented_device("FDCAN1",    0x40006400, 0x400);
    create_unimplemented_device("FDCAN2",    0x40006800, 0x400);
    create_unimplemented_device("CRS",       0x40006c00, 0x400);
    create_unimplemented_device("PWR",       0x40007000, 0x400);
    create_unimplemented_device("DAC",       0x40007400, 0x400);
    create_unimplemented_device("CEC",       0x40007800, 0x400);
    create_unimplemented_device("LPTIM1",    0x40007c00, 0x400);
    create_unimplemented_device("LPUART1",   0x40008000, 0x400);
    create_unimplemented_device("LPUART2",   0x40008400, 0x400);
    create_unimplemented_device("I2C3",      0x40008800, 0x400);
    create_unimplemented_device("LPTIM2",    0x40009400, 0x400);
    create_unimplemented_device("USB RAM1",  0x40009800, 0x400);
    create_unimplemented_device("USB RAM2",  0x40009c00, 0x400);
    create_unimplemented_device("UCPD1",     0x4000a000, 0x400);
    create_unimplemented_device("UCPD2",     0x4000a400, 0x400);
    create_unimplemented_device("TAMP",      0x4000b000, 0x400);
    create_unimplemented_device("FDCAN",     0x4000b400, 0x800);
    create_unimplemented_device("ADC",       0x40012400, 0x400);
    create_unimplemented_device("timer[1]",  0x40012C00, 0x400);
    create_unimplemented_device("timer[15]", 0x40014000, 0x400);
    create_unimplemented_device("timer[16]", 0x40014400, 0x400);
    create_unimplemented_device("timer[17]", 0x40014800, 0x400);
    create_unimplemented_device("DMA1",      0x40020000, 0x400);
    create_unimplemented_device("DMA2",      0x40020400, 0x400);
    create_unimplemented_device("DMAMUX",    0x40020800, 0x800);
    create_unimplemented_device("RCC",       0x40021000, 0x400);
    create_unimplemented_device("EXTI",      0x40021800, 0x400);
    create_unimplemented_device("FLASH",     0x40022000, 0x400);
    create_unimplemented_device("CRC",       0x40023000, 0x400);
    create_unimplemented_device("RNG",       0x40025000, 0x400);
    create_unimplemented_device("AES",       0x40026000, 0x400);
    create_unimplemented_device("GPIOA",     0x50000000, 0x400);
    create_unimplemented_device("GPIOB",     0x50000400, 0x400);
    create_unimplemented_device("GPIOC",     0x50000800, 0x400);
    create_unimplemented_device("GPIOD",     0x50000c00, 0x400);
    create_unimplemented_device("GPIOE",     0x50001000, 0x400);
    create_unimplemented_device("GPIOF",     0x50001400, 0x400);
}

static void stm32g000_soc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = stm32g000_soc_realize;
    /* No vmstate or reset required: device has no internal state */
}

static const TypeInfo stm32g000_soc_info = {
    .name          = TYPE_STM32G000_SOC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(STM32G000State),
    .instance_init = stm32g000_soc_initfn,
    .class_init    = stm32g000_soc_class_init,
};

static void stm32g000_soc_types(void)
{
    type_register_static(&stm32g000_soc_info);
}

type_init(stm32g000_soc_types)
