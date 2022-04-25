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
    uint64_t ipnum, cpu;

    /*
     * Routing in group of 32 interrupts.
     * The default value of csr[0x420][49]
     * is 0 and nobody will change it,
     * so 'ipmap' use bitmap function.
     */
    ipnum = ldub_p((void *)&s->ipmap + (irq_num / 32)) & 0xf;
    ipnum = find_first_bit(&ipnum, 4);
    ipnum = (ipnum == 4) ? 0 : ipnum;

    cpu = ldub_p((void *)s->coremap + irq_num) & 0xf;
    cpu = find_first_bit(&cpu, 4);
    cpu = (cpu == 4) ? 0 : cpu;

    if (level) {
        if (test_bit(irq_num, (unsigned long *)s->enable) == false) {
            return;
        }
        bitmap_set((unsigned long *)s->coreisr[cpu], irq_num, 1);
        qemu_set_irq(s->parent_irq[cpu][ipnum], level);
    } else {
        bitmap_clear((unsigned long *)s->coreisr[cpu], irq_num, 1);
        qemu_set_irq(s->parent_irq[cpu][ipnum], level);
    }
}

static void extioi_setirq(void *opaque, int irq, int level)
{
    LoongArchExtIOI *s = LOONGARCH_EXTIOI(opaque);
    trace_loongarch_extioi_setirq(irq, level);
    extioi_update_irq(s, irq, level);
}

static uint32_t extioi_readb(void *opaque, hwaddr addr)
{
    LoongArchExtIOI *s = LOONGARCH_EXTIOI(opaque);
    unsigned long offset, reg_count;
    uint8_t ret = 0;
    int cpu;

    offset = addr & 0xffff;
    switch (offset) {
    case EXTIOI_ENABLE_START ... EXTIOI_ENABLE_END - 1:
        ret = ldub_p((void *)s->enable + (offset - EXTIOI_ENABLE_START));
        break;
    case EXTIOI_BOUNCE_START ... EXTIOI_BOUNCE_END - 1:
        ret = ldub_p((void *)s->bounce + (offset - EXTIOI_BOUNCE_START));
        break;
    case EXTIOI_COREISR_START ... EXTIOI_COREISR_END - 1:
        reg_count = ((offset - EXTIOI_COREISR_START) & 0x1f);
        cpu = ((offset - EXTIOI_COREISR_START) >> 8) & 0x3;
        ret = ldub_p((void *)s->coreisr[cpu] + reg_count);
        break;
    case EXTIOI_IPMAP_START ... EXTIOI_IPMAP_END - 1:
        ret = ldub_p((void *)&s->ipmap + (offset - EXTIOI_IPMAP_START));
        break;
    case EXTIOI_COREMAP_START ... EXTIOI_COREMAP_END - 1:
        ret = ldub_p((void *)s->coremap + (offset - EXTIOI_COREMAP_START));
        break;
    case EXTIOI_NODETYPE_START ... EXTIOI_NODETYPE_END - 1:
        ret = ldub_p((void *)s->nodetype + (offset - EXTIOI_NODETYPE_START));
        break;
    default:
        break;
    }
    trace_loongarch_extioi_readb((uint32_t)addr, ret);
    return ret;
}

static uint32_t extioi_readw(void *opaque, hwaddr addr)
{
    LoongArchExtIOI *s = LOONGARCH_EXTIOI(opaque);
    unsigned long offset, reg_count;
    uint32_t ret = 0;
    int cpu;

    offset = addr & 0xffff;
    switch (offset) {
    case EXTIOI_ENABLE_START ... EXTIOI_ENABLE_END - 1:
        ret = ldl_p((void *)s->enable + (offset - EXTIOI_ENABLE_START));
        break;
    case EXTIOI_BOUNCE_START ... EXTIOI_BOUNCE_END - 1:
        ret = ldl_p((void *)s->bounce + (offset - EXTIOI_BOUNCE_START));
        break;
    case EXTIOI_COREISR_START ... EXTIOI_COREISR_END - 1:
        reg_count = ((offset - EXTIOI_COREISR_START) & 0x1f);
        cpu = ((offset - EXTIOI_COREISR_START) >> 8) & 0x3;
        ret = ldl_p((void *)s->coreisr[cpu] + reg_count);
        break;
    case EXTIOI_IPMAP_START ... EXTIOI_IPMAP_END - 1:
        ret = ldl_p((void *)&s->ipmap + (offset - EXTIOI_IPMAP_START));
        break;
    case EXTIOI_COREMAP_START ... EXTIOI_COREMAP_END - 1:
        ret = ldl_p((void *)s->coremap + (offset - EXTIOI_COREMAP_START));
        break;
    case EXTIOI_NODETYPE_START ... EXTIOI_NODETYPE_END - 1:
        ret = ldl_p((void *)s->nodetype + (offset - EXTIOI_NODETYPE_START));
        break;
    default:
        break;
    }
    trace_loongarch_extioi_readw((uint32_t)addr, ret);
    return ret;
}

static uint64_t extioi_readl(void *opaque, hwaddr addr)
{
    LoongArchExtIOI *s = LOONGARCH_EXTIOI(opaque);
    unsigned long offset, reg_count;
    uint64_t ret = 0;
    int cpu;

    offset = addr & 0xffff;

    switch (offset) {
    case EXTIOI_ENABLE_START ... EXTIOI_ENABLE_END - 1:
        ret = ldq_p((void *)s->enable + (offset - EXTIOI_ENABLE_START));
        break;
    case EXTIOI_BOUNCE_START ... EXTIOI_BOUNCE_END - 1:
        ret = ldq_p((void *)s->bounce + (offset - EXTIOI_BOUNCE_START));
        break;
    case EXTIOI_COREISR_START ... EXTIOI_COREISR_END - 1:
        reg_count = ((offset - EXTIOI_COREISR_START) & 0x1f);
        cpu = ((offset - EXTIOI_COREISR_START) >> 8) & 0x3;
        ret = ldq_p((void *)s->coreisr[cpu] + reg_count);
        break;
    case EXTIOI_IPMAP_START ... EXTIOI_IPMAP_END - 1:
        ret = ldq_p((void *)&s->ipmap + (offset - EXTIOI_IPMAP_START));
        break;
    case EXTIOI_COREMAP_START ... EXTIOI_COREMAP_END - 1:
        ret = ldq_p((void *)s->coremap + (offset - EXTIOI_COREMAP_START));
        break;
    case EXTIOI_NODETYPE_START ... EXTIOI_NODETYPE_END - 1:
        ret = ldq_p((void *)s->nodetype + (offset - EXTIOI_NODETYPE_START));
        break;
    default:
        break;
    }
    trace_loongarch_extioi_readl((uint32_t)addr, ret);
    return ret;
}

static void extioi_writeb(void *opaque, hwaddr addr, uint32_t value,
                          unsigned size)
{
    LoongArchExtIOI *s = LOONGARCH_EXTIOI(opaque);
    unsigned long offset, reg_count;
    int cpu, i, j, level, bits;
    uint64_t old_data, val = value & 0xff;

    offset = addr & 0xffff;

    trace_loongarch_extioi_writeb(size, (uint32_t)addr, (uint8_t)val);
    switch (offset) {
    case EXTIOI_ENABLE_START ... EXTIOI_ENABLE_END - 1:
        reg_count = (offset - EXTIOI_ENABLE_START);
        old_data = ldub_p((void *)s->enable + reg_count);
        if (old_data != val) {
            stb_p((void *)s->enable + reg_count, (uint8_t)val);
            old_data = old_data ^ val;
            bits = size * 8;
            while ((i = find_first_bit(&old_data, bits)) != bits) {
                level = test_bit(i, &val);
                extioi_update_irq(s, i + reg_count * 8, level);
                clear_bit(i, &old_data);
            }
        }
        break;
    case EXTIOI_BOUNCE_START ... EXTIOI_BOUNCE_END - 1:
        stb_p((void *)s->bounce + (offset - EXTIOI_BOUNCE_START), (uint8_t)val);
        break;
    case EXTIOI_COREISR_START ... EXTIOI_COREISR_END - 1:
        reg_count = (offset - EXTIOI_COREISR_START) & 0x1f;
        cpu = ((offset - EXTIOI_COREISR_START) >> 8) & 0x3;

        old_data = ldub_p((void *)s->coreisr[cpu] + reg_count);
        stb_p((void *)s->coreisr[cpu] + reg_count, (uint8_t)(old_data & ~val));

        if (old_data != (old_data & ~val)) {
            bits = size * 8;

            while ((i = find_first_bit(&val, bits)) != bits) {
                j = test_bit(i, &old_data);
                if (j) {
                    extioi_update_irq(s, i + reg_count * 8, 0);
                }
                clear_bit(i, &val);
            }
        }
        break;
    case EXTIOI_IPMAP_START ... EXTIOI_IPMAP_END - 1:
        /* Drop arch.core_ip_mask use s->ipmap */
        reg_count = (offset - EXTIOI_IPMAP_START);
        stb_p((void *)&s->ipmap + reg_count, (uint8_t)val);
        break;
    case EXTIOI_COREMAP_START ... EXTIOI_COREMAP_END - 1:
        reg_count = (offset - EXTIOI_COREMAP_START);

        /* Only map the core */
        while (val) {
            stb_p((void *)s->coremap + reg_count, (uint8_t)val);
            val = val >> 8;
        }
        break;
    case EXTIOI_NODETYPE_START ... EXTIOI_NODETYPE_END - 1:
        stb_p((void *)s->nodetype + (offset - EXTIOI_NODETYPE_START),
                                     (uint8_t)val);
        break;
    default:
        break;
    }
}

static void extioi_writew(void *opaque, hwaddr addr, uint32_t val,
                          unsigned size)
{
    LoongArchExtIOI *s = LOONGARCH_EXTIOI(opaque);
    int cpu, level;
    uint32_t offset, reg_count, i, j, bits;
    uint64_t old_data, value = val;
    offset = addr & 0xffff;
    trace_loongarch_extioi_writew(size, (uint32_t)addr, val);

    switch (offset) {
    case EXTIOI_ENABLE_START ... EXTIOI_ENABLE_END - 1:
        reg_count = (offset - EXTIOI_ENABLE_START);
        old_data = ldl_p((void *)s->enable + reg_count);
        if (old_data != val) {
            stl_p((void *)s->enable + reg_count, val);
            old_data = old_data ^ val;

            bits = size * 8;
            while ((i = find_first_bit(&old_data, bits)) != bits) {
                level = test_bit(i, &value);
                extioi_update_irq(s, i + reg_count * 8, level);
                clear_bit(i, &old_data);
            }
        }
        break;
    case EXTIOI_BOUNCE_START ... EXTIOI_BOUNCE_END - 1:
        stl_p((void *)s->bounce + (offset - EXTIOI_BOUNCE_START), val);
        break;
    case EXTIOI_COREISR_START ... EXTIOI_COREISR_END - 1:
        reg_count = ((offset - EXTIOI_COREISR_START) & 0x1f);
        cpu = ((offset - EXTIOI_COREISR_START) >> 8) & 0x3;
        /* Ext_core_ioisr */
        old_data = ldl_p((void *)s->coreisr[cpu] + reg_count);
        stl_p((void *)s->coreisr[cpu] + reg_count, (old_data & ~val));

        if (old_data != (old_data & ~val)) {
            bits = size * 8;
            while ((i = find_first_bit(&value, bits)) != bits) {
                j = test_bit(i, &old_data);
                if (j) {
                    extioi_update_irq(s, i + reg_count * 8, 0);
                }
                clear_bit(i, &value);
            }
        }
        break;
    case EXTIOI_IPMAP_START ... EXTIOI_IPMAP_END - 1:
        /* Drop arch.core_ip_mask use s->ipmap */
        reg_count = (offset - EXTIOI_IPMAP_START);
        stl_p((void *)&s->ipmap + reg_count, val);
        break;
    case EXTIOI_COREMAP_START ... EXTIOI_COREMAP_END - 1:
        reg_count = (offset - EXTIOI_COREMAP_START);
        /* Only map the core */
        while (val) {
            stl_p((void *)s->coremap + reg_count, val);
            val = val >> 8;
        }
        break;
    case EXTIOI_NODETYPE_START ... EXTIOI_NODETYPE_END - 1:
        stl_p((void *)s->nodetype + (offset - EXTIOI_NODETYPE_START), val);
        break;
    default:
        break;
    }
}

static void extioi_writel(void *opaque, hwaddr addr, uint64_t val,
                          unsigned size)
{
    LoongArchExtIOI *s = LOONGARCH_EXTIOI(opaque);
    int cpu, level, i, j, bits;
    uint64_t offset, old_data, reg_count;

    offset = addr & 0xffff;
    trace_loongarch_extioi_writel(size, (uint32_t)addr, val);
    switch (offset) {
    case EXTIOI_ENABLE_START ... EXTIOI_ENABLE_END - 1:
        reg_count = (offset - EXTIOI_ENABLE_START);
        old_data = s->enable[reg_count];
        if (old_data != val) {
            s->enable[reg_count] = val;
            old_data = old_data ^ val;

            bits = size * 8;
            while ((i = find_first_bit(&old_data, bits)) != bits) {
                level = test_bit(i, &val);
                extioi_update_irq(s, i + reg_count * 8, level);
                clear_bit(i, &old_data);
            }
        }
        break;
    case EXTIOI_BOUNCE_START ... EXTIOI_BOUNCE_END - 1:
        stq_p((void *)s->bounce + (offset - EXTIOI_BOUNCE_START), val);
        break;
    case EXTIOI_COREISR_START ... EXTIOI_COREISR_END - 1:
        reg_count = ((offset - EXTIOI_COREISR_START) & 0x1f);
        cpu = ((offset - EXTIOI_COREISR_START) >> 8) & 0x3;

        /* core_ext_ioisr */
        old_data = ldq_p((void *)s->coreisr[cpu] + reg_count);
        stq_p((void *)s->coreisr[cpu] + reg_count, (old_data & ~val));

        if (old_data != (old_data & ~val)) {
            bits = size * 8;
            while ((i = find_first_bit(&val, bits)) != bits) {
                j = test_bit(i, &old_data);
                if (j) {
                    extioi_update_irq(s, i + reg_count * 8, 0);
                }
                clear_bit(i, &val);
            }
        }
        break;
    case EXTIOI_IPMAP_START ... EXTIOI_IPMAP_END - 1:
        /* Drop arch.core_ip_mask use s->ipmap */
        reg_count = (offset - EXTIOI_IPMAP_START);
        stq_p((void *)&s->ipmap + reg_count, val);
        break;
    case EXTIOI_COREMAP_START ... EXTIOI_COREMAP_END - 1:
        reg_count = (offset - EXTIOI_COREMAP_START);
        /* Only map the core */
        while (val) {
            stq_p((void *)s->coremap + reg_count, val);
            val = val >> 8;
        }
        break;
    case EXTIOI_NODETYPE_START ... EXTIOI_NODETYPE_END - 1:
        stq_p((void *)s->nodetype + (offset - EXTIOI_NODETYPE_START), val);
        break;
    default:
        break;
    }
}

static uint64_t extioi_readfn(void *opaque, hwaddr addr, unsigned size)
{
    switch (size) {
    case 1:
        return extioi_readb(opaque, addr);
    case 4:
        return extioi_readw(opaque, addr);
    case 8:
        return extioi_readl(opaque, addr);
    default:
        g_assert_not_reached();
    }
}

static void extioi_writefn(void *opaque, hwaddr addr,
                           uint64_t value, unsigned size)
{
    switch (size) {
    case 1:
        extioi_writeb(opaque, addr, value, size);
        break;
    case 4:
        extioi_writew(opaque, addr, value, size);
        break;
    case 8:
        extioi_writel(opaque, addr, value, size);
        break;
    default:
        g_assert_not_reached();
    }
}

static const MemoryRegionOps extioi_ops = {
    .read = extioi_readfn,
    .write = extioi_writefn,
    .impl.min_access_size = 1,
    .impl.max_access_size = 8,
    .valid.min_access_size = 1,
    .valid.max_access_size = 8,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void loongarch_extioi_realize(DeviceState *dev, Error **errp)
{
    LoongArchMachineState *lams = LOONGARCH_MACHINE(qdev_get_machine());
    MachineState *ms = MACHINE(lams);
    LoongArchExtIOI *p = LOONGARCH_EXTIOI(dev);
    int i, cpu, pin;

    qdev_init_gpio_in(dev, extioi_setirq, EXTIOI_IRQS);

    for (i = 0; i < EXTIOI_IRQS; i++) {
        sysbus_init_irq(SYS_BUS_DEVICE(dev), &p->irq[i]);
    }
    /* two ways to access extioi: iocsr memory and system memory */
    memory_region_init_io(&p->extioi_iocsr_mem, OBJECT(p), &extioi_ops, p,
                          "loongarch_extioi_iocsr", 0x900);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &p->extioi_iocsr_mem);

    memory_region_init_io(&p->extioi_system_mem, OBJECT(p), &extioi_ops, p,
                          "loongarch_extioi", 0x900);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &p->extioi_system_mem);

    for (cpu = 0; cpu < ms->smp.cpus; cpu++) {
        for (pin = 0; pin < LS3A_INTC_IP; pin++) {
            qdev_init_gpio_out(dev, &p->parent_irq[cpu][pin], 1);
        }
    }
}

static const VMStateDescription vmstate_loongarch_extioi = {
    .name = TYPE_LOONGARCH_EXTIOI,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64_ARRAY(enable, LoongArchExtIOI,
                             EXTIOI_IRQS_BITMAP_SIZE / 8),
        VMSTATE_UINT64_ARRAY(bounce, LoongArchExtIOI,
                             EXTIOI_IRQS_BITMAP_SIZE / 8),
        VMSTATE_UINT64_2DARRAY(coreisr, LoongArchExtIOI,
                               MAX_CORES, EXTIOI_IRQS_BITMAP_SIZE / 8),
        VMSTATE_UINT64(ipmap, LoongArchExtIOI),
        VMSTATE_UINT64_ARRAY(coremap, LoongArchExtIOI,
                             EXTIOI_IRQS_COREMAP_SIZE / 8),
        VMSTATE_UINT64_ARRAY(nodetype, LoongArchExtIOI,
                             EXTIOI_IRQS_NODETYPE_SIZE / 4),
        VMSTATE_END_OF_LIST()
    }
};

static void loongarch_extioi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_loongarch_extioi;
    dc->realize = loongarch_extioi_realize;
}

static const TypeInfo loongarch_extioi_info = {
    .name          = TYPE_LOONGARCH_EXTIOI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(struct LoongArchExtIOI),
    .class_init    = loongarch_extioi_class_init,
};

static void loongarch_extioi_register_types(void)
{
    type_register_static(&loongarch_extioi_info);
}

type_init(loongarch_extioi_register_types)
