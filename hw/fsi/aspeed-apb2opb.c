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

#define APB2OPB_OPB1_ADDR                  TO_REG(0x34)
#define APB2OPB_OPB1_WRITE_DATA                  TO_REG(0x38)

#define APB2OPB_IRQ_STS                    TO_REG(0x48)
#define   APB2OPB_IRQ_STS_OPB1_TX_ACK      BIT(17)
#define   APB2OPB_IRQ_STS_OPB0_TX_ACK      BIT(16)

#define APB2OPB_OPB0_WRITE_WORD_ENDIAN     TO_REG(0x4c)
#define   APB2OPB_OPB0_WRITE_WORD_ENDIAN_BE 0x0011101b
#define APB2OPB_OPB0_WRITE_BYTE_ENDIAN     TO_REG(0x50)
#define   APB2OPB_OPB0_WRITE_BYTE_ENDIAN_BE 0x0c330f3f
#define APB2OPB_OPB1_WRITE_WORD_ENDIAN     TO_REG(0x54)
#define APB2OPB_OPB1_WRITE_BYTE_ENDIAN     TO_REG(0x58)
#define APB2OPB_OPB0_READ_BYTE_ENDIAN      TO_REG(0x5c)
#define APB2OPB_OPB1_READ_BYTE_ENDIAN      TO_REG(0x60)
#define   APB2OPB_OPB0_READ_WORD_ENDIAN_BE  0x00030b1b

#define APB2OPB_OPB0_READ_DATA         TO_REG(0x84)
#define APB2OPB_OPB1_READ_DATA         TO_REG(0x90)

/*
 * The following magic values came from AST2600 data sheet
 * The register values are defined under section "FSI controller"
 * as initial values.
 */
static const uint32_t aspeed_apb2opb_reset[ASPEED_APB2OPB_NR_REGS] = {
     [APB2OPB_VERSION]                = 0x000000a1,
     [APB2OPB_OPB0_WRITE_WORD_ENDIAN] = 0x0044eee4,
     [APB2OPB_OPB0_WRITE_BYTE_ENDIAN] = 0x0055aaff,
     [APB2OPB_OPB1_WRITE_WORD_ENDIAN] = 0x00117717,
     [APB2OPB_OPB1_WRITE_BYTE_ENDIAN] = 0xffaa5500,
     [APB2OPB_OPB0_READ_BYTE_ENDIAN]  = 0x0044eee4,
     [APB2OPB_OPB1_READ_BYTE_ENDIAN]  = 0x00117717
};

static uint64_t fsi_aspeed_apb2opb_read(void *opaque, hwaddr addr,
                                        unsigned size)
{
    AspeedAPB2OPBState *s = ASPEED_APB2OPB(opaque);

    trace_fsi_aspeed_apb2opb_read(addr, size);

    if (addr + size > sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out of bounds read: 0x%"HWADDR_PRIx" for %u\n",
                      __func__, addr, size);
        return 0;
    }

    return s->regs[TO_REG(addr)];
}

static void fsi_aspeed_apb2opb_write(void *opaque, hwaddr addr, uint64_t data,
                                     unsigned size)
{
    AspeedAPB2OPBState *s = ASPEED_APB2OPB(opaque);

    trace_fsi_aspeed_apb2opb_write(addr, size, data);

    if (addr + size > sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out of bounds write: %"HWADDR_PRIx" for %u\n",
                      __func__, addr, size);
        return;
    }

    switch (TO_REG(addr)) {
    case APB2OPB_CONTROL:
        fsi_opb_fsi_master_address(&s->opb[0], data & APB2OPB_CONTROL_OFF);
        break;
    case APB2OPB_OPB2FSI:
        fsi_opb_opb2fsi_address(&s->opb[0], data & APB2OPB_OPB2FSI_OFF);
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
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: Invalid operation: 0x%"HWADDR_PRIx" for %u\n",
                          __func__, addr, size);
            return;
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
                s->regs[index] = fsi_opb_read8(&s->opb[opb], op_addr);
                break;
            case 2:
                s->regs[index] = fsi_opb_read16(&s->opb[opb], op_addr);
                break;
            case 4:
                s->regs[index] = fsi_opb_read32(&s->opb[opb], op_addr);
                break;
            default:
                qemu_log_mask(LOG_GUEST_ERROR,
                              "%s: Size not supported: %u\n",
                              __func__, size);
                return;
            }
        } else {
            /* FIXME: Endian swizzling */
            switch (op_size) {
            case 1:
                fsi_opb_write8(&s->opb[opb], op_addr, op_data);
                break;
            case 2:
                fsi_opb_write16(&s->opb[opb], op_addr, op_data);
                break;
            case 4:
                fsi_opb_write32(&s->opb[opb], op_addr, op_data);
                break;
            default:
                qemu_log_mask(LOG_GUEST_ERROR,
                              "%s: Size not supported: %u\n",
                              __func__, op_size);
                return;
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
    .read = fsi_aspeed_apb2opb_read,
    .write = fsi_aspeed_apb2opb_write,
    .valid.max_access_size = 4,
    .valid.min_access_size = 4,
    .impl.max_access_size = 4,
    .impl.min_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void fsi_aspeed_apb2opb_realize(DeviceState *dev, Error **errp)
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

static void fsi_aspeed_apb2opb_reset(DeviceState *dev)
{
    AspeedAPB2OPBState *s = ASPEED_APB2OPB(dev);

    memcpy(s->regs, aspeed_apb2opb_reset, ASPEED_APB2OPB_NR_REGS);
}

static void fsi_aspeed_apb2opb_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "ASPEED APB2OPB Bridge";
    dc->realize = fsi_aspeed_apb2opb_realize;
    dc->reset = fsi_aspeed_apb2opb_reset;
}

static const TypeInfo aspeed_apb2opb_info = {
    .name = TYPE_ASPEED_APB2OPB,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AspeedAPB2OPBState),
    .class_init = fsi_aspeed_apb2opb_class_init,
};

static void aspeed_apb2opb_register_types(void)
{
    type_register_static(&aspeed_apb2opb_info);
}

type_init(aspeed_apb2opb_register_types);
