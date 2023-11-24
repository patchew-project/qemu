/*
 * QEMU PowerPC pervasive common chiplet model
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

static uint64_t pnv_chiplet_ctrl_read(void *opaque, hwaddr addr, unsigned size)
{
    PnvPerv *perv = PNV_PERV(opaque);
    int reg = addr >> 3;
    uint64_t val = ~0ull;

    /* CPLT_CTRL0 to CPLT_CTRL5 */
    for (int i = 0; i < CPLT_CTRL_SIZE; i++) {
        if (reg == i) {
            return perv->control_regs.cplt_ctrl[i];
        } else if ((reg == (i + 0x10)) || (reg == (i + 0x20))) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Write only register, ignoring "
                                           "xscom read at 0x%" PRIx64 "\n",
                                           __func__, (unsigned long)reg);
            return val;
        }
    }

    switch (reg) {
    case CPLT_CONF0:
        val = perv->control_regs.cplt_cfg0;
        break;
    case CPLT_CONF0_OR:
    case CPLT_CONF0_CLEAR:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Write only register, ignoring "
                                   "xscom read at 0x%" PRIx64 "\n",
                                   __func__, (unsigned long)reg);
        break;
    case CPLT_CONF1:
        val = perv->control_regs.cplt_cfg1;
        break;
    case CPLT_CONF1_OR:
    case CPLT_CONF1_CLEAR:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Write only register, ignoring "
                                   "xscom read at 0x%" PRIx64 "\n",
                                   __func__, (unsigned long)reg);
        break;
    case CPLT_STAT0:
        val = perv->control_regs.cplt_stat0;
        break;
    case CPLT_MASK0:
        val = perv->control_regs.cplt_mask0;
        break;
    case CPLT_PROTECT_MODE:
        val = perv->control_regs.ctrl_protect_mode;
        break;
    case CPLT_ATOMIC_CLOCK:
        val = perv->control_regs.ctrl_atomic_lock;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Chiplet_control_regs: Invalid xscom "
                 "read at 0x%" PRIx64 "\n", __func__, (unsigned long)reg);
    }
    return val;
}

static void pnv_chiplet_ctrl_write(void *opaque, hwaddr addr,
                                 uint64_t val, unsigned size)
{
    PnvPerv *perv = PNV_PERV(opaque);
    int reg = addr >> 3;

    /* CPLT_CTRL0 to CPLT_CTRL5 */
    for (int i = 0; i < CPLT_CTRL_SIZE; i++) {
        if (reg == i) {
            perv->control_regs.cplt_ctrl[i] = val;
            return;
        } else if (reg == (i + 0x10)) {
            perv->control_regs.cplt_ctrl[i] |= val;
            return;
        } else if (reg == (i + 0x20)) {
            perv->control_regs.cplt_ctrl[i] &= ~val;
            return;
        }
    }

    switch (reg) {
    case CPLT_CONF0:
        perv->control_regs.cplt_cfg0 = val;
        break;
    case CPLT_CONF0_OR:
        perv->control_regs.cplt_cfg0 |= val;
        break;
    case CPLT_CONF0_CLEAR:
        perv->control_regs.cplt_cfg0 &= ~val;
        break;
    case CPLT_CONF1:
        perv->control_regs.cplt_cfg1 = val;
        break;
    case CPLT_CONF1_OR:
        perv->control_regs.cplt_cfg1 |= val;
        break;
    case CPLT_CONF1_CLEAR:
        perv->control_regs.cplt_cfg1 &= ~val;
        break;
    case CPLT_STAT0:
        perv->control_regs.cplt_stat0 = val;
        break;
    case CPLT_MASK0:
        perv->control_regs.cplt_mask0 = val;
        break;
    case CPLT_PROTECT_MODE:
        perv->control_regs.ctrl_protect_mode = val;
        break;
    case CPLT_ATOMIC_CLOCK:
        perv->control_regs.ctrl_atomic_lock = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Chiplet_control_regs: Invalid xscom "
                                 "write at 0x%" PRIx64 "\n",
                                 __func__, (unsigned long)reg);
    }
}

static const MemoryRegionOps pnv_perv_control_xscom_ops = {
    .read = pnv_chiplet_ctrl_read,
    .write = pnv_chiplet_ctrl_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void pnv_perv_realize(DeviceState *dev, Error **errp)
{
    PnvPerv *perv = PNV_PERV(dev);
    g_autofree char *region_name = NULL;
    region_name = g_strdup_printf("xscom-%s-control-regs",
                                   perv->parent_obj_name);

    /* Chiplet control scoms */
    pnv_xscom_region_init(&perv->xscom_perv_ctrl_regs, OBJECT(perv),
                          &pnv_perv_control_xscom_ops, perv, region_name,
                          PNV10_XSCOM_CTRL_CHIPLET_SIZE);
}

void pnv_perv_dt(PnvPerv *perv, uint32_t base_addr, void *fdt, int offset)
{
    g_autofree char *name = NULL;
    int perv_offset;
    const char compat[] = "ibm,power10-perv-chiplet";
    uint32_t reg[] = {
        cpu_to_be32(base_addr),
        cpu_to_be32(PNV10_XSCOM_CTRL_CHIPLET_SIZE)
    };

    name = g_strdup_printf("%s-perv@%x", perv->parent_obj_name, base_addr);
    perv_offset = fdt_add_subnode(fdt, offset, name);
    _FDT(perv_offset);

    _FDT(fdt_setprop(fdt, perv_offset, "reg", reg, sizeof(reg)));
    _FDT(fdt_setprop(fdt, perv_offset, "compatible", compat, sizeof(compat)));
}

static Property pnv_perv_properties[] = {
    DEFINE_PROP_STRING("parent-obj-name", PnvPerv, parent_obj_name),
    DEFINE_PROP_END_OF_LIST(),
};

static void pnv_perv_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "PowerNV perv chiplet";
    dc->realize = pnv_perv_realize;
    device_class_set_props(dc, pnv_perv_properties);
}

static const TypeInfo pnv_perv_info = {
    .name          = TYPE_PNV_PERV,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(PnvPerv),
    .class_init    = pnv_perv_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_PNV_XSCOM_INTERFACE },
        { }
    }
};

static void pnv_perv_register_types(void)
{
    type_register_static(&pnv_perv_info);
}

type_init(pnv_perv_register_types);
