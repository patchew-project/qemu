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

static void pnv_nest1_chiplet_realize(DeviceState *dev, Error **errp)
{
    PnvNest1Chiplet *nest1_chiplet = PNV_NEST1CHIPLET(dev);

    object_initialize_child(OBJECT(nest1_chiplet), "perv_chiplet",
                            &nest1_chiplet->perv_chiplet,
                            TYPE_PNV_PERV_CHIPLET);

    if (!qdev_realize(DEVICE(&nest1_chiplet->perv_chiplet), NULL, errp)) {
        return;
    }
}

static int pnv_nest1_chiplet_dt_xscom(PnvXScomInterface *dev, void *fdt,
                             int offset)
{
    g_autofree char *name = NULL;
    int nest1_chiplet_offset;
    const char compat[] = "ibm,power10-nest1-chiplet";

    name = g_strdup_printf("nest1_chiplet@%x",
                           PNV10_XSCOM_NEST1_CTRL_CHIPLET_BASE);
    nest1_chiplet_offset = fdt_add_subnode(fdt, offset, name);
    _FDT(nest1_chiplet_offset);

    _FDT(fdt_setprop(fdt, nest1_chiplet_offset, "compatible",
                            compat, sizeof(compat)));
    return 0;
}

static void pnv_nest1_dt_populate(void *fdt)
{

    uint32_t nest1_base = cpu_to_be32(PNV10_XSCOM_NEST1_CTRL_CHIPLET_BASE);
    pnv_perv_dt(nest1_base, fdt, 0);
}

static void pnv_nest1_chiplet_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvNest1Class *nest1_class = PNV_NEST1CHIPLET_CLASS(klass);
    PnvXScomInterfaceClass *xscomc = PNV_XSCOM_INTERFACE_CLASS(klass);

    xscomc->dt_xscom = pnv_nest1_chiplet_dt_xscom;

    dc->desc = "PowerNV nest1 chiplet";
    dc->realize = pnv_nest1_chiplet_realize;
    nest1_class->nest1_dt_populate = pnv_nest1_dt_populate;
}

static const TypeInfo pnv_nest1_chiplet_info = {
    .name          = TYPE_PNV_NEST1_CHIPLET,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(PnvNest1Chiplet),
    .class_init    = pnv_nest1_chiplet_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_PNV_XSCOM_INTERFACE },
        { }
    }
};

static void pnv_nest1_chiplet_register_types(void)
{
    type_register_static(&pnv_nest1_chiplet_info);
}

type_init(pnv_nest1_chiplet_register_types);
