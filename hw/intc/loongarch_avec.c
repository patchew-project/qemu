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

/* msg addr field */
FIELD(MSG_ADDR, IRQ_NUM, 4, 8)
FIELD(MSG_ADDR, CPU_NUM, 12, 8)
FIELD(MSG_ADDR, FIX, 28, 12)

static uint64_t loongarch_avec_mem_read(void *opaque,
                                        hwaddr addr, unsigned size)
{
    return 0;
}

static void avec_set_irq(LoongArchAVECState *s, int cpu_num, int irq_num, int level)
{
    MachineState *machine = MACHINE(qdev_get_machine());
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    const CPUArchIdList *id_list = NULL;
    CPUState *cpu;
    CPULoongArchState *env;

    assert(mc->possible_cpu_arch_ids(machine));
    id_list = mc->possible_cpu_arch_ids(machine);
    cpu = id_list->cpus[cpu_num].cpu;
    env = &LOONGARCH_CPU(cpu)->env;

    if (level) {
        set_bit(irq_num, &env->CSR_MSGIS[irq_num / 64]);
    }
    qemu_set_irq(s->cpu[cpu_num].parent_irq, level);
}

static void loongarch_avec_mem_write(void *opaque, hwaddr addr,
                                     uint64_t val, unsigned size)
{
    int irq_num, cpu_num = 0;
    LoongArchAVECState *s = LOONGARCH_AVEC(opaque);
    uint64_t msg_addr = addr + VIRT_AVEC_BASE;

    cpu_num = FIELD_EX64(msg_addr, MSG_ADDR, CPU_NUM);
    irq_num = FIELD_EX64(msg_addr, MSG_ADDR, IRQ_NUM);
    avec_set_irq(s, cpu_num, irq_num, 1);
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

static AVECCore *loongarch_avec_get_cpu(LoongArchAVECState *s,
                                        DeviceState *dev)
{
    CPUClass *k = CPU_GET_CLASS(dev);
    uint64_t arch_id = k->get_arch_id(CPU(dev));
    int i;

    for (i = 0; i < s->num_cpu; i++) {
        if (s->cpu[i].arch_id == arch_id) {
            return &s->cpu[i];
        }
    }

    return NULL;
}

static void loongarch_avec_cpu_plug(HotplugHandler *hotplug_dev,
                                   DeviceState *dev, Error **errp)
{
    LoongArchAVECState *s = LOONGARCH_AVEC(hotplug_dev);
    Object *obj = OBJECT(dev);
    AVECCore *core;
    int index;

    if (!object_dynamic_cast(obj, TYPE_LOONGARCH_CPU)) {
        warn_report("LoongArch AVEC: Invalid %s device type",
                                       object_get_typename(obj));
        return;
    }
    core = loongarch_avec_get_cpu(s, dev);
    if (!core) {
        return;
    }

    core->cpu = CPU(dev);
    index = core - s->cpu;

    /* connect avec msg irq to cpu irq */
    qdev_connect_gpio_out(DEVICE(s), index, qdev_get_gpio_in(dev, INT_AVEC));
    return;
}

static void loongarch_avec_cpu_unplug(HotplugHandler *hotplug_dev,
                                     DeviceState *dev, Error **errp)
{
    LoongArchAVECState *s = LOONGARCH_AVEC(hotplug_dev);
    Object *obj = OBJECT(dev);
    AVECCore *core;

    if (!object_dynamic_cast(obj, TYPE_LOONGARCH_CPU)) {
        warn_report("LoongArch AVEC: Invalid %s device type",
                                       object_get_typename(obj));
        return;
    }

    core = loongarch_avec_get_cpu(s, dev);

    if (!core) {
        return;
    }

    core->cpu = NULL;
}

static void loongarch_avec_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(klass);
    LoongArchAVECClass *lac = LOONGARCH_AVEC_CLASS(klass);

    dc->unrealize = loongarch_avec_unrealize;
    device_class_set_parent_realize(dc, loongarch_avec_realize,
                                    &lac->parent_realize);
    hc->plug = loongarch_avec_cpu_plug;
    hc->unplug = loongarch_avec_cpu_unplug;
}

static const TypeInfo loongarch_avec_info = {
    .name          = TYPE_LOONGARCH_AVEC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LoongArchAVECState),
    .instance_init = loongarch_avec_init,
    .class_init    = loongarch_avec_class_init,
    .interfaces    = (const InterfaceInfo[]) {
        { TYPE_HOTPLUG_HANDLER },
        { }
    },
};

static void loongarch_avec_register_types(void)
{
    type_register_static(&loongarch_avec_info);
}

type_init(loongarch_avec_register_types)
