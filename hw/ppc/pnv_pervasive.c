/*
 * QEMU PowerPC pervasive common chiplet model
 *
 * Copyright (c) 2023, IBM Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/qdev-properties.h"
#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_xscom.h"
#include "hw/ppc/pnv_pervasive.h"
#include "hw/ppc/fdt.h"
#include <libfdt.h>

#define CPLT_CONF0               0x08
#define CPLT_CONF0_OR            0x18
#define CPLT_CONF0_CLEAR         0x28
#define CPLT_CONF1               0x09
#define CPLT_CONF1_OR            0x19
#define CPLT_CONF1_CLEAR         0x29
#define CPLT_STAT0               0x100
#define CPLT_MASK0               0x101
#define CPLT_PROTECT_MODE        0x3FE
#define CPLT_ATOMIC_CLOCK        0x3FF

static uint64_t pnv_chiplet_ctrl_read(void *opaque, hwaddr addr,
                                 unsigned size)
{
    PnvPervChiplet *perv_chiplet = PNV_PERVCHIPLET(opaque);
    int reg = addr >> 3;
    uint64_t val = 0xffffffffffffffffull;

    /* CPLT_CTRL0 to CPLT_CTRL5 */
    for (int i = 0; i <= 5; i++) {
        if (reg == i) {
            val = perv_chiplet->control_regs.cplt_ctrl[i];
            return val;
        } else if ((reg == (i + 0x10)) || (reg == (i + 0x20))) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Write only register, ignoring "
                                           "xscom read at 0x%016lx\n",
                                          __func__, (unsigned long)reg);
            return val;
        }
    }

    switch (reg) {
    case CPLT_CONF0:
        val = perv_chiplet->control_regs.cplt_cfg0;
        break;
    case CPLT_CONF0_OR:
    case CPLT_CONF0_CLEAR:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Write only register, ignoring "
                                   "xscom read at 0x%016lx\n",
                                   __func__, (unsigned long)reg);
        break;
    case CPLT_CONF1:
        val = perv_chiplet->control_regs.cplt_cfg1;
        break;
    case CPLT_CONF1_OR:
    case CPLT_CONF1_CLEAR:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Write only register, ignoring "
                                   "xscom read at 0x%016lx\n",
                                   __func__, (unsigned long)reg);
        break;
    case CPLT_STAT0:
        val = perv_chiplet->control_regs.cplt_stat0;
        break;
    case CPLT_MASK0:
        val = perv_chiplet->control_regs.cplt_mask0;
        break;
    case CPLT_PROTECT_MODE:
        val = perv_chiplet->control_regs.ctrl_protect_mode;
        break;
    case CPLT_ATOMIC_CLOCK:
        val = perv_chiplet->control_regs.ctrl_atomic_lock;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Chiplet_control_regs: Invalid xscom "
                 "read at 0x%016lx\n", __func__, (unsigned long)reg);
    }
    return val;
}

static void pnv_chiplet_ctrl_write(void *opaque, hwaddr addr,
                                 uint64_t val, unsigned size)
{
    PnvPervChiplet *perv_chiplet = PNV_PERVCHIPLET(opaque);
    int reg = addr >> 3;
    /* CPLT_CTRL0 to CPLT_CTRL5 */
    for (int i = 0; i <= 5; i++) {
        if (reg == i) {
            perv_chiplet->control_regs.cplt_ctrl[i] = val;
            return;
        } else if (reg == (i + 0x10)) {
            perv_chiplet->control_regs.cplt_ctrl[i] |= val;
            return;
        } else if (reg == (i + 0x20)) {
            perv_chiplet->control_regs.cplt_ctrl[i] &= ~val;
            return;
        }
    }

    switch (reg) {
    case CPLT_CONF0:
        perv_chiplet->control_regs.cplt_cfg0 = val;
        break;
    case CPLT_CONF0_OR:
        perv_chiplet->control_regs.cplt_cfg0 |= val;
        break;
    case CPLT_CONF0_CLEAR:
        perv_chiplet->control_regs.cplt_cfg0 &= ~val;
        break;
    case CPLT_CONF1:
        perv_chiplet->control_regs.cplt_cfg1 = val;
        break;
    case CPLT_CONF1_OR:
        perv_chiplet->control_regs.cplt_cfg1 |= val;
        break;
    case CPLT_CONF1_CLEAR:
        perv_chiplet->control_regs.cplt_cfg1 &= ~val;
        break;
    case CPLT_STAT0:
        perv_chiplet->control_regs.cplt_stat0 = val;
        break;
    case CPLT_MASK0:
        perv_chiplet->control_regs.cplt_mask0 = val;
        break;
    case CPLT_PROTECT_MODE:
        perv_chiplet->control_regs.ctrl_protect_mode = val;
        break;
    case CPLT_ATOMIC_CLOCK:
        perv_chiplet->control_regs.ctrl_atomic_lock = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Chiplet_control_regs: Invalid xscom "
                       "write at 0x%016lx\n", __func__, (unsigned long)reg);
    }
    return;
}

static const MemoryRegionOps pnv_perv_chiplet_control_xscom_ops = {
    .read = pnv_chiplet_ctrl_read,
    .write = pnv_chiplet_ctrl_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void pnv_perv_chiplet_realize(DeviceState *dev, Error **errp)
{
    PnvPervChiplet *perv_chiplet = PNV_PERVCHIPLET(dev);

    /* Chiplet control scoms */
    pnv_xscom_region_init(&perv_chiplet->xscom_perv_ctrl_regs,
                          OBJECT(perv_chiplet),
                          &pnv_perv_chiplet_control_xscom_ops,
                          perv_chiplet, "xscom-chiplet-control-regs",
                          PNV10_XSCOM_CTRL_CHIPLET_SIZE);
}

static int pnv_perv_chiplet_dt_xscom(PnvXScomInterface *dev, void *fdt,
                             int offset)
{
    char *name;
    static int perv_chiplet_offset;

    const char compat[] = "ibm,power10-perv-chiplet";
    uint32_t reg[] = {
        cpu_to_be32(PNV10_XSCOM_NEST1_CTRL_CHIPLET_BASE),
        cpu_to_be32(PNV10_XSCOM_CTRL_CHIPLET_SIZE)
    };
    if (perv_chiplet_offset == 0) {
        name = g_strdup_printf("perv_chiplet@%x",
                                PNV10_XSCOM_NEST1_CTRL_CHIPLET_BASE);
        perv_chiplet_offset = fdt_add_subnode(fdt, offset, name);
        _FDT(perv_chiplet_offset);
        g_free(name);

        _FDT(fdt_setprop(fdt, perv_chiplet_offset, "reg", reg, sizeof(reg)));
        _FDT(fdt_setprop(fdt, perv_chiplet_offset, "compatible", compat,
                         sizeof(compat)));
    }
    return 0;
}

static Property pnv_perv_chiplet_properties[] = {
    DEFINE_PROP_LINK("chip", PnvPervChiplet, chip, TYPE_PNV_CHIP, PnvChip *),
    DEFINE_PROP_END_OF_LIST(),
};

static void pnv_perv_chiplet_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvXScomInterfaceClass *xscomc = PNV_XSCOM_INTERFACE_CLASS(klass);

    xscomc->dt_xscom = pnv_perv_chiplet_dt_xscom;

    dc->desc = "PowerNV perv chiplet";
    dc->realize = pnv_perv_chiplet_realize;
    device_class_set_props(dc, pnv_perv_chiplet_properties);
}

static const TypeInfo pnv_perv_chiplet_info = {
    .name          = TYPE_PNV_PERV_CHIPLET,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(PnvPervChiplet),
    .class_init    = pnv_perv_chiplet_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_PNV_XSCOM_INTERFACE },
        { }
    }
};

static void pnv_perv_chiplet_register_types(void)
{
    type_register_static(&pnv_perv_chiplet_info);
}

type_init(pnv_perv_chiplet_register_types);
