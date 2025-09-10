/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU Loongson  Advance interrupt controller.
 *
 * Copyright (C) 2025 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/intc/loongarch_pch_msi.h"
#include "hw/intc/loongarch_pch_pic.h"
#include "hw/intc/loongarch_avec.h"
#include "hw/pci/msi.h"
#include "hw/misc/unimp.h"
#include "migration/vmstate.h"
#include "trace.h"
#include "hw/qdev-properties.h"
#include "target/loongarch/cpu.h"
#include "qemu/error-report.h"
#include "system/hw_accel.h"

/* msg addr field */
FIELD(MSG_ADDR, IRQ_NUM, 4, 8)
FIELD(MSG_ADDR, CPU_NUM, 12, 8)
FIELD(MSG_ADDR, FIX, 28, 12)

static uint64_t loongarch_avec_mem_read(void *opaque,
                                        hwaddr addr, unsigned size)
{
    return 0;
}

static void do_set_vcpu_avec_irq(CPUState *cs, run_on_cpu_data data)
{
    int irq = data.host_int;
    CPULoongArchState *env;

    env = &LOONGARCH_CPU(cs)->env;
    cpu_synchronize_state(cs);
    set_bit(irq, env->CSR_MSGIS);
}

static void loongarch_avec_mem_write(void *opaque, hwaddr addr,
                                     uint64_t val, unsigned size)
{
    int irq_num, cpu_num = 0;
    LoongArchAVECState *s = LOONGARCH_AVEC(opaque);
    uint64_t msg_addr = addr + VIRT_AVEC_BASE;
    CPUState *cs;

    cpu_num = FIELD_EX64(msg_addr, MSG_ADDR, CPU_NUM);
    cs = cpu_by_arch_id(cpu_num);
    irq_num = FIELD_EX64(msg_addr, MSG_ADDR, IRQ_NUM);

    async_run_on_cpu(cs, do_set_vcpu_avec_irq,
                         RUN_ON_CPU_HOST_INT(irq_num));
    qemu_set_irq(s->cpu[cpu_num].parent_irq, 1);
}

static const MemoryRegionOps loongarch_avec_ops = {
    .read = loongarch_avec_mem_read,
    .write = loongarch_avec_mem_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void loongarch_avec_realize(DeviceState *dev, Error **errp)
{
    LoongArchAVECState *s = LOONGARCH_AVEC(dev);
    LoongArchAVECClass *lac = LOONGARCH_AVEC_GET_CLASS(dev);
    MachineState *machine = MACHINE(qdev_get_machine());
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    const CPUArchIdList  *id_list;
    int i;

    Error *local_err = NULL;
    lac->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    assert(mc->possible_cpu_arch_ids);
    id_list = mc->possible_cpu_arch_ids(machine);
    s->num_cpu = id_list->len;
    s->cpu = g_new(AVECCore, s->num_cpu);
    if (s->cpu == NULL) {
        error_setg(errp, "Memory allocation for AVECCore fail");
        return;
    }

    for (i = 0; i < s->num_cpu; i++) {
        s->cpu[i].arch_id = id_list->cpus[i].arch_id;
        s->cpu[i].cpu = CPU(id_list->cpus[i].cpu);
        qdev_init_gpio_out(dev, &s->cpu[i].parent_irq, 1);
    }

    return;
}

static void loongarch_avec_unrealize(DeviceState *dev)
{
    return;
}

static void loongarch_avec_init(Object *obj)
{
    LoongArchAVECState *s = LOONGARCH_AVEC(obj);
    SysBusDevice *shd = SYS_BUS_DEVICE(obj);
    memory_region_init_io(&s->avec_mmio, OBJECT(s), &loongarch_avec_ops,
                          s, TYPE_LOONGARCH_AVEC, VIRT_AVEC_MSG_OFFSET);
    sysbus_init_mmio(shd, &s->avec_mmio);
    msi_nonbroken = true;
    return;
}

static void loongarch_avec_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    LoongArchAVECClass *lac = LOONGARCH_AVEC_CLASS(klass);

    dc->unrealize = loongarch_avec_unrealize;
    device_class_set_parent_realize(dc, loongarch_avec_realize,
                                    &lac->parent_realize);
}

static const TypeInfo loongarch_avec_info = {
    .name          = TYPE_LOONGARCH_AVEC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LoongArchAVECState),
    .instance_init = loongarch_avec_init,
    .class_init    = loongarch_avec_class_init,
};

static void loongarch_avec_register_types(void)
{
    type_register_static(&loongarch_avec_info);
}

type_init(loongarch_avec_register_types)
