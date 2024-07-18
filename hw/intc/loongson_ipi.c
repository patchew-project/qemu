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
#include "exec/memory.h"
#include "migration/vmstate.h"
#ifdef TARGET_LOONGARCH64
#include "target/loongarch/cpu.h"
#endif
#ifdef TARGET_MIPS
#include "target/mips/cpu.h"
#endif
#include "trace.h"

#ifdef TARGET_LOONGARCH64
static AddressSpace *get_iocsr_as(CPUState *cpu)
{
    return LOONGARCH_CPU(cpu)->env.address_space_iocsr;
}
#endif

#ifdef TARGET_MIPS
static AddressSpace *get_iocsr_as(CPUState *cpu)
{
    if (ase_lcsr_available(&MIPS_CPU(cpu)->env)) {
        return &MIPS_CPU(cpu)->env.iocsr.as;
    }

    return NULL;
}
#endif

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
    LoongsonIPICommonState *sc = LOONGSON_IPI_COMMON(dev);
    LoongsonIPIState *s = LOONGSON_IPI(dev);
    LoongsonIPIClass *lic = LOONGSON_IPI_GET_CLASS(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    Error *local_err = NULL;

    lic->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    s->ipi_mmio_mem = g_new0(MemoryRegion, sc->num_cpu);
    for (unsigned i = 0; i < sc->num_cpu; i++) {
        g_autofree char *name = g_strdup_printf("loongson_ipi_cpu%d_mmio", i);

        memory_region_init_io(&s->ipi_mmio_mem[i], OBJECT(dev),
                              &loongson_ipi_core_ops, &sc->cpu[i], name, 0x48);
        sysbus_init_mmio(sbd, &s->ipi_mmio_mem[i]);
    }
}

static void loongson_ipi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    LoongsonIPIClass *lic = LOONGSON_IPI_CLASS(klass);
    LoongsonIPICommonClass *licc = LOONGSON_IPI_COMMON_CLASS(klass);

    device_class_set_parent_realize(dc, loongson_ipi_realize,
                                    &lic->parent_realize);
    licc->get_iocsr_as = get_iocsr_as;
    licc->cpu_by_arch_id = cpu_by_arch_id;
}

static void loongson_ipi_finalize(Object *obj)
{
    LoongsonIPIState *s = LOONGSON_IPI(obj);

    g_free(s->ipi_mmio_mem);
}

static const TypeInfo loongson_ipi_types[] = {
    {
        .name               = TYPE_LOONGSON_IPI,
        .parent             = TYPE_LOONGSON_IPI_COMMON,
        .class_init         = loongson_ipi_class_init,
        .instance_finalize  = loongson_ipi_finalize,
    }
};

DEFINE_TYPES(loongson_ipi_types)
