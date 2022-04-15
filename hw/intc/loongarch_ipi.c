/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch ipi interrupt support
 *
 * Copyright (C) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/intc/loongarch_ipi.h"
#include "hw/irq.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "exec/address-spaces.h"
#include "hw/loongarch/loongarch.h"
#include "migration/vmstate.h"
#include "trace.h"

static uint64_t loongarch_ipi_readl(void *opaque, hwaddr addr, unsigned size)
{
    LoongArchMachineState *lams = LOONGARCH_MACHINE(qdev_get_machine());
    IPICore *s = lams->ipi_core;
    uint64_t ret = 0;
    int index = 0;

    addr &= 0xff;
    switch (addr) {
    case CORE_STATUS_OFF:
        ret = s->status;
        break;
    case CORE_EN_OFF:
        ret = s->en;
        break;
    case CORE_SET_OFF:
        ret = 0;
        break;
    case CORE_CLEAR_OFF:
        ret = 0;
        break;
    case CORE_BUF_20 ... CORE_BUF_38 + 4:
        index = (addr - CORE_BUF_20) >> 2;
        ret = s->buf[index];
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "invalid read: %x", (uint32_t)addr);
        break;
    }

    trace_loongarch_ipi_read(size, (uint64_t)addr, ret);
    return ret;
}

static void loongarch_ipi_writel(void *opaque, hwaddr addr, uint64_t val,
                                 unsigned size)
{
    LoongArchMachineState *lams = LOONGARCH_MACHINE(qdev_get_machine());
    IPICore *s = lams->ipi_core;
    int index = 0;

    addr &= 0xff;
    trace_loongarch_ipi_write(size, (uint64_t)addr, val);
    switch (addr) {
    case CORE_STATUS_OFF:
        qemu_log_mask(LOG_GUEST_ERROR, "can not be written");
        break;
    case CORE_EN_OFF:
        s->en = val;
        break;
    case CORE_SET_OFF:
        s->status |= val;
        if (s->status != 0 && (s->status & s->en) != 0) {
            qemu_irq_raise(s->irq);
        }
        break;
    case CORE_CLEAR_OFF:
        s->status &= ~val;
        if (s->status == 0 && s->en != 0) {
            qemu_irq_lower(s->irq);
        }
        break;
    case CORE_BUF_20 ... CORE_BUF_38 + 4:
        index = (addr - CORE_BUF_20) >> 2;
        s->buf[index] = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "invalid write: %x", (uint32_t)addr);
        break;
    }
}

static const MemoryRegionOps loongarch_ipi_ops = {
    .read = loongarch_ipi_readl,
    .write = loongarch_ipi_writel,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 8,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void loongarch_ipi_init(Object *obj)
{
    LoongArchIPI *s = LOONGARCH_IPI(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    int cpu;
    LoongArchMachineState *lams = LOONGARCH_MACHINE(qdev_get_machine());

    for (cpu = 0; cpu < MAX_IPI_CORE_NUM; cpu++) {
        memory_region_init_io(&s->ipi_mmio[cpu], obj, &loongarch_ipi_ops,
                              &lams->ipi_core[cpu], "loongarch_ipi", 0x100);
        sysbus_init_mmio(sbd, &s->ipi_mmio[cpu]);
        qdev_init_gpio_out(DEVICE(obj), &lams->ipi_core[cpu].irq, 1);
    }
}

static const VMStateDescription vmstate_loongarch_ipi = {
    .name = TYPE_LOONGARCH_IPI,
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static void loongarch_ipi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_loongarch_ipi;
}

static const TypeInfo loongarch_ipi_info = {
    .name          = TYPE_LOONGARCH_IPI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LoongArchIPI),
    .instance_init = loongarch_ipi_init,
    .class_init    = loongarch_ipi_class_init,
};

static void loongarch_ipi_register_types(void)
{
    type_register_static(&loongarch_ipi_info);
}

type_init(loongarch_ipi_register_types)
