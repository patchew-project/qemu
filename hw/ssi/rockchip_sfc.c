/*
 * Rockchip Serial Flash Controller
 *
 * Copyright (c) 2026 Process Mission
 *
 * Authors:
 *    Chao Liu <chao.liu@processmission.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/register.h"
#include "hw/core/sysbus.h"
#include "hw/ssi/ssi.h"
#include "qom/object.h"

#define TYPE_ROCKCHIP_SFC "rockchip-sfc"
OBJECT_DECLARE_SIMPLE_TYPE(RockchipSFCState, ROCKCHIP_SFC)

#define ROCKCHIP_SFC_MMIO_SIZE 0x4000
#define ROCKCHIP_SFC_NUM_CS 2
#define ROCKCHIP_SFC_NUM_REGS (0x10c / sizeof(uint32_t))

struct RockchipSFCState {
    SysBusDevice parent_obj;

    RegisterInfo regs_info[ROCKCHIP_SFC_NUM_REGS];
    uint32_t regs[ROCKCHIP_SFC_NUM_REGS];
    MemoryRegion *dma_memory;
    AddressSpace dma_as;
    SSIBus *spi;
    qemu_irq irq;
    qemu_irq cs_lines[ROCKCHIP_SFC_NUM_CS];

    uint32_t remaining;
    uint8_t address_bytes;
    uint8_t dummy_bytes;
    uint8_t cs;
    bool write;
    bool pending;
    bool started;
    bool resetting;
};

REG32(CTRL, 0x000)
REG32(IMR, 0x004)
REG32(ICLR, 0x008)
REG32(FTLR, 0x00c)
REG32(RCVR, 0x010)
REG32(AX, 0x014)
REG32(ABIT, 0x018)
REG32(ISR, 0x01c)
REG32(FSR, 0x020)
REG32(SR, 0x024)
REG32(RISR, 0x028)
REG32(VER, 0x02c)
REG32(DLL_CTRL0, 0x03c)
REG32(DMA_TRIGGER, 0x080)
REG32(DMA_ADDR, 0x084)
REG32(LEN_CTRL, 0x088)
REG32(LEN_EXT, 0x08c)
REG32(CMD, 0x100)
REG32(ADDR, 0x104)
REG32(DATA, 0x108)

#define SFC_INT_FINISH   BIT(4)
#define SFC_INT_BUS_ERR  BIT(5)
#define SFC_INT_NSPI_ERR BIT(6)
#define SFC_INT_DMA      BIT(7)
#define SFC_MAX_LENGTH  (32 * MiB)
#define SFC_LEN_CTRL_TRB_SEL BIT(0)

static void rockchip_sfc_update_irq(RockchipSFCState *s)
{
    qemu_set_irq(s->irq, (s->regs[R_RISR] & ~s->regs[R_IMR]) != 0);
}

static void rockchip_sfc_end(RockchipSFCState *s)
{
    if (s->started) {
        qemu_irq_raise(s->cs_lines[s->cs]);
    }
    s->pending = false;
    s->started = false;
    s->remaining = 0;
}

static void rockchip_sfc_complete(RockchipSFCState *s, uint32_t status)
{
    rockchip_sfc_end(s);
    s->regs[R_RISR] |= status;
    rockchip_sfc_update_irq(s);
}

static void rockchip_sfc_begin(RockchipSFCState *s)
{
    if (!s->pending || s->started) {
        return;
    }

    qemu_irq_lower(s->cs_lines[s->cs]);
    s->started = true;
    ssi_transfer(s->spi, s->regs[R_CMD] & 0xff);
    for (int i = s->address_bytes - 1; i >= 0; i--) {
        ssi_transfer(s->spi, (s->regs[R_ADDR] >> (8 * i)) & 0xff);
    }
    for (unsigned int i = 0; i < s->dummy_bytes; i++) {
        ssi_transfer(s->spi, 0);
    }
    if (!s->remaining) {
        rockchip_sfc_complete(s, SFC_INT_FINISH);
    }
}

static void rockchip_sfc_command(RockchipSFCState *s, uint32_t value)
{
    unsigned int address_mode = (value >> 14) & 3;
    unsigned int dummy_cycles = (value >> 8) & 0xf;
    unsigned int address_width = 1 << ((s->regs[R_CTRL] >> 10) & 3);
    uint32_t len;

    if (s->regs[R_LEN_CTRL] & SFC_LEN_CTRL_TRB_SEL) {
        len = s->regs[R_LEN_EXT];
    } else {
        /* Normal mode: transfer bytes are in CMD[29:16]. */
        len = (value >> 16) & 0x3fff;
    }

    rockchip_sfc_end(s);
    s->cs = value >> 30;
    if (s->cs >= ROCKCHIP_SFC_NUM_CS || len > SFC_MAX_LENGTH) {
        rockchip_sfc_complete(s, SFC_INT_NSPI_ERR);
        return;
    }

    switch (address_mode) {
    case 1:
        s->address_bytes = 3;
        break;
    case 2:
        s->address_bytes = 4;
        break;
    case 3:
        if ((s->regs[R_ABIT] & 7) != 7) {
            rockchip_sfc_complete(s, SFC_INT_NSPI_ERR);
            return;
        }
        s->address_bytes = ((s->regs[R_ABIT] & 31) + 1) / 8;
        break;
    default:
        s->address_bytes = 0;
        break;
    }
    s->dummy_bytes = DIV_ROUND_UP(dummy_cycles * address_width, 8);
    s->write = (value >> 12) & 1;
    s->remaining = len;
    s->pending = true;
    if (!s->address_bytes) {
        rockchip_sfc_begin(s);
    }
}

static uint32_t rockchip_sfc_read_data(RockchipSFCState *s)
{
    uint32_t value = 0;
    unsigned int count;

    if (!s->started || s->write) {
        return 0;
    }
    count = MIN(4, s->remaining);
    for (unsigned int i = 0; i < count; i++) {
        value |= (uint32_t)ssi_transfer(s->spi, 0) << (i * 8);
    }
    s->remaining -= count;
    if (!s->remaining) {
        rockchip_sfc_complete(s, SFC_INT_FINISH);
    }
    return value;
}

static void rockchip_sfc_write_data(RockchipSFCState *s, uint32_t value)
{
    unsigned int count;

    if (!s->started || !s->write) {
        return;
    }
    count = MIN(4, s->remaining);
    for (unsigned int i = 0; i < count; i++) {
        ssi_transfer(s->spi, (value >> (i * 8)) & 0xff);
    }
    s->remaining -= count;
    if (!s->remaining) {
        rockchip_sfc_complete(s, SFC_INT_FINISH);
    }
}

static void rockchip_sfc_dma(RockchipSFCState *s)
{
    uint8_t buffer[256];
    hwaddr addr = s->regs[R_DMA_ADDR];
    MemTxAttrs attrs = { .memory = true };

    if (!s->started) {
        rockchip_sfc_complete(s, SFC_INT_BUS_ERR);
        return;
    }

    while (s->remaining) {
        unsigned int count = MIN(sizeof(buffer), s->remaining);
        MemTxResult result;

        if (s->write) {
            result = address_space_read(&s->dma_as, addr, attrs,
                                        buffer, count);
            if (result == MEMTX_OK) {
                for (unsigned int i = 0; i < count; i++) {
                    ssi_transfer(s->spi, buffer[i]);
                }
            }
        } else {
            for (unsigned int i = 0; i < count; i++) {
                buffer[i] = ssi_transfer(s->spi, 0);
            }
            result = address_space_write(&s->dma_as, addr, attrs,
                                         buffer, count);
        }
        if (result != MEMTX_OK) {
            rockchip_sfc_complete(s, SFC_INT_BUS_ERR);
            return;
        }
        s->remaining -= count;
        addr += count;
    }
    rockchip_sfc_complete(s, SFC_INT_DMA | SFC_INT_FINISH);
}

static void rockchip_sfc_reset(DeviceState *dev)
{
    RockchipSFCState *s = ROCKCHIP_SFC(dev);

    rockchip_sfc_end(s);
    for (unsigned int i = 0; i < ROCKCHIP_SFC_NUM_CS; i++) {
        qemu_irq_raise(s->cs_lines[i]);
    }
    s->resetting = true;
    for (unsigned int i = 0; i < ARRAY_SIZE(s->regs_info); i++) {
        register_reset(&s->regs_info[i]);
    }
    s->resetting = false;
    s->cs = 0;
    s->address_bytes = 0;
    s->dummy_bytes = 0;
    s->write = false;
    rockchip_sfc_update_irq(s);
}

static uint64_t rockchip_sfc_status_read(RegisterInfo *reg, uint64_t value)
{
    RockchipSFCState *s = ROCKCHIP_SFC(reg->opaque);

    switch (reg->access->addr) {
    case A_ISR:
        return s->regs[R_RISR] & ~s->regs[R_IMR];
    case A_FSR:
        if (s->started && !s->write && s->remaining) {
            return (31 << 8) | BIT(1) |
                   (MIN(DIV_ROUND_UP(s->remaining, 4), 31) << 16);
        }
        return (31 << 8) | BIT(1) | BIT(2);
    case A_SR:
        return s->pending ? 1 : 0;
    case A_DATA:
        return rockchip_sfc_read_data(s);
    default:
        g_assert_not_reached();
    }
}

static void rockchip_sfc_register_write(RegisterInfo *reg, uint64_t value)
{
    RockchipSFCState *s = ROCKCHIP_SFC(reg->opaque);

    if (s->resetting) {
        return;
    }

    switch (reg->access->addr) {
    case A_IMR:
        rockchip_sfc_update_irq(s);
        break;
    case A_ICLR:
        s->regs[R_RISR] &= ~value;
        s->regs[R_ICLR] = 0;
        rockchip_sfc_update_irq(s);
        break;
    case A_RCVR:
        if (value & 1) {
            rockchip_sfc_end(s);
            s->regs[R_RISR] = 0;
            rockchip_sfc_update_irq(s);
        }
        s->regs[R_RCVR] = 0;
        break;
    case A_DMA_TRIGGER:
        if (value & 1) {
            rockchip_sfc_dma(s);
        }
        s->regs[R_DMA_TRIGGER] = 0;
        break;
    case A_CMD:
        rockchip_sfc_command(s, value);
        break;
    case A_ADDR:
        rockchip_sfc_begin(s);
        break;
    case A_DATA:
        rockchip_sfc_write_data(s, value);
        break;
    default:
        g_assert_not_reached();
    }
}

static const RegisterAccessInfo rockchip_sfc_regs[] = {
    { .name = "CTRL", .addr = A_CTRL },
    { .name = "IMR", .addr = A_IMR, .reset = UINT32_MAX,
      .post_write = rockchip_sfc_register_write },
    { .name = "ICLR", .addr = A_ICLR,
      .post_write = rockchip_sfc_register_write },
    { .name = "FTLR", .addr = A_FTLR },
    { .name = "RCVR", .addr = A_RCVR,
      .post_write = rockchip_sfc_register_write },
    { .name = "AX", .addr = A_AX },
    { .name = "ABIT", .addr = A_ABIT },
    { .name = "ISR", .addr = A_ISR, .ro = UINT32_MAX,
      .post_read = rockchip_sfc_status_read },
    { .name = "FSR", .addr = A_FSR, .ro = UINT32_MAX,
      .post_read = rockchip_sfc_status_read },
    { .name = "SR", .addr = A_SR, .ro = UINT32_MAX,
      .post_read = rockchip_sfc_status_read },
    { .name = "RISR", .addr = A_RISR, .ro = UINT32_MAX },
    { .name = "VER", .addr = A_VER, .ro = UINT32_MAX, .reset = 4 },
    { .name = "DLL_CTRL0", .addr = A_DLL_CTRL0 },
    { .name = "DMA_TRIGGER", .addr = A_DMA_TRIGGER,
      .post_write = rockchip_sfc_register_write },
    { .name = "DMA_ADDR", .addr = A_DMA_ADDR },
    { .name = "LEN_CTRL", .addr = A_LEN_CTRL },
    { .name = "LEN_EXT", .addr = A_LEN_EXT },
    { .name = "CMD", .addr = A_CMD,
      .post_write = rockchip_sfc_register_write },
    { .name = "ADDR", .addr = A_ADDR,
      .post_write = rockchip_sfc_register_write },
    { .name = "DATA", .addr = A_DATA,
      .post_read = rockchip_sfc_status_read,
      .post_write = rockchip_sfc_register_write },
};

static const MemoryRegionOps rockchip_sfc_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void rockchip_sfc_realize(DeviceState *dev, Error **errp)
{
    RockchipSFCState *s = ROCKCHIP_SFC(dev);

    if (!s->dma_memory) {
        error_setg(errp, "rockchip-sfc: dma-memory link is required");
        return;
    }
    address_space_init(&s->dma_as, s->dma_memory, TYPE_ROCKCHIP_SFC);
}

static void rockchip_sfc_unrealize(DeviceState *dev)
{
    RockchipSFCState *s = ROCKCHIP_SFC(dev);

    address_space_destroy(&s->dma_as);
}

static void rockchip_sfc_init(Object *obj)
{
    RockchipSFCState *s = ROCKCHIP_SFC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RegisterInfoArray *reg_array;

    s->spi = ssi_create_bus(DEVICE(obj), "spi");
    qdev_init_gpio_out_named(DEVICE(obj), s->cs_lines, "cs",
                             ROCKCHIP_SFC_NUM_CS);
    sysbus_init_irq(sbd, &s->irq);
    reg_array = register_init_block32(DEVICE(obj), rockchip_sfc_regs,
                                     ARRAY_SIZE(rockchip_sfc_regs),
                                     s->regs_info, s->regs, &rockchip_sfc_ops,
                                     false, ROCKCHIP_SFC_MMIO_SIZE);
    sysbus_init_mmio(sbd, &reg_array->mem);
}

static const Property rockchip_sfc_properties[] = {
    DEFINE_PROP_LINK("dma-memory", RockchipSFCState, dma_memory,
                     TYPE_MEMORY_REGION, MemoryRegion *),
};

static void rockchip_sfc_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = rockchip_sfc_realize;
    dc->unrealize = rockchip_sfc_unrealize;
    device_class_set_props(dc, rockchip_sfc_properties);
    device_class_set_legacy_reset(dc, rockchip_sfc_reset);
}

static const TypeInfo rockchip_sfc_info = {
    .name = TYPE_ROCKCHIP_SFC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RockchipSFCState),
    .instance_init = rockchip_sfc_init,
    .class_init = rockchip_sfc_class_init,
};

static void rockchip_sfc_register_types(void)
{
    type_register_static(&rockchip_sfc_info);
}

type_init(rockchip_sfc_register_types)
