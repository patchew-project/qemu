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


static int get_coremap(LoongArchExtIOI *s, int irq_num)
{
    int cpu;
    int cpu_index = irq_num / 4;
    int cpu_offset = irq_num & 0x3;
    int cpu_mask = 0xf << cpu_offset;

    cpu = (s->coremap[cpu_index] & cpu_mask) >> cpu_offset;
    cpu = ctz32(cpu);
    cpu = (cpu >= 4) ? 0 : cpu;
    return cpu;
}

static int get_ipmap(LoongArchExtIOI *s, int irq_num)
{
    int ipnum;
    int ipmap_index = irq_num / 32 / 4;
    int ipmap_offset = (irq_num / 32) & 0x3;
    int ipmap_mask = 0xf << ipmap_offset;

    ipnum = (s->ipmap[ipmap_index] & ipmap_mask) >> ipmap_offset;
    ipnum = ctz32(ipnum);
    ipnum = (ipnum >= 4) ? 0 : ipnum;
    return ipnum;
}

static void extioi_update_irq(LoongArchExtIOI *s, int irq_num, int level)
{
    int ipnum, cpu, found, irq_index, irq_mask;

    ipnum = get_ipmap(s, irq_num);
    cpu = get_coremap(s, irq_num);
    irq_index = irq_num / 32;
    irq_mask = 1 << (irq_num & 0x1f);

    if (level) {
        /* if not enable return false */
        if (((s->enable[irq_index]) & irq_mask) == 0) {
            s->sw_pending[irq_index] |= irq_mask;
            return;
        }
        s->coreisr[cpu][irq_index] |= irq_mask;
        found = find_first_bit(s->sw_isr[cpu][ipnum], EXTIOI_IRQS);
        set_bit(irq_num, s->sw_isr[cpu][ipnum]);
        if (found < EXTIOI_IRQS) {
            /* other irq is handling, need not update parent irq level */
            return;
        }
    } else {
        s->coreisr[cpu][irq_index] &= ~irq_mask;
        clear_bit(irq_num, s->sw_isr[cpu][ipnum]);
        found = find_first_bit(s->sw_isr[cpu][ipnum], EXTIOI_IRQS);
        if (found < EXTIOI_IRQS) {
            /* other irq is handling, need not update parent irq level */
            return;
        }
    }
    qemu_set_irq(s->parent_irq[cpu][ipnum], level);
}

static void extioi_setirq(void *opaque, int irq, int level)
{
    LoongArchExtIOI *s = LOONGARCH_EXTIOI(opaque);
    trace_loongarch_extioi_setirq(irq, level);
    if (level) {
        s->isr[irq / 32] |= 1 << (irq % 32);
    } else {
        s->isr[irq / 32] &= ~(1 << (irq % 32));
    }
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
    int i, cpu, index, old_data, data_offset;
    int old_ip, new_ip, old_core, new_core, irq_mask, irq_num;
    uint32_t offset;
    int old_ipnum[128], old_cpu[4];
    trace_loongarch_extioi_writew(size, (uint32_t)addr, val);

    offset = addr & 0xffff;

    switch (offset) {
    case EXTIOI_NODETYPE_START ... EXTIOI_NODETYPE_END - 1:
        index = (offset - EXTIOI_NODETYPE_START) >> 2;
        s->nodetype[index] = val;
        break;
    case EXTIOI_IPMAP_START ... EXTIOI_IPMAP_END - 1:
        /* get irq number */
        offset -= EXTIOI_IPMAP_START;
        index = offset >> 2;
        /*
         * 4 bytes writing, set 4 irq groups one time,
         * and one group is 32 irqs, so set 128 irqs mapping
         */
        for (i = 0; i < 128; i++) {
            old_ipnum[i] = get_ipmap(s, offset);
            offset += 1;
        }
        s->ipmap[index] = val;
        offset -= 128;
        /* if core isr is set, need to update irq */
        for (i = 0; i < 128; i++) {
            old_ip = old_ipnum[i];
            new_ip = get_ipmap(s, offset);
            cpu = get_coremap(s, offset);
            if (old_ip != new_ip) {
                if (s->coreisr[cpu][offset / 32] & (1 << (offset & 0x1f))) {
                    extioi_update_irq(s, offset, 1);
                }
            }
        }
        break;
    case EXTIOI_ENABLE_START ... EXTIOI_ENABLE_END - 1:
        index = (offset - EXTIOI_ENABLE_START) >> 2;
        old_data = s->enable[index];
        if (old_data != (int)val) {
            s->enable[index] = val;
            /* get data diff */
            old_data ^= val;
            /* value change from 0 to 1 */
            old_data &= val;
            data_offset = ctz32(old_data);
            while (data_offset != 32) {
                /*
                 * enable bit change from 0 to 1,
                 * need to update irq by pending bits
                 */
                irq_num = data_offset + index * 32;
                irq_mask = 1 << data_offset;
                if (s->sw_pending[index] & irq_mask) {
                    extioi_update_irq(s, irq_num, 1);
                    s->sw_pending[index] &= ~irq_mask;
                }
                old_data &= ~irq_mask;
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
            /* coreisr change from 1 to 0 */
            val &= old_data;
            data_offset = ctz32(val);
            while (data_offset != 32) {
                extioi_update_irq(s, data_offset + index * 32, 0);
                val &= ~(1 << data_offset);
                data_offset = ctz32(val);
            }
        }
        break;
    case EXTIOI_COREMAP_START ... EXTIOI_COREMAP_END - 1:
        /* get irq number */
        offset -= EXTIOI_COREMAP_START;
        index = offset >> 2;
        /* 4 bytes writing, set 4 irqs mapping */
        for (i = 0; i < 4; i++) {
            /* get old core mapping */
            old_cpu[i] = get_coremap(s, offset);
            offset += 1;
        }
        s->coremap[index] = val;
        offset -= 4;
        /* if old core isr is set, need to recompute new core isr */
        for (i = 0; i < 4; i++) {
            old_core = old_cpu[i];
            new_core = get_coremap(s, offset);
            if (old_core != new_core) {
                if (s->coreisr[old_core][offset / 32] &
                    (1 << (offset & 0x1f))) {
                    extioi_update_irq(s, offset, 1);
                }
            }
        }
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
        VMSTATE_UINT32_ARRAY(isr, LoongArchExtIOI, EXTIOI_IRQS / 32),
        VMSTATE_UINT32_ARRAY(sw_pending, LoongArchExtIOI, EXTIOI_IRQS / 32),
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
