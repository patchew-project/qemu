/*
 * SmartFusion2 SoC emulation.
 *
 * Copyright (c) 2017 Subbaraya Sundeep <sundeep.lkml@gmail.com>
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
#include "qemu-common.h"
#include "hw/arm/arm.h"
#include "exec/address-spaces.h"
#include "hw/char/serial.h"
#include "hw/boards.h"
#include "sysemu/block-backend.h"
#include "hw/arm/msf2-soc.h"

#define MSF2_TIMER_BASE       0x40004000
#define MSF2_SYSREG_BASE      0x40038000

#define MSF2_TIMER_IRQ0       14
#define MSF2_TIMER_IRQ1       15

static const uint32_t spi_addr[MSF2_NUM_SPIS] = { 0x40001000 , 0x40011000 };
static const uint32_t uart_addr[MSF2_NUM_UARTS] = { 0x40000000 , 0x40010000 };

static const int spi_irq[MSF2_NUM_SPIS] = { 2, 3 };
static const int uart_irq[MSF2_NUM_UARTS] = { 10, 11 };

static void msf2_soc_initfn(Object *obj)
{
    MSF2State *s = MSF2_SOC(obj);
    int i;

    object_initialize(&s->armv7m, sizeof(s->armv7m), TYPE_ARMV7M);
    qdev_set_parent_bus(DEVICE(&s->armv7m), sysbus_get_default());

    object_initialize(&s->sysreg, sizeof(s->sysreg), TYPE_MSF2_SYSREG);
    qdev_set_parent_bus(DEVICE(&s->sysreg), sysbus_get_default());

    object_initialize(&s->timer, sizeof(s->timer), TYPE_MSF2_TIMER);
    qdev_set_parent_bus(DEVICE(&s->timer), sysbus_get_default());

    for (i = 0; i < MSF2_NUM_SPIS; i++) {
        object_initialize(&s->spi[i], sizeof(s->spi[i]),
                          TYPE_MSF2_SPI);
        qdev_set_parent_bus(DEVICE(&s->spi[i]), sysbus_get_default());
    }
}

static void msf2_soc_realize(DeviceState *dev_soc, Error **errp)
{
    MSF2State *s = MSF2_SOC(dev_soc);
    DeviceState *dev, *armv7m;
    SysBusDevice *busdev;
    Error *err = NULL;
    int i;

    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *nvm = g_new(MemoryRegion, 1);
    MemoryRegion *nvm_alias = g_new(MemoryRegion, 1);
    MemoryRegion *sram = g_new(MemoryRegion, 1);
    MemoryRegion *ddr = g_new(MemoryRegion, 1);

    memory_region_init_ram(nvm, NULL, "MSF2.envm", ENVM_SIZE,
                           &error_fatal);
    memory_region_init_alias(nvm_alias, NULL, "MSF2.flash.alias",
                             nvm, 0, ENVM_SIZE);
    vmstate_register_ram_global(nvm);

    memory_region_set_readonly(nvm, true);
    memory_region_set_readonly(nvm_alias, true);

    memory_region_add_subregion(system_memory, ENVM_BASE_ADDRESS, nvm);
    memory_region_add_subregion(system_memory, 0, nvm_alias);

    memory_region_init_ram(ddr, NULL, "MSF2.ddr", DDR_SIZE,
                           &error_fatal);
    vmstate_register_ram_global(ddr);
    memory_region_add_subregion(system_memory, DDR_BASE_ADDRESS, ddr);

    memory_region_init_ram(sram, NULL, "MSF2.sram", SRAM_SIZE,
                           &error_fatal);
    vmstate_register_ram_global(sram);
    memory_region_add_subregion(system_memory, SRAM_BASE_ADDRESS, sram);

    armv7m = DEVICE(&s->armv7m);
    qdev_prop_set_uint32(armv7m, "num-irq", 96);
    qdev_prop_set_string(armv7m, "cpu-model", "cortex-m3");
    object_property_set_link(OBJECT(&s->armv7m), OBJECT(get_system_memory()),
                                     "memory", &error_abort);
    object_property_set_bool(OBJECT(&s->armv7m), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }

    for (i = 0; i < MSF2_NUM_UARTS; i++) {
        if (serial_hds[i]) {
            serial_mm_init(get_system_memory(), uart_addr[i], 2,
                           qdev_get_gpio_in(armv7m, uart_irq[i]),
                           115200, serial_hds[i], DEVICE_NATIVE_ENDIAN);
        }
    }

    dev = DEVICE(&s->timer);
    qdev_prop_set_uint32(dev, "clock-frequency", MSF2_TIMER_FREQ);
    object_property_set_bool(OBJECT(&s->timer), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(busdev, 0, MSF2_TIMER_BASE);
    sysbus_connect_irq(busdev, 0,
                           qdev_get_gpio_in(armv7m, MSF2_TIMER_IRQ0));
    sysbus_connect_irq(busdev, 1,
                           qdev_get_gpio_in(armv7m, MSF2_TIMER_IRQ1));

    dev = DEVICE(&s->sysreg);
    object_property_set_bool(OBJECT(&s->sysreg), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(busdev, 0, MSF2_SYSREG_BASE);

    for (i = 0; i < MSF2_NUM_SPIS; i++) {
        gchar *bus_name = g_strdup_printf("spi%d", i);

        object_property_set_bool(OBJECT(&s->spi[i]), true, "realized", &err);
        if (err != NULL) {
            g_free(bus_name);
            error_propagate(errp, err);
            return;
        }

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->spi[i]), 0, spi_addr[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->spi[i]), 0,
                           qdev_get_gpio_in(armv7m, spi_irq[i]));

        /* Alias controller SPI bus to the SoC itself */
        object_property_add_alias(OBJECT(s), bus_name,
                                  OBJECT(&s->spi[i]), "spi0",
                                  &error_abort);
        g_free(bus_name);
    }
}

static void msf2_soc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = msf2_soc_realize;
}

static const TypeInfo msf2_soc_info = {
    .name          = TYPE_MSF2_SOC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MSF2State),
    .instance_init = msf2_soc_initfn,
    .class_init    = msf2_soc_class_init,
};

static void msf2_soc_types(void)
{
    type_register_static(&msf2_soc_info);
}

type_init(msf2_soc_types)
