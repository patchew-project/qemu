/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU Loongson 7A1000 I/O interrupt controller.
 *
 * Copyright (C) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/loongarch/loongarch.h"
#include "hw/irq.h"
#include "hw/intc/loongarch_pch_pic.h"
#include "migration/vmstate.h"
#include "trace.h"

static void pch_pic_update_irq(LoongArchPCHPIC *s, uint32_t mask,
                               int level, int hi)
{
    uint32_t val, irq;

    if (level == 1) {
        if (hi) {
            val = mask & s->intirr_hi & (~s->int_mask_hi);
            irq = find_first_bit((unsigned long *)&val, 32);
            if (irq != 32) {
                s->intisr_hi |= 1ULL << irq;
                qemu_set_irq(s->parent_irq[s->htmsi_vector[irq + 32]], 1);
            }
        } else {
            val = mask & s->intirr_lo & (~s->int_mask_lo);
            irq = find_first_bit((unsigned long *)&val, 32);
            if (irq != 32) {
                s->intisr_lo |= 1ULL << irq;
                qemu_set_irq(s->parent_irq[s->htmsi_vector[irq]], 1);
            }
        }
    } else {
        if (hi) {
            val = mask & s->intisr_hi;
            irq = find_first_bit((unsigned long *)&val, 32);
            if (irq != 32) {
                s->intisr_hi &= ~(0x1ULL << irq);
                qemu_set_irq(s->parent_irq[s->htmsi_vector[irq + 32]], 0);
            }
        } else {
            val = mask & s->intisr_lo;
            irq = find_first_bit((unsigned long *)&val, 32);
            if (irq != 32) {
                s->intisr_lo &= ~(0x1ULL << irq);
                qemu_set_irq(s->parent_irq[s->htmsi_vector[irq]], 0);
            }
        }
    }
}

static void pch_pic_irq_handler(void *opaque, int irq, int level)
{
    LoongArchPCHPIC *s = LOONGARCH_PCH_PIC(opaque);
    int hi = 0;
    uint32_t mask;

    assert(irq < PCH_PIC_IRQ_NUM);
    trace_loongarch_pch_pic_irq_handler(irq, level);

    hi = (irq >= 32) ? 1 : 0;
    if (hi) {
        irq = irq - 32;
    }

    mask = 1ULL << irq;

    if (hi) {
        if (s->intedge_hi & mask) {
            /* Edge triggered */
            if (level) {
                if ((s->last_intirr_hi & mask) == 0) {
                    s->intirr_hi |= mask;
                }
                s->last_intirr_hi |= mask;
            } else {
                s->last_intirr_hi &= ~mask;
            }
        } else {
            /* Level triggered */
            if (level) {
                s->intirr_hi |= mask;
                s->last_intirr_hi |= mask;
            } else {
                s->intirr_hi &= ~mask;
                s->last_intirr_hi &= ~mask;
            }
        }
    } else {
        if (s->intedge_lo & mask) {
            /* Edge triggered */
            if (level) {
                if ((s->last_intirr_lo & mask) == 0) {
                    s->intirr_lo |= mask;
                }
                s->last_intirr_lo |= mask;
            } else {
                s->last_intirr_lo &= ~mask;
            }
        } else {
            /* Level triggered */
            if (level) {
                s->intirr_lo |= mask;
                s->last_intirr_lo |= mask;
            } else {
                s->intirr_lo &= ~mask;
                s->last_intirr_lo &= ~mask;
            }

        }
    }
    pch_pic_update_irq(s, mask, level, hi);
}

static uint64_t loongarch_pch_pic_low_readw(void *opaque, hwaddr addr,
                                            unsigned size)
{
    LoongArchPCHPIC *s = LOONGARCH_PCH_PIC(opaque);
    uint64_t val = 0;
    uint32_t offset = addr & 0xfff;

    switch (offset) {
    case PCH_PIC_INT_ID_LO:
        val = PCH_PIC_INT_ID_VAL;
        break;
    case PCH_PIC_INT_ID_HI:
        val = PCH_PIC_INT_ID_NUM;
        break;
    case PCH_PIC_INT_MASK_LO:
        val = s->int_mask_lo;
        break;
    case PCH_PIC_INT_MASK_HI:
        val = s->int_mask_hi;
        break;
    case PCH_PIC_INT_EDGE_LO:
        val = s->intedge_lo;
        break;
    case PCH_PIC_INT_EDGE_HI:
        val = s->intedge_hi;
        break;
    case PCH_PIC_HTMSI_EN_LO:
        val = s->htmsi_en_lo;
        break;
    case PCH_PIC_HTMSI_EN_HI:
        val = s->htmsi_en_hi;
        break;
    case PCH_PIC_AUTO_CTRL0_LO:
    case PCH_PIC_AUTO_CTRL0_HI:
    case PCH_PIC_AUTO_CTRL1_LO:
    case PCH_PIC_AUTO_CTRL1_HI:
        break;
    default:
        break;
    }

    trace_loongarch_pch_pic_low_readw(size, (uint32_t)addr, val);
    return val;
}

static void loongarch_pch_pic_low_writew(void *opaque, hwaddr addr,
                                         uint64_t data, unsigned size)
{
    LoongArchPCHPIC *s = LOONGARCH_PCH_PIC(opaque);
    uint32_t offset, old;
    offset = addr & 0xfff;

    trace_loongarch_pch_pic_low_writew(size, (uint32_t)addr, data);

    switch (offset) {
    case PCH_PIC_INT_MASK_LO:
        old = s->int_mask_lo;
        s->int_mask_lo = data;
        if (old & ~data) {
            pch_pic_update_irq(s, (old & ~data), 1, 0);
        } else if (~old & data) {
            pch_pic_update_irq(s, (~old & data), 0, 0);
        }
        break;
    case PCH_PIC_INT_MASK_HI:
        old = s->int_mask_hi;
        s->int_mask_hi = data;
        if (old & ~data) {
            pch_pic_update_irq(s, (old & ~data), 1, 1);
        } else if (~old & data) {
            pch_pic_update_irq(s, (~old & data), 0, 1);
        }
        break;
    case PCH_PIC_INT_EDGE_LO:
        s->intedge_lo = data;
        break;
    case PCH_PIC_INT_EDGE_HI:
        s->intedge_hi = data;
        break;
    case PCH_PIC_INT_CLEAR_LO:
        if (s->intedge_lo & data) {
            s->intirr_lo &= (~data);
            pch_pic_update_irq(s, data, 0, 0);
            s->intisr_lo &= (~data);
        }
        break;
    case PCH_PIC_INT_CLEAR_HI:
        if (s->intedge_hi & data) {
            s->intirr_hi &= (~data);
            pch_pic_update_irq(s, data, 0, 1);
            s->intisr_hi &= (~data);
        }
        break;
    case PCH_PIC_HTMSI_EN_LO:
        s->htmsi_en_lo = data;
        break;
    case PCH_PIC_HTMSI_EN_HI:
        s->htmsi_en_hi = data;
        break;
    case PCH_PIC_AUTO_CTRL0_LO:
    case PCH_PIC_AUTO_CTRL0_HI:
    case PCH_PIC_AUTO_CTRL1_LO:
    case PCH_PIC_AUTO_CTRL1_HI:
        break;
    default:
        break;
    }
}

static uint64_t loongarch_pch_pic_high_readw(void *opaque, hwaddr addr,
                                        unsigned size)
{
    LoongArchPCHPIC *s = LOONGARCH_PCH_PIC(opaque);
    uint64_t val = 0;
    uint32_t offset = addr & 0xfff;

    switch (offset) {
    case STATUS_LO_START:
        val = s->intisr_lo & (~s->int_mask_lo);
        break;
    case STATUS_HI_START:
        val = s->intisr_hi & (~s->int_mask_hi);
        break;
    case POL_LO_START:
        val = s->int_polarity_lo;
        break;
    case POL_HI_START:
        val = s->int_polarity_hi;
        break;
    default:
        break;
    }

    trace_loongarch_pch_pic_high_readw(size, (uint32_t)addr, val);
    return val;
}

static void loongarch_pch_pic_high_writew(void *opaque, hwaddr addr,
                                     uint64_t data, unsigned size)
{
    LoongArchPCHPIC *s = LOONGARCH_PCH_PIC(opaque);
    uint32_t offset;
    offset = addr & 0xfff;

    trace_loongarch_pch_pic_high_writew(size, (uint32_t)addr, data);

    switch (offset) {
    case STATUS_LO_START:
        s->intisr_lo = data;
        break;
    case STATUS_HI_START:
        s->intisr_hi = data;
        break;
    case POL_LO_START:
        s->int_polarity_lo = data;
        break;
    case POL_HI_START:
        s->int_polarity_hi = data;
        break;
    default:
        break;
    }
}

static uint64_t loongarch_pch_pic_readb(void *opaque, hwaddr addr,
                                        unsigned size)
{
    LoongArchPCHPIC *s = LOONGARCH_PCH_PIC(opaque);
    uint64_t val = 0;
    uint32_t offset = (addr & 0xfff) + PCH_PIC_ROUTE_ENTRY_OFFSET;
    int64_t offset_tmp;

    switch (offset) {
    case PCH_PIC_HTMSI_VEC_OFFSET ... PCH_PIC_HTMSI_VEC_END:
        offset_tmp = offset - PCH_PIC_HTMSI_VEC_OFFSET;
        if (offset_tmp >= 0 && offset_tmp < 64) {
            val = s->htmsi_vector[offset_tmp];
        }
        break;
    case PCH_PIC_ROUTE_ENTRY_OFFSET ... PCH_PIC_ROUTE_ENTRY_END:
        offset_tmp = offset - PCH_PIC_ROUTE_ENTRY_OFFSET;
        if (offset_tmp >= 0 && offset_tmp < 64) {
            val = s->route_entry[offset_tmp];
        }
        break;
    default:
        break;
    }

    trace_loongarch_pch_pic_readb(size, (uint32_t)addr, val);
    return val;
}

static void loongarch_pch_pic_writeb(void *opaque, hwaddr addr,
                                     uint64_t data, unsigned size)
{
    LoongArchPCHPIC *s = LOONGARCH_PCH_PIC(opaque);
    int32_t offset_tmp;
    uint32_t offset = (addr & 0xfff) + PCH_PIC_ROUTE_ENTRY_OFFSET;

    trace_loongarch_pch_pic_writeb(size, (uint32_t)addr, data);

    switch (offset) {
    case PCH_PIC_HTMSI_VEC_OFFSET ... PCH_PIC_HTMSI_VEC_END:
        offset_tmp = offset - PCH_PIC_HTMSI_VEC_OFFSET;
        if (offset_tmp >= 0 && offset_tmp < 64) {
            s->htmsi_vector[offset_tmp] = (uint8_t)(data & 0xff);
        }
        break;
    case PCH_PIC_ROUTE_ENTRY_OFFSET ... PCH_PIC_ROUTE_ENTRY_END:
        offset_tmp = offset - PCH_PIC_ROUTE_ENTRY_OFFSET;
        if (offset_tmp >= 0 && offset_tmp < 64) {
            s->route_entry[offset_tmp] = (uint8_t)(data & 0xff);
        }
        break;
    default:
        break;
    }
}

static const MemoryRegionOps loongarch_pch_pic_reg32_low_ops = {
    .read = loongarch_pch_pic_low_readw,
    .write = loongarch_pch_pic_low_writew,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const MemoryRegionOps loongarch_pch_pic_reg32_high_ops = {
    .read = loongarch_pch_pic_high_readw,
    .write = loongarch_pch_pic_high_writew,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const MemoryRegionOps loongarch_pch_pic_reg8_ops = {
    .read = loongarch_pch_pic_readb,
    .write = loongarch_pch_pic_writeb,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void loongarch_pch_pic_reset(DeviceState *d)
{
    LoongArchPCHPIC *s = LOONGARCH_PCH_PIC(d);
    int i;

    s->int_mask_lo = -1;
    s->int_mask_hi = -1;
    s->htmsi_en_lo = 0x0;
    s->htmsi_en_hi = 0x0;
    s->intedge_lo  = 0x0;
    s->intedge_hi  = 0x0;
    s->intclr_lo   = 0x0;
    s->intclr_hi   = 0x0;
    s->auto_crtl0_lo = 0x0;
    s->auto_crtl0_hi = 0x0;
    s->auto_crtl1_lo = 0x0;
    s->auto_crtl1_hi = 0x0;
    for (i = 0; i < 64; i++) {
        s->route_entry[i] = 0x1;
        s->htmsi_vector[i] = 0x0;
    }
    s->intirr_lo = 0x0;
    s->intirr_hi = 0x0;
    s->intisr_lo = 0x0;
    s->intisr_hi = 0x0;
    s->last_intirr_lo = 0x0;
    s->last_intirr_hi = 0x0;
    s->int_polarity_lo = 0x0;
    s->int_polarity_hi = 0x0;
}

static void loongarch_pch_pic_init(Object *obj)
{
    LoongArchPCHPIC *s = LOONGARCH_PCH_PIC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    int i;

    memory_region_init_io(&s->iomem32_low, obj,
                          &loongarch_pch_pic_reg32_low_ops,
                          s, PCH_PIC_NAME(.reg32_part1), 0x100);
    memory_region_init_io(&s->iomem8, obj, &loongarch_pch_pic_reg8_ops,
                          s, PCH_PIC_NAME(.reg8), 0x2a0);
    memory_region_init_io(&s->iomem32_high, obj,
                          &loongarch_pch_pic_reg32_high_ops,
                          s, PCH_PIC_NAME(.reg32_part2), 0xc60);
    sysbus_init_mmio(sbd, &s->iomem32_low);
    sysbus_init_mmio(sbd, &s->iomem8);
    sysbus_init_mmio(sbd, &s->iomem32_high);

    for (i = 0; i < PCH_PIC_IRQ_NUM; i++) {
        sysbus_init_irq(sbd, &s->parent_irq[i]);
    }
    qdev_init_gpio_in(DEVICE(obj), pch_pic_irq_handler, PCH_PIC_IRQ_NUM);
}

static const VMStateDescription vmstate_loongarch_pch_pic = {
    .name = TYPE_LOONGARCH_PCH_PIC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(int_mask_lo, LoongArchPCHPIC),
        VMSTATE_UINT32(int_mask_hi, LoongArchPCHPIC),
        VMSTATE_UINT32(htmsi_en_lo, LoongArchPCHPIC),
        VMSTATE_UINT32(htmsi_en_hi, LoongArchPCHPIC),
        VMSTATE_UINT32(intedge_lo, LoongArchPCHPIC),
        VMSTATE_UINT32(intedge_hi, LoongArchPCHPIC),
        VMSTATE_UINT32(intclr_lo, LoongArchPCHPIC),
        VMSTATE_UINT32(intclr_hi, LoongArchPCHPIC),
        VMSTATE_UINT32(auto_crtl0_lo, LoongArchPCHPIC),
        VMSTATE_UINT32(auto_crtl0_hi, LoongArchPCHPIC),
        VMSTATE_UINT32(auto_crtl1_lo, LoongArchPCHPIC),
        VMSTATE_UINT32(auto_crtl1_hi, LoongArchPCHPIC),
        VMSTATE_UINT8_ARRAY(route_entry, LoongArchPCHPIC, 64),
        VMSTATE_UINT8_ARRAY(htmsi_vector, LoongArchPCHPIC, 64),
        VMSTATE_UINT32(last_intirr_lo, LoongArchPCHPIC),
        VMSTATE_UINT32(last_intirr_hi, LoongArchPCHPIC),
        VMSTATE_UINT32(intirr_lo, LoongArchPCHPIC),
        VMSTATE_UINT32(intirr_hi, LoongArchPCHPIC),
        VMSTATE_UINT32(intisr_lo, LoongArchPCHPIC),
        VMSTATE_UINT32(intisr_hi, LoongArchPCHPIC),
        VMSTATE_UINT32(int_polarity_lo, LoongArchPCHPIC),
        VMSTATE_UINT32(int_polarity_hi, LoongArchPCHPIC),
        VMSTATE_END_OF_LIST()
    }
};

static void loongarch_pch_pic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = loongarch_pch_pic_reset;
    dc->vmsd = &vmstate_loongarch_pch_pic;
}

static const TypeInfo loongarch_pch_pic_info = {
    .name          = TYPE_LOONGARCH_PCH_PIC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LoongArchPCHPIC),
    .instance_init = loongarch_pch_pic_init,
    .class_init    = loongarch_pch_pic_class_init,
};

static void loongarch_pch_pic_register_types(void)
{
    type_register_static(&loongarch_pch_pic_info);
}

type_init(loongarch_pch_pic_register_types)
