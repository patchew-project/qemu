/*
 * ASPEED USB Device Controller (UDC)
 *
 * Copyright (c) 2026 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Models the ASPEED USB Device Controller (UDC). It implements one control
 * endpoint (EP0) and 4 programmable endpoints.
 *
 * This file is the system-bus side of the controller: the MMIO register map,
 * the interrupt and the soft reset. The gadget USB device presented to a host
 * controller (and the endpoint data path) is added on top of this.
 */

#include "qemu/osdep.h"
#include "hw/core/irq.h"
#include "hw/core/registerfields.h"
#include "hw/usb/aspeed-udc.h"
#include "qemu/module.h"
#include "trace.h"

/* Root / Global registers (offset from the controller base) */
REG32(UDC_FUNC_CTRL, 0x00)
    FIELD(UDC_FUNC_CTRL, UPSTREAM_EN,       0, 1)
REG32(UDC_CONFIG, 0x04)
    FIELD(UDC_CONFIG, DEV_ADDR,             0, 7)
REG32(UDC_IER, 0x08)
REG32(UDC_ISR, 0x0C)
    FIELD(UDC_ISR, EP_POOL_ACK,            16, 1)
    FIELD(UDC_ISR, BUS_RESET,               6, 1)
    FIELD(UDC_ISR, EP0_IN_ACK,              3, 1)
    FIELD(UDC_ISR, EP0_OUT_ACK,             1, 1)
    FIELD(UDC_ISR, EP0_SETUP,               0, 1)
REG32(UDC_EP_ACK_IER, 0x10)
REG32(UDC_EP_NAK_IER, 0x14)
REG32(UDC_EP_ACK_ISR, 0x18)
REG32(UDC_EP_NAK_ISR, 0x1C)
REG32(UDC_DEV_RESET, 0x20)
    FIELD(UDC_DEV_RESET, EP_POOL,           9, 1)
    FIELD(UDC_DEV_RESET, DMA,               8, 1)
    FIELD(UDC_DEV_RESET, ROOT,              0, 1)
REG32(UDC_STS, 0x24)
    FIELD(UDC_STS, HIGHSPEED,              27, 1)
REG32(UDC_EP_DATA, 0x28)
REG32(UDC_ISO_TX_FAIL, 0x2C)
REG32(UDC_EP0_CTRL, 0x30)
    FIELD(UDC_EP0_CTRL, RX_LEN,            16, 7)
    FIELD(UDC_EP0_CTRL, TX_LEN,             8, 7)
    FIELD(UDC_EP0_CTRL, RX_RDY,             2, 1)
    FIELD(UDC_EP0_CTRL, TX_RDY,             1, 1)
    FIELD(UDC_EP0_CTRL, STALL,              0, 1)
REG32(UDC_EP0_DATA_BUFF, 0x34)
/* EP0 SETUP packet buffer: SETUP0 = bytes 0...3, SETUP1 = bytes 4...7 */
REG32(UDC_SETUP0, 0x80)
REG32(UDC_SETUP1, 0x84)

/* Per programmable-endpoint registers (offset from the EP register base) */
REG32(EP_CONFIG, 0x00)
    FIELD(EP_CONFIG, MAX_PKT,              16, 10)
    FIELD(EP_CONFIG, EP_NUM,                8, 4)
    FIELD(EP_CONFIG, DIR_OUT,               4, 1)
    FIELD(EP_CONFIG, ENABLE,                0, 1)
REG32(EP_DMA_CTRL, 0x04)
    FIELD(EP_DMA_CTRL, PROC_STS,            4, 4)
    FIELD(EP_DMA_CTRL, DESC_OP_EN,          0, 1)
REG32(EP_DMA_BUFF, 0x08)
REG32(EP_DMA_STS, 0x0C)
    FIELD(EP_DMA_STS, PKT_SIZE,            16, 11)
    FIELD(EP_DMA_STS, RPTR,                 8, 8)
    FIELD(EP_DMA_STS, WPTR,                 0, 8)

static void aspeed_udc_update_irq(AspeedUDCState *s)
{
    bool level;

    level = (s->regs[R_UDC_ISR] & s->regs[R_UDC_IER]) ||
            (s->regs[R_UDC_EP_ACK_ISR] & s->regs[R_UDC_EP_ACK_IER]) ||
            (s->regs[R_UDC_EP_NAK_ISR] & s->regs[R_UDC_EP_NAK_IER]);

    trace_aspeed_udc_irq(s->regs[R_UDC_ISR], s->regs[R_UDC_IER], level);
    qemu_set_irq(s->irq, level);
}

static uint64_t aspeed_udc_read(void *opaque, hwaddr offset, unsigned size)
{
    AspeedUDCState *s = ASPEED_UDC(opaque);
    uint32_t reg = offset >> 2;
    uint32_t val;

    val = s->regs[reg];
    trace_aspeed_udc_read(offset, val);

    return val;
}

static void aspeed_udc_write(void *opaque, hwaddr offset, uint64_t data,
                             unsigned size)
{
    AspeedUDCState *s = ASPEED_UDC(opaque);
    uint32_t reg = offset >> 2;
    uint32_t val = data;

    trace_aspeed_udc_write(offset, val);

    switch (reg) {
    case R_UDC_IER:
    case R_UDC_EP_ACK_IER:
    case R_UDC_EP_NAK_IER:
        s->regs[reg] = val;
        aspeed_udc_update_irq(s);
        break;
    case R_UDC_ISR:
    case R_UDC_EP_ACK_ISR:
    case R_UDC_EP_NAK_ISR:
        s->regs[reg] &= ~val;
        aspeed_udc_update_irq(s);
        break;
    default:
        s->regs[reg] = val;
        break;
    }
}

static const MemoryRegionOps aspeed_udc_ops = {
    .read = aspeed_udc_read,
    .write = aspeed_udc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint64_t aspeed_udc_ep_read(void *opaque, hwaddr offset, unsigned size)
{
    AspeedUDCEP *e = opaque;
    uint32_t reg = offset >> 2;
    uint32_t val;

    val = e->regs[reg];
    trace_aspeed_udc_ep_read(e->index, offset, val);

    return val;
}

static void aspeed_udc_ep_write(void *opaque, hwaddr offset, uint64_t data,
                                unsigned size)
{
    AspeedUDCEP *e = opaque;
    uint32_t reg = offset >> 2;

    trace_aspeed_udc_ep_write(e->index, offset, data);
    e->regs[reg] = data;
}

static const MemoryRegionOps aspeed_udc_ep_ops = {
    .read = aspeed_udc_ep_read,
    .write = aspeed_udc_ep_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void aspeed_udc_reset_hold(Object *obj, ResetType type)
{
    AspeedUDCState *s = ASPEED_UDC(obj);
    int i;

    memset(s->regs, 0, sizeof(s->regs));
    for (i = 0; i < ASPEED_UDC_NUM_EP; i++) {
        memset(s->ep[i].regs, 0, sizeof(s->ep[i].regs));
    }

    /* Device-reset default: root, DMA and EP-pool soft-reset bits set */
    s->regs[R_UDC_DEV_RESET] = (R_UDC_DEV_RESET_ROOT_MASK |
                                R_UDC_DEV_RESET_DMA_MASK |
                                R_UDC_DEV_RESET_EP_POOL_MASK);
}

static void aspeed_udc_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    AspeedUDCState *s = ASPEED_UDC(dev);
    int i;

    memory_region_init(&s->udc_container, OBJECT(s), TYPE_ASPEED_UDC,
                       ASPEED_UDC_MEM_SIZE);
    memory_region_init_io(&s->root_mr, OBJECT(s), &aspeed_udc_ops, s,
                          TYPE_ASPEED_UDC ".root",
                          ASPEED_UDC_ROOT_NR_REGS << 2);
    memory_region_add_subregion(&s->udc_container, 0, &s->root_mr);

    /* Each programmable endpoint has its own register bank */
    for (i = 0; i < ASPEED_UDC_NUM_EP; i++) {
        g_autofree char *name = g_strdup_printf(TYPE_ASPEED_UDC ".ep%d", i);

        s->ep[i].index = i;
        memory_region_init_io(&s->ep[i].mr, OBJECT(s), &aspeed_udc_ep_ops,
                              &s->ep[i], name, ASPEED_UDC_EP_NR_REGS << 2);
        memory_region_add_subregion(&s->udc_container,
                                    ASPEED_UDC_EP_REG_BASE +
                                    i * (ASPEED_UDC_EP_NR_REGS << 2),
                                    &s->ep[i].mr);
    }

    sysbus_init_mmio(sbd, &s->udc_container);
    sysbus_init_irq(sbd, &s->irq);
}

static void aspeed_udc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->desc = "ASPEED USB Device Controller";
    dc->realize = aspeed_udc_realize;
    rc->phases.hold = aspeed_udc_reset_hold;
}

static const TypeInfo aspeed_udc_types[] = {
    {
        .name          = TYPE_ASPEED_UDC,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(AspeedUDCState),
        .class_init    = aspeed_udc_class_init,
    },
};

DEFINE_TYPES(aspeed_udc_types)
