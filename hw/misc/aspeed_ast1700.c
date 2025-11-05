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

#define AST2700_SOC_LTPI_SIZE        0x01000000
static void aspeed_ast1700_realize(DeviceState *dev, Error **errp)
{
    AspeedAST1700SoCState *s = ASPEED_AST1700(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    /* Occupy memory space for all controllers in AST1700 */
    memory_region_init(&s->iomem, OBJECT(s), TYPE_ASPEED_AST1700,
                       AST2700_SOC_LTPI_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);

}

static void aspeed_ast1700_instance_init(Object *obj)
{
    return;
}

static void aspeed_ast1700_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = aspeed_ast1700_realize;
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
