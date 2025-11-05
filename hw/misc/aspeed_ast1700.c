/*
 * ASPEED AST1700 IO Expander
 *
 * Copyright (C) 2025 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/boards.h"
#include "hw/qdev-core.h"
#include "qom/object.h"
#include "hw/qdev-properties.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/misc/aspeed_ast1700.h"

#define AST1700_BOARD1_MEM_ADDR      0x30000000
#define AST2700_SOC_LTPI_SIZE        0x01000000
#define AST1700_SOC_SRAM_SIZE        0x00040000
#define AST1700_SOC_I3C_SIZE         0x00010000
#define AST1700_SOC_SGPIOM_SIZE      0x00002000
#define AST1700_SOC_PWM_SIZE         0x00000200

enum {
    ASPEED_AST1700_DEV_SPI0,
    ASPEED_AST1700_DEV_PWM,
    ASPEED_AST1700_DEV_SRAM,
    ASPEED_AST1700_DEV_ADC,
    ASPEED_AST1700_DEV_SCU,
    ASPEED_AST1700_DEV_GPIO,
    ASPEED_AST1700_DEV_SGPIOM,
    ASPEED_AST1700_DEV_I2C,
    ASPEED_AST1700_DEV_I3C,
    ASPEED_AST1700_DEV_UART12,
    ASPEED_AST1700_DEV_LTPI_CTRL,
    ASPEED_AST1700_DEV_WDT,
    ASPEED_AST1700_DEV_SPI0_MEM,
};

static const hwaddr aspeed_ast1700_io_memmap[] = {
    [ASPEED_AST1700_DEV_SPI0]      =  0x00030000,
    [ASPEED_AST1700_DEV_PWM]       =  0x000C0000,
    [ASPEED_AST1700_DEV_SRAM]      =  0x00BC0000,
    [ASPEED_AST1700_DEV_ADC]       =  0x00C00000,
    [ASPEED_AST1700_DEV_SCU]       =  0x00C02000,
    [ASPEED_AST1700_DEV_GPIO]      =  0x00C0B000,
    [ASPEED_AST1700_DEV_SGPIOM]    =  0x00C0C000,
    [ASPEED_AST1700_DEV_I2C]       =  0x00C0F000,
    [ASPEED_AST1700_DEV_I3C]       =  0x00C20000,
    [ASPEED_AST1700_DEV_UART12]    =  0x00C33B00,
    [ASPEED_AST1700_DEV_LTPI_CTRL] =  0x00C34000,
    [ASPEED_AST1700_DEV_WDT]       =  0x00C37000,
    [ASPEED_AST1700_DEV_SPI0_MEM]  =  0x04000000,
};

static void aspeed_ast1700_realize(DeviceState *dev, Error **errp)
{
    int i;
    AspeedAST1700SoCState *s = ASPEED_AST1700(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    hwaddr uart_base;
    Error *err = NULL;
    int board_idx;
    char sram_name[32];

    if (s->mapped_base == AST1700_BOARD1_MEM_ADDR) {
        board_idx = 0;
    } else {
        board_idx = 1;
    }

    /* Occupy memory space for all controllers in AST1700 */
    memory_region_init(&s->iomem, OBJECT(s), TYPE_ASPEED_AST1700,
                       AST2700_SOC_LTPI_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);

    /* SRAM */
    snprintf(sram_name, sizeof(sram_name), "aspeed.ioexp-sram.%d", board_idx);
    memory_region_init_ram(&s->sram, OBJECT(s), sram_name,
                           AST1700_SOC_SRAM_SIZE, &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }
    memory_region_add_subregion(&s->iomem,
                                aspeed_ast1700_io_memmap[ASPEED_AST1700_DEV_SRAM],
                                &s->sram);

    /* UART */
    uart_base = s->mapped_base +
               aspeed_ast1700_io_memmap[ASPEED_AST1700_DEV_UART12];
    qdev_prop_set_uint8(DEVICE(&s->uart), "regshift", 2);
    qdev_prop_set_uint32(DEVICE(&s->uart), "baudbase", 38400);
    qdev_set_legacy_instance_id(DEVICE(&s->uart), uart_base, 2);
    qdev_prop_set_uint8(DEVICE(&s->uart), "endianness", DEVICE_LITTLE_ENDIAN);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->uart), errp)) {
        return;
    }
    memory_region_add_subregion(&s->iomem,
                        aspeed_ast1700_io_memmap[ASPEED_AST1700_DEV_UART12],
                        sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->uart), 0));

    /* SPI */
    object_property_set_link(OBJECT(&s->spi), "dram",
                             OBJECT(&s->iomem), &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->spi), errp)) {
        return;
    }
    memory_region_add_subregion(&s->iomem,
                        aspeed_ast1700_io_memmap[ASPEED_AST1700_DEV_SPI0],
                        sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->spi), 0));

    memory_region_add_subregion(&s->iomem,
                        aspeed_ast1700_io_memmap[ASPEED_AST1700_DEV_SPI0_MEM],
                        sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->spi), 1));

    /* ADC */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->adc), errp)) {
        return;
    }
    memory_region_add_subregion(&s->iomem,
                        aspeed_ast1700_io_memmap[ASPEED_AST1700_DEV_ADC],
                        sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->adc), 0));

    /* SCU */
    qdev_prop_set_uint32(DEVICE(&s->scu), "silicon-rev",
                         s->silicon_rev);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->scu), errp)) {
        return;
    }
    memory_region_add_subregion(&s->iomem,
                        aspeed_ast1700_io_memmap[ASPEED_AST1700_DEV_SCU],
                        sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->scu), 0));

    /* GPIO */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gpio), errp)) {
        return;
    }
    memory_region_add_subregion(&s->iomem,
                        aspeed_ast1700_io_memmap[ASPEED_AST1700_DEV_GPIO],
                        sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->gpio), 0));

    /* I2C */
    object_property_set_link(OBJECT(&s->i2c), "dram",
                             OBJECT(&s->iomem), &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->i2c), errp)) {
        return;
    }
    memory_region_add_subregion(&s->iomem,
                        aspeed_ast1700_io_memmap[ASPEED_AST1700_DEV_I2C],
                        sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->i2c), 0));

    /* I3C */
    qdev_prop_set_string(DEVICE(&s->i3c), "name", "ioexp-i3c");
    qdev_prop_set_uint64(DEVICE(&s->i3c), "size", AST1700_SOC_I3C_SIZE);
    sysbus_realize(SYS_BUS_DEVICE(&s->i3c), errp);
    memory_region_add_subregion_overlap(&s->iomem,
                        aspeed_ast1700_io_memmap[ASPEED_AST1700_DEV_I3C],
                        sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->i3c), 0),
                        -1000);

    /* LTPI controller */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->ltpi), errp)) {
        return;
    }
    memory_region_add_subregion(&s->iomem,
                        aspeed_ast1700_io_memmap[ASPEED_AST1700_DEV_LTPI_CTRL],
                        sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->ltpi), 0));

    /* SGPIOM */
    qdev_prop_set_string(DEVICE(&s->sgpiom), "name", "ioexp-sgpiom");
    qdev_prop_set_uint64(DEVICE(&s->sgpiom), "size", AST1700_SOC_SGPIOM_SIZE);
    sysbus_realize(SYS_BUS_DEVICE(&s->sgpiom), errp);
    memory_region_add_subregion_overlap(&s->iomem,
                        aspeed_ast1700_io_memmap[ASPEED_AST1700_DEV_SGPIOM],
                        sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->sgpiom), 0),
                        -1000);

    /* PWM */
    qdev_prop_set_string(DEVICE(&s->pwm), "name", "ioexp-pwm");
    qdev_prop_set_uint64(DEVICE(&s->pwm), "size", AST1700_SOC_PWM_SIZE);
    sysbus_realize(SYS_BUS_DEVICE(&s->pwm), errp);
    memory_region_add_subregion_overlap(&s->iomem,
                        aspeed_ast1700_io_memmap[ASPEED_AST1700_DEV_PWM],
                        sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->pwm), 0),
                        -1000);

    /* WDT */
    for (i = 0; i < AST1700_WDT_NUM; i++) {
        AspeedWDTClass *awc = ASPEED_WDT_GET_CLASS(&s->wdt[i]);
        hwaddr wdt_offset = aspeed_ast1700_io_memmap[ASPEED_AST1700_DEV_WDT] +
                            i * awc->iosize;

        object_property_set_link(OBJECT(&s->wdt[i]), "scu", OBJECT(&s->scu),
                                 &error_abort);
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->wdt[i]), errp)) {
            return;
        }
        memory_region_add_subregion(&s->iomem,
                        wdt_offset,
                        sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->wdt[i]), 0));
    }
}

static void aspeed_ast1700_instance_init(Object *obj)
{
    int i;
    AspeedAST1700SoCState *s = ASPEED_AST1700(obj);
    char socname[8];
    char typename[64];

    if (sscanf(object_get_typename(obj), "aspeed.ast1700-%7s", socname) != 1) {
        g_assert_not_reached();
    }

    /* UART */
    object_initialize_child(obj, "uart[*]", &s->uart,
                            TYPE_SERIAL_MM);

    /* SPI */
    snprintf(typename, sizeof(typename), "aspeed.spi%d-%s", 0, socname);
    object_initialize_child(obj, "ioexp-spi[*]", &s->spi,
                            typename);

    /* ADC */
    snprintf(typename, sizeof(typename), "aspeed.adc-%s", socname);
    object_initialize_child(obj, "ioexp-adc[*]", &s->adc,
                            typename);

    /* SCU */
    object_initialize_child(obj, "ioexp-scu[*]", &s->scu,
                            TYPE_ASPEED_2700_SCU);

    /* GPIO */
    snprintf(typename, sizeof(typename), "aspeed.gpio-%s", socname);
    object_initialize_child(obj, "ioexp-gpio[*]", &s->gpio, typename);

    /* I2C */
    snprintf(typename, sizeof(typename), "aspeed.i2c-%s", socname);
    object_initialize_child(obj, "ioexp-i2c[*]", &s->i2c,
                            typename);

    /* I3C */
    object_initialize_child(obj, "ioexp-i3c[*]", &s->i3c,
                            TYPE_UNIMPLEMENTED_DEVICE);

    /* LTPI controller */
    object_initialize_child(obj, "ltpi-ctrl",
                            &s->ltpi, TYPE_ASPEED_LTPI);

    /* SGPIOM */
    object_initialize_child(obj, "ioexp-sgpiom[*]", &s->sgpiom,
                            TYPE_UNIMPLEMENTED_DEVICE);

    /* PWM */
    object_initialize_child(obj, "ioexp-pwm", &s->pwm,
                            TYPE_UNIMPLEMENTED_DEVICE);

    /* WDT */
    for (i = 0; i < AST1700_WDT_NUM; i++) {
        snprintf(typename, sizeof(typename), "aspeed.wdt-%s", socname);
        object_initialize_child(obj, "ioexp-wdt[*]",
                                &s->wdt[i], typename);
    }

    return;
}

static const Property aspeed_ast1700_props[] = {
    DEFINE_PROP_UINT64("mapped-base", AspeedAST1700SoCState, mapped_base, 0),
    DEFINE_PROP_UINT32("silicon-rev", AspeedAST1700SoCState, silicon_rev, 0),
};

static void aspeed_ast1700_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = aspeed_ast1700_realize;
    device_class_set_props(dc, aspeed_ast1700_props);
}

static const TypeInfo aspeed_ast1700_info = {
    .name          = TYPE_ASPEED_AST1700,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AspeedAST1700SoCState),
    .class_init    = aspeed_ast1700_class_init,
    .instance_init = aspeed_ast1700_instance_init,
};

static const TypeInfo aspeed_ast1700_ast2700_info = {
    .name = TYPE_ASPEED_AST1700_AST2700,
    .parent = TYPE_ASPEED_AST1700,
};


static void aspeed_ast1700_register_types(void)
{
    type_register_static(&aspeed_ast1700_info);
    type_register_static(&aspeed_ast1700_ast2700_info);
}

type_init(aspeed_ast1700_register_types);
