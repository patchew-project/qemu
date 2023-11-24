/*
 * QEMU PowerPC nest1 chiplet model
 *
 * Copyright (c) 2023, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/qdev-properties.h"
#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_xscom.h"
#include "hw/ppc/pnv_nest_chiplet.h"
#include "hw/ppc/pnv_pervasive.h"
#include "hw/ppc/fdt.h"
#include <libfdt.h>

/*
 * The nest1 chiplet contains chiplet control unit,
 * PowerBus/RaceTrack/Bridge logic, nest Memory Management Unit(nMMU)
 * and more.
 */

#define PB_SCOM_EQ0_HP_MODE2_CURR      0xe
#define PB_SCOM_ES3_MODE               0x8a

static uint64_t pnv_nest1_pb_scom_eq_read(void *opaque, hwaddr addr,
                                                  unsigned size)
{
    PnvNest1 *nest1 = PNV_NEST1(opaque);
    int reg = addr >> 3;
    uint64_t val = ~0ull;

    switch (reg) {
    case PB_SCOM_EQ0_HP_MODE2_CURR:
        val = nest1->eq[0].hp_mode2_curr;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Invalid xscom read at 0x%" PRIx32 "\n",
                      __func__, reg);
    }
    return val;
}

static void pnv_nest1_pb_scom_eq_write(void *opaque, hwaddr addr,
                                               uint64_t val, unsigned size)
{
    PnvNest1 *nest1 = PNV_NEST1(opaque);
    int reg = addr >> 3;

    switch (reg) {
    case PB_SCOM_EQ0_HP_MODE2_CURR:
        nest1->eq[0].hp_mode2_curr = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Invalid xscom write at 0x%" PRIx32 "\n",
                      __func__, reg);
    }
}

static const MemoryRegionOps pnv_nest1_pb_scom_eq_ops = {
    .read = pnv_nest1_pb_scom_eq_read,
    .write = pnv_nest1_pb_scom_eq_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static uint64_t pnv_nest1_pb_scom_es_read(void *opaque, hwaddr addr,
                                          unsigned size)
{
    PnvNest1 *nest1 = PNV_NEST1(opaque);
    int reg = addr >> 3;
    uint64_t val = ~0ull;

    switch (reg) {
    case PB_SCOM_ES3_MODE:
        val = nest1->es[3].mode;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Invalid xscom read at 0x%" PRIx32 "\n",
                      __func__, reg);
    }
    return val;
}

static void pnv_nest1_pb_scom_es_write(void *opaque, hwaddr addr,
                                               uint64_t val, unsigned size)
{
    PnvNest1 *nest1 = PNV_NEST1(opaque);
    int reg = addr >> 3;

    switch (reg) {
    case PB_SCOM_ES3_MODE:
        nest1->es[3].mode = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Invalid xscom write at 0x%" PRIx32 "\n",
                      __func__, reg);
    }
}

static const MemoryRegionOps pnv_nest1_pb_scom_es_ops = {
    .read = pnv_nest1_pb_scom_es_read,
    .write = pnv_nest1_pb_scom_es_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void pnv_nest1_realize(DeviceState *dev, Error **errp)
{
    PnvNest1 *nest1 = PNV_NEST1(dev);

    /* perv chiplet initialize and realize */
    object_initialize_child(OBJECT(nest1), "perv", &nest1->perv, TYPE_PNV_PERV);
    object_property_set_str(OBJECT(&nest1->perv), "parent-obj-name", "nest1",
                                   errp);
    if (!qdev_realize(DEVICE(&nest1->perv), NULL, errp)) {
        return;
    }

    /* Nest1 chiplet power bus EQ xscom region */
    pnv_xscom_region_init(&nest1->xscom_pb_eq_regs, OBJECT(nest1),
                          &pnv_nest1_pb_scom_eq_ops, nest1,
                          "xscom-nest1-pb-scom-eq-regs",
                          PNV10_XSCOM_NEST1_PB_SCOM_EQ_SIZE);

    /* Nest1 chiplet power bus ES xscom region */
    pnv_xscom_region_init(&nest1->xscom_pb_es_regs, OBJECT(nest1),
                          &pnv_nest1_pb_scom_es_ops, nest1,
                          "xscom-nest1-pb-scom-es-regs",
                          PNV10_XSCOM_NEST1_PB_SCOM_ES_SIZE);
}

static int pnv_nest1_dt_xscom(PnvXScomInterface *dev, void *fdt,
                             int offset)
{
    PnvNest1 *nest1 = PNV_NEST1(dev);
    g_autofree char *name = NULL;
    int nest1_offset = 0;
    const char compat[] = "ibm,power10-nest1-chiplet";
    uint32_t reg[] = {
        cpu_to_be32(PNV10_XSCOM_NEST1_PB_SCOM_EQ_BASE),
        cpu_to_be32(PNV10_XSCOM_NEST1_PB_SCOM_EQ_SIZE),
        cpu_to_be32(PNV10_XSCOM_NEST1_PB_SCOM_ES_BASE),
        cpu_to_be32(PNV10_XSCOM_NEST1_PB_SCOM_ES_SIZE)
    };

    /* populate perv_chiplet control_regs */
    pnv_perv_dt(&nest1->perv, PNV10_XSCOM_NEST1_CTRL_CHIPLET_BASE, fdt, offset);

    name = g_strdup_printf("nest1@%x", PNV10_XSCOM_NEST1_PB_SCOM_EQ_BASE);
    nest1_offset = fdt_add_subnode(fdt, offset, name);
    _FDT(nest1_offset);

    _FDT(fdt_setprop(fdt, nest1_offset, "reg", reg, sizeof(reg)));
    _FDT(fdt_setprop(fdt, nest1_offset, "compatible", compat, sizeof(compat)));
    return 0;
}

static void pnv_nest1_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvXScomInterfaceClass *xscomc = PNV_XSCOM_INTERFACE_CLASS(klass);

    xscomc->dt_xscom = pnv_nest1_dt_xscom;

    dc->desc = "PowerNV nest1 chiplet";
    dc->realize = pnv_nest1_realize;
}

static const TypeInfo pnv_nest1_info = {
    .name          = TYPE_PNV_NEST1,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(PnvNest1),
    .class_init    = pnv_nest1_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_PNV_XSCOM_INTERFACE },
        { }
    }
};

static void pnv_nest1_register_types(void)
{
    type_register_static(&pnv_nest1_info);
}

type_init(pnv_nest1_register_types);
