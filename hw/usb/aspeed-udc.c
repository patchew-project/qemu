/*
 * ASPEED USB Device Controller (UDC)
 *
 * Copyright (c) 2026 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Models the ASPEED USB Device Controller (UDC), driven by the Linux
 * "aspeed_udc" gadget driver. It implements one control endpoint (EP0) and
 * 4 programmable endpoints.
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
    FIELD(UDC_FUNC_CTRL, UPSTREAM_FS,       1, 1)
    FIELD(UDC_FUNC_CTRL, STOP_CLK_SUSPEND,  2, 1)
    FIELD(UDC_FUNC_CTRL, AUTO_REMOTE_WKUP,  3, 1)
    FIELD(UDC_FUNC_CTRL, REMOTE_WKUP_EN,    4, 1)
    FIELD(UDC_FUNC_CTRL, TEST_MODE,         8, 3)
    FIELD(UDC_FUNC_CTRL, PHY_RESET_DIS,    11, 1)
    FIELD(UDC_FUNC_CTRL, EP_LONG_DESC,     18, 1)
    FIELD(UDC_FUNC_CTRL, PHY_CLK_EN,       31, 1)
REG32(UDC_CONFIG, 0x04)
    FIELD(UDC_CONFIG, DEV_ADDR,             0, 7)
REG32(UDC_IER, 0x08)
REG32(UDC_ISR, 0x0C)
    FIELD(UDC_ISR, EP0_SETUP,               0, 1)
    FIELD(UDC_ISR, EP0_OUT_ACK,             1, 1)
    FIELD(UDC_ISR, EP0_OUT_NAK,             2, 1)
    FIELD(UDC_ISR, EP0_IN_ACK,              3, 1)
    FIELD(UDC_ISR, EP0_IN_NAK,              4, 1)
    FIELD(UDC_ISR, BUS_RESET,               6, 1)
    FIELD(UDC_ISR, SUSPEND,                 7, 1)
    FIELD(UDC_ISR, RESUME,                  8, 1)
    FIELD(UDC_ISR, EP_POOL_ACK,            16, 1)
    FIELD(UDC_ISR, EP_POOL_NAK,            17, 1)
REG32(UDC_EP_ACK_IER, 0x10)
REG32(UDC_EP_NAK_IER, 0x14)
REG32(UDC_EP_ACK_ISR, 0x18)
REG32(UDC_EP_NAK_ISR, 0x1C)
REG32(UDC_DEV_RESET, 0x20)
    FIELD(UDC_DEV_RESET, ROOT,              0, 1)
    FIELD(UDC_DEV_RESET, DMA,               8, 1)
    FIELD(UDC_DEV_RESET, EP_POOL,           9, 1)
REG32(UDC_STS, 0x24)
    FIELD(UDC_STS, HIGHSPEED,              27, 1)
REG32(UDC_EP_DATA, 0x28)
REG32(UDC_ISO_TX_FAIL, 0x2C)
REG32(UDC_EP0_CTRL, 0x30)
    FIELD(UDC_EP0_CTRL, STALL,              0, 1)
    FIELD(UDC_EP0_CTRL, TX_RDY,             1, 1)
    FIELD(UDC_EP0_CTRL, RX_RDY,             2, 1)
    FIELD(UDC_EP0_CTRL, TX_LEN,             8, 7)
    FIELD(UDC_EP0_CTRL, RX_LEN,            16, 7)
REG32(UDC_EP0_DATA_BUFF, 0x34)
/* EP0 SETUP packet buffer: SETUP0 = bytes 0...3, SETUP1 = bytes 4...7 */
REG32(UDC_SETUP0, 0x80)
REG32(UDC_SETUP1, 0x84)

/* Per programmable-endpoint registers (offset from the EP register base) */
REG32(EP_CONFIG, 0x00)
    FIELD(EP_CONFIG, ENABLE,                0, 1)
    FIELD(EP_CONFIG, DIR_OUT,               4, 1)
    FIELD(EP_CONFIG, TYPE,                  5, 2)
    FIELD(EP_CONFIG, EP_NUM,                8, 4)
    FIELD(EP_CONFIG, STALL,                12, 1)
    FIELD(EP_CONFIG, AUTO_TOGGLE_DIS,      13, 1)
    FIELD(EP_CONFIG, MAX_PKT,              16, 10)
REG32(EP_DMA_CTRL, 0x04)
    FIELD(EP_DMA_CTRL, DESC_OP_EN,          0, 1)
    FIELD(EP_DMA_CTRL, SINGLE_STAGE,        1, 1)
    FIELD(EP_DMA_CTRL, RESET,               2, 1)
    FIELD(EP_DMA_CTRL, IN_LONG_MODE,        3, 1)
    FIELD(EP_DMA_CTRL, PROC_STS,            4, 4)
REG32(EP_DMA_BUFF, 0x08)
REG32(EP_DMA_STS, 0x0C)
    FIELD(EP_DMA_STS, WPTR,                 0, 8)
    FIELD(EP_DMA_STS, RPTR,                 8, 8)
    FIELD(EP_DMA_STS, TX_SIZE,             16, 11)

/* Device-reset default: root, DMA and EP-pool soft-reset bits set (0x301) */
#define UDC_DEV_RESET_DEFAULT \
    (R_UDC_DEV_RESET_ROOT_MASK | R_UDC_DEV_RESET_DMA_MASK | \
     R_UDC_DEV_RESET_EP_POOL_MASK)

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
    uint64_t val = s->regs[offset >> 2];

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
    case R_UDC_ISR:
    case R_UDC_EP_ACK_ISR:
    case R_UDC_EP_NAK_ISR:
        /* Status registers are write-1-to-clear */
        s->regs[reg] &= ~val;
        break;
    default:
        s->regs[reg] = val;
        break;
    }

    aspeed_udc_update_irq(s);
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
    uint64_t val = e->regs[offset >> 2];

    trace_aspeed_udc_ep_read(e->index, offset, val);
    return val;
}

static void aspeed_udc_ep_write(void *opaque, hwaddr offset, uint64_t data,
                                unsigned size)
{
    AspeedUDCEP *e = opaque;

    trace_aspeed_udc_ep_write(e->index, offset, data);
    e->regs[offset >> 2] = data;
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

static void aspeed_udc_realize(DeviceState *dev, Error **errp)
{
    AspeedUDCState *s = ASPEED_UDC(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    int i;

    s->regs = g_new0(uint32_t, ASPEED_UDC_NR_REGS);

    memory_region_init(&s->iomem, OBJECT(s), TYPE_ASPEED_UDC,
                       ASPEED_UDC_REG_SIZE);

    /* Root/global registers occupy the low part of the window */
    memory_region_init_io(&s->reg_mr, OBJECT(s), &aspeed_udc_ops, s,
                          TYPE_ASPEED_UDC ".regs", ASPEED_UDC_NR_REGS << 2);
    memory_region_add_subregion(&s->iomem, 0, &s->reg_mr);

    /* Each programmable endpoint has its own register bank */
    for (i = 0; i < ASPEED_UDC_NUM_EP; i++) {
        g_autofree char *name = g_strdup_printf(TYPE_ASPEED_UDC ".ep%d", i);

        s->ep[i].index = i;
        s->ep[i].regs = g_new0(uint32_t, ASPEED_UDC_EP_NR_REGS);
        memory_region_init_io(&s->ep[i].mr, OBJECT(s), &aspeed_udc_ep_ops,
                              &s->ep[i], name, ASPEED_UDC_EP_NR_REGS << 2);
        memory_region_add_subregion(&s->iomem, ASPEED_UDC_EP_REG_BASE +
                                    i * ASPEED_UDC_EP_REG_SIZE, &s->ep[i].mr);
    }

    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static void aspeed_udc_reset_hold(Object *obj, ResetType type)
{
    AspeedUDCState *s = ASPEED_UDC(obj);
    int i;

    memset(s->regs, 0, ASPEED_UDC_NR_REGS * sizeof(uint32_t));
    for (i = 0; i < ASPEED_UDC_NUM_EP; i++) {
        memset(s->ep[i].regs, 0, ASPEED_UDC_EP_NR_REGS * sizeof(uint32_t));
    }
    s->regs[R_UDC_DEV_RESET] = UDC_DEV_RESET_DEFAULT;
}

static void aspeed_udc_unrealize(DeviceState *dev)
{
    AspeedUDCState *s = ASPEED_UDC(dev);
    int i;

    for (i = 0; i < ASPEED_UDC_NUM_EP; i++) {
        g_free(s->ep[i].regs);
    }
    g_free(s->regs);
}

static void aspeed_udc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->desc = "ASPEED USB Device Controller";
    dc->realize = aspeed_udc_realize;
    dc->unrealize = aspeed_udc_unrealize;
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
