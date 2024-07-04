/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Loongson ipi interrupt support
 *
 * Copyright (C) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "hw/boards.h"
#include "hw/sysbus.h"
#include "hw/intc/loongson_ipi.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "exec/address-spaces.h"
#include "migration/vmstate.h"
#include "target/mips/cpu.h"
#include "trace.h"

static AddressSpace *get_iocsr_as(CPUState *cpu)
{
    if (ase_lcsr_available(&MIPS_CPU(cpu)->env)) {
        return &MIPS_CPU(cpu)->env.iocsr.as;
    }

    return NULL;
}

static const MemoryRegionOps loongson_ipi_core_ops = {
    .read_with_attrs = loongson_ipi_core_readl,
    .write_with_attrs = loongson_ipi_core_writel,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 8,
    .endianness = DEVICE_LITTLE_ENDIAN,
};


static void loongson_ipi_realize(DeviceState *dev, Error **errp)
{
    LoongsonIPIState *s = LOONGSON_IPI(dev);
    LoongsonIPIClass *lic = LOONGSON_IPI_GET_CLASS(s);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    Error *local_err = NULL;
    int i;

    lic->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    s->ipi_mmio_mem = g_new0(MemoryRegion, s->parent_obj.num_cpu);
    for (i = 0; i < s->parent_obj.num_cpu; i++) {
        g_autofree char *name = g_strdup_printf("loongson_ipi_cpu%d_mmio", i);
        memory_region_init_io(s->ipi_mmio_mem + i, OBJECT(dev),
                              &loongson_ipi_core_ops, &s->parent_obj.cpu[i],
                              name, 0x48);
        sysbus_init_mmio(sbd, s->ipi_mmio_mem + i);
    }
}

static void loongson_ipi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    LoongsonIPICommonClass *licc = LOONGSON_IPI_COMMON_CLASS(klass);
    LoongsonIPIClass *lic = LOONGSON_IPI_CLASS(klass);

    device_class_set_parent_realize(dc, loongson_ipi_realize,
                                    &lic->parent_realize);
    licc->get_iocsr_as = get_iocsr_as;
}

static void loongson_ipi_finalize(Object *obj)
{
    LoongsonIPIState *s = LOONGSON_IPI(obj);

    g_free(s->ipi_mmio_mem);
}

static const TypeInfo loongson_ipi_info = {
    .name          = TYPE_LOONGSON_IPI,
    .parent        = TYPE_LOONGSON_IPI_COMMON,
    .instance_size = sizeof(LoongsonIPIState),
    .class_size    = sizeof(LoongsonIPIClass),
    .class_init    = loongson_ipi_class_init,
    .instance_finalize = loongson_ipi_finalize,
};

static void loongson_ipi_register_types(void)
{
    type_register_static(&loongson_ipi_info);
}

type_init(loongson_ipi_register_types)
