/*
 * QEMU PowerPC PowerNV Emulation of OCMB related registers
 *
 * Copyright (c) 2025, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/hwaddr.h"
#include "system/memory.h"
#include "system/cpus.h"
#include "hw/qdev-core.h"
#include "hw/qdev-properties.h"
#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_chip.h"
#include "hw/ppc/pnv_ocmb.h"

static uint64_t pnv_power10_ocmb_read(void *opaque, hwaddr addr,
                                      unsigned size)
{
    /* TODO: Add support for OCMB reads */
    return 0;
}

static void pnv_power10_ocmb_write(void *opaque, hwaddr addr,
                                   uint64_t val, unsigned size)
{
    /* TODO: Add support for OCMB writes */
    return;
}

static const MemoryRegionOps pnv_power10_ocmb_ops = {
    .read = pnv_power10_ocmb_read,
    .write = pnv_power10_ocmb_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 8,
    .impl.min_access_size = 1,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void pnv_ocmb_power10_class_init(ObjectClass *klass, const void *data)
{
    PnvOcmbClass *ocmb = PNV_OCMB_CLASS(klass);

    ocmb->ocmb_size = PNV10_OCMB_SIZE;
    ocmb->ocmb_ops = &pnv_power10_ocmb_ops;
}

static const TypeInfo pnv_ocmb_power10_type_info = {
    .name          = TYPE_PNV10_OCMB,
    .parent        = TYPE_PNV_OCMB,
    .instance_size = sizeof(PnvOcmb),
    .class_init    = pnv_ocmb_power10_class_init,
};

static void pnv_ocmb_realize(DeviceState *dev, Error **errp)
{
    PnvOcmb *ocmb = PNV_OCMB(dev);
    PnvOcmbClass *ocmbc = PNV_OCMB_GET_CLASS(ocmb);

    assert(ocmb->chip);

    /* ocmb region */
    memory_region_init_io(&ocmb->regs, OBJECT(dev),
                          ocmbc->ocmb_ops, ocmb, "ocmb-main-memory",
                          ocmbc->ocmb_size);
}

static const Property pnv_ocmb_properties[] = {
    DEFINE_PROP_LINK("chip", PnvOcmb, chip, TYPE_PNV_CHIP, PnvChip *),
};

static void pnv_ocmb_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pnv_ocmb_realize;
    dc->desc = "PowerNV OCMB Memory";
    device_class_set_props(dc, pnv_ocmb_properties);
    dc->user_creatable = false;
}

static const TypeInfo pnv_ocmb_type_info = {
    .name          = TYPE_PNV_OCMB,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(PnvOcmb),
    .class_init    = pnv_ocmb_class_init,
    .class_size    = sizeof(PnvOcmbClass),
    .abstract      = true,
};

static void pnv_ocmb_register_types(void)
{
    type_register_static(&pnv_ocmb_type_info);
    type_register_static(&pnv_ocmb_power10_type_info);
}

type_init(pnv_ocmb_register_types);
