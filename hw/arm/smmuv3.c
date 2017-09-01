/*
 * Copyright (C) 2014-2016 Broadcom Corporation
 * Copyright (c) 2017 Red Hat, Inc.
 * Written by Prem Mallappa, Eric Auger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/boards.h"
#include "sysemu/sysemu.h"
#include "hw/sysbus.h"
#include "hw/pci/pci.h"
#include "exec/address-spaces.h"
#include "trace.h"
#include "qemu/error-report.h"

#include "hw/arm/smmuv3.h"
#include "smmuv3-internal.h"

static void smmuv3_init_regs(SMMUV3State *s)
{
    uint32_t data =
        SMMU_IDR0_STLEVEL << SMMU_IDR0_STLEVEL_SHIFT |
        SMMU_IDR0_TERM    << SMMU_IDR0_TERM_SHIFT    |
        SMMU_IDR0_STALL   << SMMU_IDR0_STALL_SHIFT   |
        SMMU_IDR0_VMID16  << SMMU_IDR0_VMID16_SHIFT  |
        SMMU_IDR0_PRI     << SMMU_IDR0_PRI_SHIFT     |
        SMMU_IDR0_ASID16  << SMMU_IDR0_ASID16_SHIFT  |
        SMMU_IDR0_ATS     << SMMU_IDR0_ATS_SHIFT     |
        SMMU_IDR0_HYP     << SMMU_IDR0_HYP_SHIFT     |
        SMMU_IDR0_HTTU    << SMMU_IDR0_HTTU_SHIFT    |
        SMMU_IDR0_COHACC  << SMMU_IDR0_COHACC_SHIFT  |
        SMMU_IDR0_TTF     << SMMU_IDR0_TTF_SHIFT     |
        SMMU_IDR0_S1P     << SMMU_IDR0_S1P_SHIFT     |
        SMMU_IDR0_S2P     << SMMU_IDR0_S2P_SHIFT;

    smmu_write32_reg(s, SMMU_REG_IDR0, data);

#define SMMU_QUEUE_SIZE_LOG2  19
    data =
        1 << 27 |                    /* Attr Types override */
        SMMU_QUEUE_SIZE_LOG2 << 21 | /* Cmd Q size */
        SMMU_QUEUE_SIZE_LOG2 << 16 | /* Event Q size */
        SMMU_QUEUE_SIZE_LOG2 << 11 | /* PRI Q size */
        0  << 6 |                    /* SSID not supported */
        SMMU_IDR1_SIDSIZE;

    smmu_write32_reg(s, SMMU_REG_IDR1, data);

    s->sid_size = SMMU_IDR1_SIDSIZE;

    data = SMMU_IDR5_GRAN << SMMU_IDR5_GRAN_SHIFT | SMMU_IDR5_OAS;

    smmu_write32_reg(s, SMMU_REG_IDR5, data);
}

static void smmuv3_init_queues(SMMUV3State *s)
{
    s->cmdq.prod = 0;
    s->cmdq.cons = 0;
    s->cmdq.wrap.prod = 0;
    s->cmdq.wrap.cons = 0;

    s->evtq.prod = 0;
    s->evtq.cons = 0;
    s->evtq.wrap.prod = 0;
    s->evtq.wrap.cons = 0;

    s->cmdq.entries = SMMU_QUEUE_SIZE_LOG2;
    s->cmdq.ent_size = sizeof(Cmd);
    s->evtq.entries = SMMU_QUEUE_SIZE_LOG2;
    s->evtq.ent_size = sizeof(Evt);
}

static void smmuv3_init(SMMUV3State *s)
{
    smmuv3_init_regs(s);
    smmuv3_init_queues(s);
}

static inline void smmu_update_base_reg(SMMUV3State *s, uint64_t *base,
                                        uint64_t val)
{
    *base = val & ~(SMMU_BASE_RA | 0x3fULL);
}

static void smmu_write_mmio_fixup(SMMUV3State *s, hwaddr *addr)
{
    switch (*addr) {
    case 0x100a8: case 0x100ac:         /* Aliasing => page0 registers */
    case 0x100c8: case 0x100cc:
        *addr ^= (hwaddr)0x10000;
    }
}

static void smmu_write_mmio(void *opaque, hwaddr addr,
                            uint64_t val, unsigned size)
{
}

static uint64_t smmu_read_mmio(void *opaque, hwaddr addr, unsigned size)
{
    SMMUState *sys = opaque;
    SMMUV3State *s = SMMU_V3_DEV(sys);
    uint64_t val;

    smmu_write_mmio_fixup(s, &addr);

    /* Primecell/Corelink ID registers */
    switch (addr) {
    case 0xFF0 ... 0xFFC:
    case 0xFDC ... 0xFE4:
        val = 0;
        error_report("addr:0x%"PRIx64" val:0x%"PRIx64, addr, val);
        break;
    case SMMU_REG_STRTAB_BASE ... SMMU_REG_CMDQ_BASE:
    case SMMU_REG_EVTQ_BASE:
    case SMMU_REG_PRIQ_BASE ... SMMU_REG_PRIQ_IRQ_CFG1:
        val = smmu_read64_reg(s, addr);
        break;
    default:
        val = (uint64_t)smmu_read32_reg(s, addr);
        break;
    }

    trace_smmuv3_read_mmio(addr, val, size);
    return val;
}

static const MemoryRegionOps smmu_mem_ops = {
    .read = smmu_read_mmio,
    .write = smmu_write_mmio,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static void smmu_init_irq(SMMUV3State *s, SysBusDevice *dev)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(s->irq); i++) {
        sysbus_init_irq(dev, &s->irq[i]);
    }
}

static void smmu_reset(DeviceState *dev)
{
    SMMUV3State *s = SMMU_V3_DEV(dev);
    smmuv3_init(s);
}

static void smmu_realize(DeviceState *d, Error **errp)
{
    SMMUState *sys = SMMU_SYS_DEV(d);
    SMMUV3State *s = SMMU_V3_DEV(sys);
    SysBusDevice *dev = SYS_BUS_DEVICE(d);

    memory_region_init_io(&sys->iomem, OBJECT(s),
                          &smmu_mem_ops, sys, TYPE_SMMU_V3_DEV, 0x20000);

    sys->mrtypename = g_strdup(TYPE_SMMUV3_IOMMU_MEMORY_REGION);

    sysbus_init_mmio(dev, &sys->iomem);

    smmu_init_irq(s, dev);
}

static const VMStateDescription vmstate_smmuv3 = {
    .name = "smmuv3",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, SMMUV3State, SMMU_NREGS),
        VMSTATE_END_OF_LIST(),
    },
};

static void smmuv3_instance_init(Object *obj)
{
    /* Nothing much to do here as of now */
}

static void smmuv3_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset   = smmu_reset;
    dc->vmsd    = &vmstate_smmuv3;
    dc->realize = smmu_realize;
}

static void smmuv3_iommu_memory_region_class_init(ObjectClass *klass,
                                                  void *data)
{
}

static const TypeInfo smmuv3_type_info = {
    .name          = TYPE_SMMU_V3_DEV,
    .parent        = TYPE_SMMU_DEV_BASE,
    .instance_size = sizeof(SMMUV3State),
    .instance_init = smmuv3_instance_init,
    .class_data    = NULL,
    .class_size    = sizeof(SMMUV3Class),
    .class_init    = smmuv3_class_init,
};

static const TypeInfo smmuv3_iommu_memory_region_info = {
    .parent = TYPE_IOMMU_MEMORY_REGION,
    .name = TYPE_SMMUV3_IOMMU_MEMORY_REGION,
    .class_init = smmuv3_iommu_memory_region_class_init,
};

static void smmuv3_register_types(void)
{
    type_register(&smmuv3_type_info);
    type_register(&smmuv3_iommu_memory_region_info);
}

type_init(smmuv3_register_types)

