/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Loongson 3A5000 ext interrupt controller emulation
 *
 * Copyright (C) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/loongarch/virt.h"
#include "hw/qdev-properties.h"
#include "exec/address-spaces.h"
#include "hw/intc/loongarch_extioi.h"
#include "migration/vmstate.h"
#include "trace.h"

static void extioi_update_irq(LoongArchExtIOI *s, int irq_num, int level)
{
    int ipnum, cpu;

    int ipmap_index = irq_num / 32 / 4;
    int ipmap_offset = (irq_num / 32) & 0x3;
    int ipmap_mask = 0xff << ipmap_offset;

    int cpu_index = irq_num / 4;
    int cpu_offset = irq_num & 0x3;
    int cpu_mask = 0xff << ipmap_offset;

    int coreisr_index = irq_num / 32;
    int enable_index = coreisr_index;
    int coreisr_mask = irq_num & 0x1f;
    int enable_mask = coreisr_mask;
    /*
     * Routing in group of 32 interrupts.
     * The default value of csr[0x420][49]
     * is 0 and nobody will change it,
     * so 'ipmap' use bitmap function.
     */

    ipnum = ((s->ipmap[ipmap_index] & ipmap_mask) >> ipmap_offset) & 0xf;
    ipnum = ctz32(ipnum);
    ipnum = (ipnum >= 4) ? 0 : ipnum;

    cpu = ((s->coremap[cpu_index] & cpu_mask) >> cpu_offset) & 0xf;
    cpu = ctz32(cpu);
    cpu = (cpu >= 4) ? 0 : cpu;

    if (level) {
        /* if not enable return false */
        if (((s->enable[enable_index]) & (1 << enable_mask)) == 0) {
            return;
        }
        s->coreisr[cpu][coreisr_index] |= (1 << coreisr_mask);
        qemu_set_irq(s->parent_irq[cpu][ipnum], level);
    } else {
        s->coreisr[cpu][coreisr_index] &= ~(1 << coreisr_mask);
        qemu_set_irq(s->parent_irq[cpu][ipnum], level);
    }
}

static void extioi_setirq(void *opaque, int irq, int level)
{
    LoongArchExtIOI *s = LOONGARCH_EXTIOI(opaque);
    trace_loongarch_extioi_setirq(irq, level);
    extioi_update_irq(s, irq, level);
}

static uint64_t extioi_readw(void *opaque, hwaddr addr, unsigned size)
{
    LoongArchExtIOI *s = LOONGARCH_EXTIOI(opaque);
    unsigned long offset = addr & 0xffff;
    uint32_t index, cpu, ret = 0;

    switch (offset) {
    case EXTIOI_NODETYPE_START ... EXTIOI_NODETYPE_END - 1:
        index = (offset - EXTIOI_NODETYPE_START) >> 2;
        ret = s->nodetype[index];
        break;
    case EXTIOI_IPMAP_START ... EXTIOI_IPMAP_END - 1:
        index = (offset - EXTIOI_IPMAP_START) >> 2;
        ret = s->ipmap[index];
        break;
    case EXTIOI_ENABLE_START ... EXTIOI_ENABLE_END - 1:
        index = (offset - EXTIOI_ENABLE_START) >> 2;
        ret = s->enable[index];
        break;
    case EXTIOI_BOUNCE_START ... EXTIOI_BOUNCE_END - 1:
        index = (offset - EXTIOI_BOUNCE_START) >> 2;
        ret = s->bounce[index];
        break;
    case EXTIOI_COREISR_START ... EXTIOI_COREISR_END - 1:
        index = ((offset - EXTIOI_COREISR_START) & 0x1f) >> 2;
        cpu = ((offset - EXTIOI_COREISR_START) >> 8) & 0x3;
        ret = s->coreisr[cpu][index];
        break;
    case EXTIOI_COREMAP_START ... EXTIOI_COREMAP_END - 1:
        index = (offset - EXTIOI_COREMAP_START) >> 2;
        ret = s->coremap[index];
        break;
    default:
        break;
    }

    trace_loongarch_extioi_readw((uint32_t)addr, ret);
    return ret;
}

static void extioi_writew(void *opaque, hwaddr addr,
                                   uint64_t val, unsigned size)
{
    LoongArchExtIOI *s = LOONGARCH_EXTIOI(opaque);
    int cpu, index, old_data, data_offset;
    uint32_t offset;
    trace_loongarch_extioi_writew(size, (uint32_t)addr, val);

    offset = addr & 0xffff;

    switch (offset) {
    case EXTIOI_NODETYPE_START ... EXTIOI_NODETYPE_END - 1:
        index = (offset - EXTIOI_NODETYPE_START) >> 2;
        s->nodetype[index] = val;
        break;
    case EXTIOI_IPMAP_START ... EXTIOI_IPMAP_END - 1:
        index = (offset - EXTIOI_IPMAP_START) >> 2;
        s->ipmap[index] = val;
        break;
    case EXTIOI_ENABLE_START ... EXTIOI_ENABLE_END - 1:
        index = (offset - EXTIOI_ENABLE_START) >> 2;
        old_data = s->enable[index];
        if (old_data != (int)val) {
            s->enable[index] = val;
            old_data = old_data ^ val;
            data_offset = ctz32(old_data);
            while (data_offset != 32) {
                if (!(val & (1 << data_offset))) {
                    extioi_update_irq(s, data_offset + index * 32, 0);
                }
                old_data &= ~(1 << data_offset);
                data_offset = ctz32(old_data);
            }
        }
        break;
    case EXTIOI_BOUNCE_START ... EXTIOI_BOUNCE_END - 1:
        index = (offset - EXTIOI_BOUNCE_START) >> 2;
        s->bounce[index] = val;
        break;
    case EXTIOI_COREISR_START ... EXTIOI_COREISR_END - 1:
        index = ((offset - EXTIOI_COREISR_START) & 0x1f) >> 2;
        cpu = ((offset - EXTIOI_COREISR_START) >> 8) & 0x3;
        old_data = s->coreisr[cpu][index];
        s->coreisr[cpu][index] = old_data & ~val;
        if (old_data != s->coreisr[cpu][index]) {
            data_offset = ctz32(val);
            while (data_offset != 32) {
                if ((old_data & (1 << data_offset))) {
                    extioi_update_irq(s, data_offset + index * 32, 0);
                }
                val &= ~(1 << data_offset);
                data_offset = ctz32(val);
            }
        }
        break;
    case EXTIOI_COREMAP_START ... EXTIOI_COREMAP_END - 1:
        index = (offset - EXTIOI_COREMAP_START) >> 2;
        s->coremap[index] = val;
        break;

    default:
        break;
    }
}

static const MemoryRegionOps extioi_ops = {
    .read = extioi_readw,
    .write = extioi_writew,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 8,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const VMStateDescription vmstate_loongarch_extioi = {
    .name = TYPE_LOONGARCH_EXTIOI,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(bounce, LoongArchExtIOI, EXTIOI_IRQS_GROUP_COUNT),
        VMSTATE_UINT32_2DARRAY(coreisr, LoongArchExtIOI, LOONGARCH_MAX_VCPUS,
                               EXTIOI_IRQS_GROUP_COUNT),
        VMSTATE_UINT32_ARRAY(nodetype, LoongArchExtIOI,
                             EXTIOI_IRQS_NODETYPE_COUNT / 2),
        VMSTATE_UINT32_ARRAY(enable, LoongArchExtIOI, 8),
        VMSTATE_UINT32_ARRAY(ipmap, LoongArchExtIOI, 2),
        VMSTATE_UINT32_ARRAY(coremap, LoongArchExtIOI, EXTIOI_IRQS / 4),
        VMSTATE_END_OF_LIST()
    }
};

static void loongarch_extioi_instance_init(Object *obj)
{
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);
    LoongArchExtIOI *s = LOONGARCH_EXTIOI(obj);
    int i, cpu, pin;

    for (i = 0; i < EXTIOI_IRQS; i++) {
        sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq[i]);
    }

    qdev_init_gpio_in(DEVICE(obj), extioi_setirq, EXTIOI_IRQS);

    for (cpu = 0; cpu < LOONGARCH_MAX_VCPUS; cpu++) {
        memory_region_init_io(&s->extioi_iocsr_mem[cpu], OBJECT(s), &extioi_ops,
                              s, "extioi_iocsr", 0x900);
        sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->extioi_iocsr_mem[cpu]);
        for (pin = 0; pin < LS3A_INTC_IP; pin++) {
            qdev_init_gpio_out(DEVICE(obj), &s->parent_irq[cpu][pin], 1);
        }
    }
    memory_region_init_io(&s->extioi_system_mem, OBJECT(s), &extioi_ops,
                          s, "extioi_system_mem", 0x900);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->extioi_system_mem);
}

static void loongarch_extioi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_loongarch_extioi;
}

static const TypeInfo loongarch_extioi_info = {
    .name          = TYPE_LOONGARCH_EXTIOI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = loongarch_extioi_instance_init,
    .instance_size = sizeof(struct LoongArchExtIOI),
    .class_init    = loongarch_extioi_class_init,
};

static void loongarch_extioi_register_types(void)
{
    type_register_static(&loongarch_extioi_info);
}

type_init(loongarch_extioi_register_types)
