/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Loongarch ipi interrupt support
 *
 * Copyright (C) 2024 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "hw/boards.h"
#include "hw/sysbus.h"
#include "hw/intc/loongarch_ipi.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "exec/address-spaces.h"
#include "migration/vmstate.h"
#include "target/loongarch/cpu.h"
#include "trace.h"

static AddressSpace *get_iocsr_as(CPUState *cpu)
{
    return LOONGARCH_CPU(cpu)->env.address_space_iocsr;
}

static int archid_cmp(const void *a, const void *b)
{
   CPUArchId *archid_a = (CPUArchId *)a;
   CPUArchId *archid_b = (CPUArchId *)b;

   return archid_a->arch_id - archid_b->arch_id;
}

static CPUArchId *find_cpu_by_archid(MachineState *ms, uint32_t id)
{
    CPUArchId apic_id, *found_cpu;

    apic_id.arch_id = id;
    found_cpu = bsearch(&apic_id, ms->possible_cpus->cpus,
        ms->possible_cpus->len, sizeof(*ms->possible_cpus->cpus),
        archid_cmp);

    return found_cpu;
}

static CPUState *get_cpu_by_archid(int64_t arch_id)
{
    MachineState *machine = MACHINE(qdev_get_machine());
    CPUArchId *archid;

    archid = find_cpu_by_archid(machine, arch_id);
    if (archid) {
        return CPU(archid->cpu);
    }

    return NULL;
}

static void loongarch_ipi_class_init(ObjectClass *klass, void *data)
{
    LoongsonIPICommonClass *licc = LOONGSON_IPI_COMMON_CLASS(klass);

    licc->get_iocsr_as = get_iocsr_as;
    licc->cpu_by_arch_id = get_cpu_by_archid;
}

static const TypeInfo loongarch_ipi_info = {
    .name          = TYPE_LOONGARCH_IPI,
    .parent        = TYPE_LOONGSON_IPI_COMMON,
    .instance_size = sizeof(LoongarchIPIState),
    .class_size    = sizeof(LoongarchIPIClass),
    .class_init    = loongarch_ipi_class_init,
};

static void loongarch_ipi_register_types(void)
{
    type_register_static(&loongarch_ipi_info);
}

type_init(loongarch_ipi_register_types)
