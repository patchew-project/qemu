/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2023 IBM Corp.
 *
 * ASPEED APB-OPB FSI interface
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qom/object.h"
#include "qapi/error.h"
#include "trace.h"

#include "hw/fsi/aspeed-apb2opb.h"
#include "hw/qdev-core.h"

#define TO_REG(x) (x >> 2)
#define GENMASK(t, b) (((1ULL << ((t) + 1)) - 1) & ~((1ULL << (b)) - 1))

#define APB2OPB_VERSION                    TO_REG(0x00)
#define   APB2OPB_VERSION_VER              GENMASK(7, 0)

#define APB2OPB_TRIGGER                    TO_REG(0x04)
#define   APB2OPB_TRIGGER_EN               BIT(0)

#define APB2OPB_CONTROL                    TO_REG(0x08)
#define   APB2OPB_CONTROL_OFF              GENMASK(31, 13)

#define APB2OPB_OPB2FSI                    TO_REG(0x0c)
#define   APB2OPB_OPB2FSI_OFF              GENMASK(31, 22)

#define APB2OPB_OPB0_SEL                   TO_REG(0x10)
#define APB2OPB_OPB1_SEL                   TO_REG(0x28)
#define   APB2OPB_OPB_SEL_EN               BIT(0)

#define APB2OPB_OPB0_MODE                  TO_REG(0x14)
#define APB2OPB_OPB1_MODE                  TO_REG(0x2c)
#define   APB2OPB_OPB_MODE_RD              BIT(0)

#define APB2OPB_OPB0_XFER                  TO_REG(0x18)
#define APB2OPB_OPB1_XFER                  TO_REG(0x30)
#define   APB2OPB_OPB_XFER_FULL            BIT(1)
#define   APB2OPB_OPB_XFER_HALF            BIT(0)

#define APB2OPB_OPB0_ADDR                  TO_REG(0x1c)
#define APB2OPB_OPB0_WRITE_DATA            TO_REG(0x20)

#define APB2OPB_OPB1_DMA_EN                TO_REG(0x24)
#define APB2OPB_OPB1_DMA_EN_3              BIT(3)
#define APB2OPB_OPB1_DMA_EN_2              BIT(2)
#define APB2OPB_OPB1_DMA_EN_1              BIT(1)
#define APB2OPB_OPB1_DMA_EN_0              BIT(0)

#define APB2OPB_OPB1_ADDR                  TO_REG(0x34)
#define APB2OPB_OPB1_WRITE_DATA                  TO_REG(0x38)

#define APB2OPB_OPB_CLK                    TO_REG(0x3c)
#define   APB2OPB_OPB_CLK_SYNC             BIT(0)

#define APB2OPB_IRQ_CLEAR                  TO_REG(0x40)
#define   APB2OPB_IRQ_CLEAR_EN             BIT(0)

#define APB2OPB_IRQ_MASK                   TO_REG(0x44)
#define   APB2OPB_IRQ_MASK_OPB1_TX_ACK     BIT(17)
#define   APB2OPB_IRQ_MASK_OPB0_TX_ACK     BIT(16)
#define   APB2OPB_IRQ_MASK_CH3_TCONT       BIT(15)
#define   APB2OPB_IRQ_MASK_CH2_TCONT       BIT(14)
#define   APB2OPB_IRQ_MASK_CH1_TCONT       BIT(13)
#define   APB2OPB_IRQ_MASK_CH0_TCONT       BIT(12)
#define   APB2OPB_IRQ_MASK_CH3_FIFO_EMPTY  BIT(11)
#define   APB2OPB_IRQ_MASK_CH2_FIFO_EMPTY  BIT(10)
#define   APB2OPB_IRQ_MASK_CH1_FIFO_EMPTY  BIT(9)
#define   APB2OPB_IRQ_MASK_CH0_FIFO_EMPTY  BIT(8)
#define   APB2OPB_IRQ_MASK_CH3_FIFO_FULL   BIT(7)
#define   APB2OPB_IRQ_MASK_CH2_FIFO_FULL   BIT(6)
#define   APB2OPB_IRQ_MASK_CH1_FIFO_FULL   BIT(5)
#define   APB2OPB_IRQ_MASK_CH0_FIFO_FULL   BIT(4)
#define   APB2OPB_IRQ_MASK_CH3_DMA_EOT     BIT(3)
#define   APB2OPB_IRQ_MASK_CH2_DMA_EOT     BIT(2)
#define   APB2OPB_IRQ_MASK_CH1_DMA_EOT     BIT(1)
#define   APB2OPB_IRQ_MASK_CH0_DMA_EOT     BIT(0)

#define APB2OPB_IRQ_STS                    TO_REG(0x48)
#define   APB2OPB_IRQ_STS_MASTER_ERROR     BIT(28)
#define   APB2OPB_IRQ_STS_PORT_ERROR       BIT(27)
#define   APB2OPB_IRQ_STS_HOTPLUG          BIT(26)
#define   APB2OPB_IRQ_STS_SLAVE_7          BIT(25)
#define   APB2OPB_IRQ_STS_SLAVE_6          BIT(24)
#define   APB2OPB_IRQ_STS_SLAVE_5          BIT(23)
#define   APB2OPB_IRQ_STS_SLAVE_4          BIT(22)
#define   APB2OPB_IRQ_STS_SLAVE_3          BIT(21)
#define   APB2OPB_IRQ_STS_SLAVE_2          BIT(20)
#define   APB2OPB_IRQ_STS_SLAVE_1          BIT(19)
#define   APB2OPB_IRQ_STS_SLAVE_0          BIT(18)
#define   APB2OPB_IRQ_STS_OPB1_TX_ACK      BIT(17)
#define   APB2OPB_IRQ_STS_OPB0_TX_ACK      BIT(16)
#define   APB2OPB_IRQ_STS_CH3_TCONT        BIT(15)
#define   APB2OPB_IRQ_STS_CH2_TCONT        BIT(14)
#define   APB2OPB_IRQ_STS_CH1_TCONT        BIT(13)
#define   APB2OPB_IRQ_STS_CH0_TCONT        BIT(12)
#define   APB2OPB_IRQ_STS_CH3_FIFO_EMPTY   BIT(11)
#define   APB2OPB_IRQ_STS_CH2_FIFO_EMPTY   BIT(10)
#define   APB2OPB_IRQ_STS_CH1_FIFO_EMPTY   BIT(9)
#define   APB2OPB_IRQ_STS_CH0_FIFO_EMPTY   BIT(8)
#define   APB2OPB_IRQ_STS_CH3_FIFO_FULL    BIT(7)
#define   APB2OPB_IRQ_STS_CH2_FIFO_FULL    BIT(6)
#define   APB2OPB_IRQ_STS_CH1_FIFO_FULL    BIT(5)
#define   APB2OPB_IRQ_STS_CH0_FIFO_FULL    BIT(4)
#define   APB2OPB_IRQ_STS_CH3_DMA_EOT      BIT(3)
#define   APB2OPB_IRQ_STS_CH2_DMA_EOT      BIT(2)
#define   APB2OPB_IRQ_STS_CH1_DMA_EOT      BIT(1)
#define   APB2OPB_IRQ_STS_CH0_DMA_EOT      BIT(0)

#define APB2OPB_OPB0_WRITE_WORD_ENDIAN     TO_REG(0x4c)
#define   APB2OPB_OPB0_WRITE_WORD_ENDIAN_BE 0x0011101b
#define APB2OPB_OPB0_WRITE_BYTE_ENDIAN     TO_REG(0x50)
#define   APB2OPB_OPB0_WRITE_BYTE_ENDIAN_BE 0x0c330f3f
#define APB2OPB_OPB1_WRITE_WORD_ENDIAN     TO_REG(0x54)
#define APB2OPB_OPB1_WRITE_BYTE_ENDIAN     TO_REG(0x58)
#define APB2OPB_OPB0_READ_BYTE_ENDIAN      TO_REG(0x5c)
#define   APB2OPB_OPB0_READ_WORD_ENDIAN_BE  0x00030b1b
#define APB2OPB_OPB1_READ_BYTE_ENDIAN      TO_REG(0x60)

#define APB2OPB_RETRY                      TO_REG(0x64)
#define   APB2OPB_RETRY_COUNTER            GENMASK(15, 0)

#define APB2OPB_OPB0_STATUS                TO_REG(0x80)
#define APB2OPB_OPB1_STATUS                TO_REG(0x8c)
#define   APB2OPB_OPB_STATUS_TIMEOUT       BIT(4)
#define   APB2OPB_OPB_STATUS_RETRY         BIT(3)
#define   APB2OPB_OPB_STATUS_ERROR_ACK     BIT(2)
#define   APB2OPB_OPB_STATUS_FW_ACK        BIT(1)
#define   APB2OPB_OPB_STATUS_HW_ACK        BIT(0)

#define APB2OPB_OPB0_READ_DATA         TO_REG(0x84)

#define APB2OPB_OPB1_DMA_STATUS            TO_REG(0x88)
#define   APB2OPB_OPB1_DMA_STATUS_CH3_EOT  BIT(7)
#define   APB2OPB_OPB1_DMA_STATUS_CH2_EOT  BIT(6)
#define   APB2OPB_OPB1_DMA_STATUS_CH1_EOT  BIT(5)
#define   APB2OPB_OPB1_DMA_STATUS_CH0_EOT  BIT(4)
#define   APB2OPB_OPB1_DMA_STATUS_CH3_REQ  BIT(3)
#define   APB2OPB_OPB1_DMA_STATUS_CH2_REQ  BIT(2)
#define   APB2OPB_OPB1_DMA_STATUS_CH1_REQ  BIT(1)
#define   APB2OPB_OPB1_DMA_STATUS_CH0_REQ  BIT(0)

#define APB2OPB_OPB1_READ_DATA         TO_REG(0x90)

static uint64_t aspeed_apb2opb_read(void *opaque, hwaddr addr, unsigned size)
{
    AspeedAPB2OPBState *s = ASPEED_APB2OPB(opaque);

    trace_aspeed_apb2opb_read(addr, size);

    assert(!(addr & 3));
    assert(size == 4);

    if (addr + size > sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out of bounds read: 0x%"HWADDR_PRIx" for %u\n",
                      __func__, addr, size);
        return 0;
    }

    return s->regs[TO_REG(addr)];
}

static void aspeed_apb2opb_write(void *opaque, hwaddr addr, uint64_t data,
                                 unsigned size)
{
    AspeedAPB2OPBState *s = ASPEED_APB2OPB(opaque);

    trace_aspeed_apb2opb_write(addr, size, data);

    assert(!(addr & 3));
    assert(size == 4);

    if (addr + size > sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out of bounds write: %"HWADDR_PRIx" for %u\n",
                      __func__, addr, size);
        return;
    }

    switch (TO_REG(addr)) {
    case APB2OPB_CONTROL:
        opb_fsi_master_address(&s->opb[0], data & APB2OPB_CONTROL_OFF);
        break;
    case APB2OPB_OPB2FSI:
        opb_opb2fsi_address(&s->opb[0], data & APB2OPB_OPB2FSI_OFF);
        break;
    case APB2OPB_OPB0_WRITE_WORD_ENDIAN:
        if (data != APB2OPB_OPB0_WRITE_WORD_ENDIAN_BE) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Bridge needs to be driven as BE (0x%x)\n",
                          __func__, APB2OPB_OPB0_WRITE_WORD_ENDIAN_BE);
        }
        break;
    case APB2OPB_OPB0_WRITE_BYTE_ENDIAN:
        if (data != APB2OPB_OPB0_WRITE_BYTE_ENDIAN_BE) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Bridge needs to be driven as BE (0x%x)\n",
                          __func__, APB2OPB_OPB0_WRITE_BYTE_ENDIAN_BE);
        }
        break;
    case APB2OPB_OPB0_READ_BYTE_ENDIAN:
        if (data != APB2OPB_OPB0_READ_WORD_ENDIAN_BE) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Bridge needs to be driven as BE (0x%x)\n",
                          __func__, APB2OPB_OPB0_READ_WORD_ENDIAN_BE);
        }
        break;
    case APB2OPB_TRIGGER:
    {
        uint32_t opb, op_mode, op_size, op_addr, op_data;

        assert((s->regs[APB2OPB_OPB0_SEL] & APB2OPB_OPB_SEL_EN) ^
               (s->regs[APB2OPB_OPB1_SEL] & APB2OPB_OPB_SEL_EN));

        if (s->regs[APB2OPB_OPB0_SEL] & APB2OPB_OPB_SEL_EN) {
            opb = 0;
            op_mode = s->regs[APB2OPB_OPB0_MODE];
            op_size = s->regs[APB2OPB_OPB0_XFER];
            op_addr = s->regs[APB2OPB_OPB0_ADDR];
            op_data = s->regs[APB2OPB_OPB0_WRITE_DATA];
        } else if (s->regs[APB2OPB_OPB1_SEL] & APB2OPB_OPB_SEL_EN) {
            opb = 1;
            op_mode = s->regs[APB2OPB_OPB1_MODE];
            op_size = s->regs[APB2OPB_OPB1_XFER];
            op_addr = s->regs[APB2OPB_OPB1_ADDR];
            op_data = s->regs[APB2OPB_OPB1_WRITE_DATA];
        } else {
            g_assert_not_reached();
        }

        if (op_size & ~(APB2OPB_OPB_XFER_HALF | APB2OPB_OPB_XFER_FULL)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "OPB transaction failed: Unrecognised access width: %d\n",
                          op_size);
            return;
        }

        op_size += 1;

        if (op_mode & APB2OPB_OPB_MODE_RD) {
            int index = opb ? APB2OPB_OPB1_READ_DATA
                : APB2OPB_OPB0_READ_DATA;

            switch (op_size) {
            case 1:
                s->regs[index] = opb_read8(&s->opb[opb], op_addr);
                break;
            case 2:
                s->regs[index] = opb_read16(&s->opb[opb], op_addr);
                break;
            case 4:
                s->regs[index] = opb_read32(&s->opb[opb], op_addr);
                break;
            default:
                g_assert_not_reached(); /* should have bailed above */
            }
        } else {
            /* FIXME: Endian swizzling */
            switch (op_size) {
            case 1:
                opb_write8(&s->opb[opb], op_addr, op_data);
                break;
            case 2:
                opb_write16(&s->opb[opb], op_addr, op_data);
                break;
            case 4:
                opb_write32(&s->opb[opb], op_addr, op_data);
                break;
            default:
                g_assert_not_reached(); /* should have bailed above */
            }
        }
        s->regs[APB2OPB_IRQ_STS] |= opb ? APB2OPB_IRQ_STS_OPB1_TX_ACK
            : APB2OPB_IRQ_STS_OPB0_TX_ACK;
        break;
    }
    }

    s->regs[TO_REG(addr)] = data;
}

static const struct MemoryRegionOps aspeed_apb2opb_ops = {
    .read = aspeed_apb2opb_read,
    .write = aspeed_apb2opb_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void aspeed_apb2opb_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    AspeedAPB2OPBState *s = ASPEED_APB2OPB(dev);

    qbus_init(&s->opb[0], sizeof(s->opb[0]), TYPE_OP_BUS,
                        DEVICE(s), NULL);
    qbus_init(&s->opb[1], sizeof(s->opb[1]), TYPE_OP_BUS,
                        DEVICE(s), NULL);

    sysbus_init_irq(sbd, &s->irq);

    memory_region_init_io(&s->iomem, OBJECT(s), &aspeed_apb2opb_ops, s,
                          TYPE_ASPEED_APB2OPB, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void aspeed_apb2opb_reset(DeviceState *dev)
{
    AspeedAPB2OPBState *s = ASPEED_APB2OPB(dev);

    memset(s->regs, 0, sizeof(s->regs));

    s->regs[APB2OPB_VERSION] = 0x000000a1;
    s->regs[APB2OPB_OPB0_WRITE_WORD_ENDIAN] = 0x0044eee4;
    s->regs[APB2OPB_OPB0_WRITE_BYTE_ENDIAN] = 0x0055aaff;
    s->regs[APB2OPB_OPB1_WRITE_WORD_ENDIAN] = 0x00117717;
    s->regs[APB2OPB_OPB1_WRITE_BYTE_ENDIAN] = 0xffaa5500;
    s->regs[APB2OPB_OPB0_READ_BYTE_ENDIAN] = 0x0044eee4;
    s->regs[APB2OPB_OPB0_READ_BYTE_ENDIAN] = 0x00117717;
}

static void aspeed_apb2opb_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "ASPEED APB2OPB Bridge";
    dc->realize = aspeed_apb2opb_realize;
    dc->reset = aspeed_apb2opb_reset;
}

static const TypeInfo aspeed_apb2opb_info = {
    .name = TYPE_ASPEED_APB2OPB,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AspeedAPB2OPBState),
    .class_init = aspeed_apb2opb_class_init,
};

static void aspeed_apb2opb_register_types(void)
{
    type_register_static(&aspeed_apb2opb_info);
}

type_init(aspeed_apb2opb_register_types);
