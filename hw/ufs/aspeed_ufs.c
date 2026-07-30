/*
 * ASPEED AST2700 UFS Host Controller
 *
 * Sysbus frontend for the AST2700 UFS host controller
 * (aspeed,ufshc-m31-16nm).  The UFSHCI register interface, UTRL/UTMRL
 * processing, UPIU/query handling and the SCSI/logical-unit logic are all
 * provided by the shared UFS core (hw/ufs/ufs.c, hw/ufs/lu.c); this file
 * only supplies the sysbus-specific MMIO, IRQ and DMA plumbing.
 *
 * The ASPEED clock/reset wrapper at 0x12c08000 (aspeed,ast2700-ufscnr) is
 * modelled elsewhere as an UnimplementedDevice.
 *
 * Copyright 2026 IBM Corp.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/core/qdev-properties.h"
#include "system/address-spaces.h"
#include "hw/ufs/aspeed_ufs.h"

static void aspeed_ufs_realize(DeviceState *dev, Error **errp)
{
    UfsHc *u = ASPEED_UFS(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    /* AST2700 UFS masters DMA into system memory. */
    u->dma_as = &address_space_memory;

    if (!ufs_realize_core(u, errp)) {
        return;
    }

    /*
     * The AST2700 host controller (aspeed,ufshc-m31-16nm) reports UFSHCI
     * version 2.0. The shared core defaults to 4.1, which makes the U-Boot
     * and Linux aspeed-ufs drivers run 4.x-only probe steps that this model
     * does not implement. Pin the controller version register to 2.0.
     */
    u->reg.ver = 0x00000200;

    ufs_init_mmio(u);
    sysbus_init_mmio(sbd, &u->iomem);
    sysbus_init_irq(sbd, &u->irq);
}

static const Property aspeed_ufs_props[] = {
    DEFINE_PROP_STRING("serial", UfsHc, params.serial),
    DEFINE_PROP_UINT8("nutrs", UfsHc, params.nutrs, 32),
    DEFINE_PROP_UINT8("nutmrs", UfsHc, params.nutmrs, 8),
    DEFINE_PROP_UINT32("wb-max-size", UfsHc, params.wb_max_size, 0x400),
    DEFINE_PROP_UINT32("wb-min-size", UfsHc, params.wb_min_size, 0x100),
};

static void aspeed_ufs_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = aspeed_ufs_realize;
    dc->desc = "ASPEED UFS Host Controller";
    device_class_set_props(dc, aspeed_ufs_props);
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo aspeed_ufs_types[] = {
    {
        .name          = TYPE_ASPEED_UFS,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(UfsHc),
        .class_init    = aspeed_ufs_class_init,
    },
};

DEFINE_TYPES(aspeed_ufs_types)
