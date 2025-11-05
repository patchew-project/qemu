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

enum {
    ASPEED_AST1700_DEV_SPI0,
    ASPEED_AST1700_DEV_SRAM,
    ASPEED_AST1700_DEV_ADC,
    ASPEED_AST1700_DEV_SCU,
    ASPEED_AST1700_DEV_UART12,
    ASPEED_AST1700_DEV_LTPI_CTRL,
    ASPEED_AST1700_DEV_SPI0_MEM,
};

static const hwaddr aspeed_ast1700_io_memmap[] = {
    [ASPEED_AST1700_DEV_SPI0]      =  0x00030000,
    [ASPEED_AST1700_DEV_SRAM]      =  0x00BC0000,
    [ASPEED_AST1700_DEV_ADC]       =  0x00C00000,
    [ASPEED_AST1700_DEV_SCU]       =  0x00C02000,
    [ASPEED_AST1700_DEV_UART12]    =  0x00C33B00,
    [ASPEED_AST1700_DEV_LTPI_CTRL] =  0x00C34000,
    [ASPEED_AST1700_DEV_SPI0_MEM]  =  0x04000000,
};
static void aspeed_ast1700_realize(DeviceState *dev, Error **errp)
{
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

    /* LTPI controller */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->ltpi), errp)) {
        return;
    }
    memory_region_add_subregion(&s->iomem,
                        aspeed_ast1700_io_memmap[ASPEED_AST1700_DEV_LTPI_CTRL],
                        sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->ltpi), 0));
}

static void aspeed_ast1700_instance_init(Object *obj)
{
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
    /* LTPI controller */
    object_initialize_child(obj, "ltpi-ctrl",
                            &s->ltpi, TYPE_ASPEED_LTPI);

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
