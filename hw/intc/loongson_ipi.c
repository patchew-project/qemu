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

static MemTxResult loongson_ipi_core_readl(void *opaque, hwaddr addr,
                                           uint64_t *data,
                                           unsigned size, MemTxAttrs attrs)
{
    IPICore *s = opaque;
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

    trace_loongson_ipi_read(size, (uint64_t)addr, ret);
    *data = ret;
    return MEMTX_OK;
}

static MemTxResult loongson_ipi_iocsr_readl(void *opaque, hwaddr addr,
                                            uint64_t *data,
                                            unsigned size, MemTxAttrs attrs)
{
    LoongsonIPICommonState *ipi = opaque;
    IPICore *s;

    if (attrs.requester_id >= ipi->num_cpu) {
        return MEMTX_DECODE_ERROR;
    }

    s = &ipi->cpu[attrs.requester_id];
    return loongson_ipi_core_readl(s, addr, data, size, attrs);
}

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

static MemTxResult send_ipi_data(LoongsonIPICommonState *ipi, CPUState *cpu,
                                 uint64_t val, hwaddr addr, MemTxAttrs attrs)
{
    LoongsonIPICommonClass *licc = LOONGSON_IPI_COMMON_GET_CLASS(ipi);
    int i, mask = 0, data = 0;
    AddressSpace *iocsr_as = licc->get_iocsr_as(cpu);

    if (!iocsr_as) {
        return MEMTX_DECODE_ERROR;
    }

    /*
     * bit 27-30 is mask for byte writing,
     * if the mask is 0, we need not to do anything.
     */
    if ((val >> 27) & 0xf) {
        data = address_space_ldl_le(iocsr_as, addr, attrs, NULL);
        for (i = 0; i < 4; i++) {
            /* get mask for byte writing */
            if (val & (0x1 << (27 + i))) {
                mask |= 0xff << (i * 8);
            }
        }
    }

    data &= mask;
    data |= (val >> 32) & ~mask;
    address_space_stl_le(iocsr_as, addr, data, attrs, NULL);

    return MEMTX_OK;
}

static MemTxResult mail_send(LoongsonIPICommonState *ipi,
                             uint64_t val, MemTxAttrs attrs)
{
    LoongsonIPICommonClass *licc = LOONGSON_IPI_COMMON_GET_CLASS(ipi);
    uint32_t cpuid;
    hwaddr addr;
    CPUState *cs;

    cpuid = extract32(val, 16, 10);
    cs = licc->cpu_by_arch_id(cpuid);
    if (cs == NULL) {
        return MEMTX_DECODE_ERROR;
    }

    /* override requester_id */
    addr = SMP_IPI_MAILBOX + CORE_BUF_20 + (val & 0x1c);
    attrs.requester_id = cs->cpu_index;
    return send_ipi_data(ipi, cs, val, addr, attrs);
}

static MemTxResult any_send(LoongsonIPICommonState *ipi,
                            uint64_t val, MemTxAttrs attrs)
{
    LoongsonIPICommonClass *licc = LOONGSON_IPI_COMMON_GET_CLASS(ipi);
    uint32_t cpuid;
    hwaddr addr;
    CPUState *cs;

    cpuid = extract32(val, 16, 10);
    cs = licc->cpu_by_arch_id(cpuid);
    if (cs == NULL) {
        return MEMTX_DECODE_ERROR;
    }

    /* override requester_id */
    addr = val & 0xffff;
    attrs.requester_id = cs->cpu_index;
    return send_ipi_data(ipi, cs, val, addr, attrs);
}

static MemTxResult loongson_ipi_core_writel(void *opaque, hwaddr addr,
                                            uint64_t val, unsigned size,
                                            MemTxAttrs attrs)
{
    IPICore *s = opaque;
    LoongsonIPICommonState *ipi = s->ipi;
    LoongsonIPICommonClass *licc = LOONGSON_IPI_COMMON_GET_CLASS(ipi);
    int index = 0;
    uint32_t cpuid;
    uint8_t vector;
    CPUState *cs;

    addr &= 0xff;
    trace_loongson_ipi_write(size, (uint64_t)addr, val);
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
    case IOCSR_IPI_SEND:
        cpuid = extract32(val, 16, 10);
        /* IPI status vector */
        vector = extract8(val, 0, 5);
        cs = licc->cpu_by_arch_id(cpuid);
        if (cs == NULL || cs->cpu_index >= ipi->num_cpu) {
            return MEMTX_DECODE_ERROR;
        }
        loongson_ipi_core_writel(&ipi->cpu[cs->cpu_index], CORE_SET_OFF,
                                 BIT(vector), 4, attrs);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "invalid write: %x", (uint32_t)addr);
        break;
    }

    return MEMTX_OK;
}

static MemTxResult loongson_ipi_iocsr_writel(void *opaque, hwaddr addr,
                                            uint64_t val, unsigned size,
                                            MemTxAttrs attrs)
{
    LoongsonIPICommonState *ipi = opaque;
    IPICore *s;

    if (attrs.requester_id >= ipi->num_cpu) {
        return MEMTX_DECODE_ERROR;
    }

    s = &ipi->cpu[attrs.requester_id];
    return loongson_ipi_core_writel(s, addr, val, size, attrs);
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

static const MemoryRegionOps loongson_ipi_iocsr_ops = {
    .read_with_attrs = loongson_ipi_iocsr_readl,
    .write_with_attrs = loongson_ipi_iocsr_writel,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 8,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/* mail send and any send only support writeq */
static MemTxResult loongson_ipi_writeq(void *opaque, hwaddr addr, uint64_t val,
                                        unsigned size, MemTxAttrs attrs)
{
    LoongsonIPICommonState *ipi = opaque;
    MemTxResult ret = MEMTX_OK;

    addr &= 0xfff;
    switch (addr) {
    case MAIL_SEND_OFFSET:
        ret = mail_send(ipi, val, attrs);
        break;
    case ANY_SEND_OFFSET:
        ret = any_send(ipi, val, attrs);
        break;
    default:
       break;
    }

    return ret;
}

static const MemoryRegionOps loongson_ipi64_ops = {
    .write_with_attrs = loongson_ipi_writeq,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void loongson_ipi_common_realize(DeviceState *dev, Error **errp)
{
    LoongsonIPICommonState *s = LOONGSON_IPI_COMMON(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    int i;

    if (s->num_cpu == 0) {
        error_setg(errp, "num-cpu must be at least 1");
        return;
    }

    memory_region_init_io(&s->ipi_iocsr_mem, OBJECT(dev),
                          &loongson_ipi_iocsr_ops,
                          s, "loongson_ipi_iocsr", 0x48);

    /* loongson_ipi_iocsr performs re-entrant IO through ipi_send */
    s->ipi_iocsr_mem.disable_reentrancy_guard = true;

    sysbus_init_mmio(sbd, &s->ipi_iocsr_mem);

    memory_region_init_io(&s->ipi64_iocsr_mem, OBJECT(dev),
                          &loongson_ipi64_ops,
                          s, "loongson_ipi64_iocsr", 0x118);
    sysbus_init_mmio(sbd, &s->ipi64_iocsr_mem);

    s->cpu = g_new0(IPICore, s->num_cpu);
    for (i = 0; i < s->num_cpu; i++) {
        s->cpu[i].ipi = s;

        qdev_init_gpio_out(dev, &s->cpu[i].irq, 1);
    }
}

static void loongson_ipi_realize(DeviceState *dev, Error **errp)
{
    LoongsonIPICommonState *sc = LOONGSON_IPI_COMMON(dev);
    LoongsonIPIState *s = LOONGSON_IPI(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    Error *local_err = NULL;

    loongson_ipi_common_realize(dev, &local_err);
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
