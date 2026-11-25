/*
 * Spacemit K3 SPI Controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright 2026 Anirudh Srinivasan
 */

#include "qemu/osdep.h"

#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/registerfields.h"
#include "hw/ssi/spacemit_k3_spi.h"
#include "qemu/log.h"
#include "qemu/units.h"
#include "trace.h"

REG32(MCR, 0x0)
    FIELD(MCR, CLR_TXF_MASK, 11, 1)
    FIELD(MCR, CLR_RXF_MASK, 10, 1)

REG32(IPCR, 0x8)

REG32(SFAR, 0x100)

REG32(TBDR, 0x154)

REG32(FR, 0x160)
    FIELD(FR, TFF_MASK, 0, 1)
    FIELD(FR, RBDF, 16, 1)

REG32(RXBUF0, 0x200)
REG32(RXBUF31, 0x27c)

REG32(LUT1REG0, 0x320)
REG32(LUT1REG1, 0x324)
REG32(LUT1REG2, 0x328)
REG32(LUT1REG3, 0x32C)

static uint64_t spacemit_k3_spi_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    SpacemitK3SpiState *s = opaque;
    uint32_t ret = 0;

    if (addr >= A_RXBUF0 && addr <= A_RXBUF31) {
        for (int idx = 3; idx >= 0; idx--) {
            ret |= (s->rxbuf[idx + addr - A_RXBUF0] << (8UL * idx));
        }
    } else {
        switch (addr) {
        case A_MCR:
            ret = s->mcr;
            break;
        case A_FR:
            ret = s->fr;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: read from invalid offset 0x%" HWADDR_PRIx "\n",
                          DEVICE(s)->canonical_path, addr);
            ret = 0;
        }
    }
    trace_spacemit_k3_spi_read(addr, size, ret);
    return ret;
}

static void spacemit_k3_spi_write(void *opaque, hwaddr addr, uint64_t val,
                             unsigned int size)
{
    SpacemitK3SpiState *s = opaque;

    trace_spacemit_k3_spi_write(addr, size, val);

    switch (addr) {
    case A_MCR:
        s->mcr = val;
        if (FIELD_EX32(s->mcr, MCR, CLR_RXF_MASK)) {
            memset(s->rxbuf, 0, SPACEMIT_K3_SPI_RXFIFO_SIZE);
            s->rxlevel = 0;
        }
        if (FIELD_EX32(s->mcr, MCR, CLR_TXF_MASK)) {
            memset(s->txbuf, 0, SPACEMIT_K3_SPI_TXFIFO_SIZE);
            s->txlevel = 0;
        }
        break;
    case A_IPCR:
        qemu_set_irq(s->cs_line, 0);
        for (int lut_reg = 0; lut_reg < 4; lut_reg++) {
            for (int slot = 0; slot < 2; slot++) {
                uint32_t lut_entry = (s->lut1[lut_reg] >> (16 * slot)) & 0xffff;
                uint32_t inst = lut_entry >> 10;

                switch (inst) {
                case 1: /* CMD */
                    ssi_transfer(s->spi, lut_entry & 0xff);
                    break;
                case 2: /* ADDR */
                    uint32_t read_addr = s->sfar - s->ahb_base;
                    ssi_transfer(s->spi, (read_addr >> 16) & 0xff);
                    ssi_transfer(s->spi, (read_addr >> 8) & 0xff);
                    ssi_transfer(s->spi, read_addr & 0xff);
                    break;
                case 3: /* DUMMY */
                  for (uint32_t num_dummy = 0; num_dummy < (lut_reg & 0xff);
                       num_dummy += 8)
                    ssi_transfer(s->spi, 0);
                  break;
                case 7: /* READ */
                  uint32_t len = val & 0xffffff;
                  if (len > sizeof(s->rxbuf)) {
                    qemu_log_mask(LOG_GUEST_ERROR,
                                  "%s: read length %u exceeds rxbuf size %zu\n",
                                  __func__, len, sizeof(s->rxbuf));
                    len = sizeof(s->rxbuf);
                  }
                  for (uint32_t num_bytes = 0; num_bytes < len; num_bytes++) {
                    s->rxbuf[num_bytes] = ssi_transfer(s->spi, 0);
                  }
                  s->fr = FIELD_DP32(s->fr, FR, RBDF, 1);
                  s->rxlevel = val & 0xffffff;
                  break;
                case 8: /* WRITE */
                  len = val & 0xffffff;
                  if (len > s->txlevel) {
                    qemu_log_mask(LOG_GUEST_ERROR,
                                  "%s: write length %u exceeds txlevel %u\n",
                                  __func__, len, s->txlevel);
                    len = MIN(len, s->txlevel);
                  }
                  for (uint32_t num_bytes = 0; num_bytes < len; num_bytes++) {
                    ssi_transfer(s->spi, s->txbuf[num_bytes]);
                  }
                  break;
                }
            }
        }
        s->fr = FIELD_DP32(s->fr, FR, TFF_MASK, 1);
        qemu_set_irq(s->cs_line, 1);
        break;
    case A_SFAR:
        s->sfar = val;
        break;
    case A_TBDR:
        for (int idx = 0; idx < 4; idx++) {
            if (s->txlevel >= SPACEMIT_K3_SPI_TXFIFO_SIZE) {
                qemu_log_mask(LOG_GUEST_ERROR,
                        "%s: TX FIFO full, dropping byte\n", __func__);
                break;
            }
            s->txbuf[s->txlevel++] = val & 0xff;
            val = val >> 8;
        }
        break;
    case A_FR:
        s->fr &= ~val;
        break;
    case A_LUT1REG0:
    case A_LUT1REG1:
    case A_LUT1REG2:
    case A_LUT1REG3:
        s->lut1[(addr - A_LUT1REG0) >> 2] = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to invalid offset 0x%" HWADDR_PRIx "\n",
                      DEVICE(s)->canonical_path, addr);
        break;
    }
}

static const MemoryRegionOps spacemit_k3_spi_ops = {
    .read = spacemit_k3_spi_read,
    .write = spacemit_k3_spi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static uint64_t spacemit_k3_spi_ahb_read(void *opaque, hwaddr addr,
                                         unsigned int size)
{
    SpacemitK3SpiState *s = opaque;
    uint64_t ret = 0;

    switch (addr) {
    default:
        qemu_set_irq(s->cs_line, 0);
        ssi_transfer(s->spi, 0x03);
        ssi_transfer(s->spi, (addr >> 16) & 0xff);
        ssi_transfer(s->spi, (addr >> 8) & 0xff);
        ssi_transfer(s->spi, addr & 0xff);
        for (uint32_t idx = 0; idx < size; idx += 1) {
            ret |= (uint64_t)ssi_transfer(s->spi, 0) << (8 * idx);
        }
        qemu_set_irq(s->cs_line, 1);
    }
    trace_spacemit_k3_spi_ahb_read(addr, size, ret);
    return ret;
}

static const MemoryRegionOps spacemit_k3_spi_ahb_ops = {
    .read = spacemit_k3_spi_ahb_read,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = false,
    },
};

static void spacemit_k3_spi_enter_reset(Object *obj, ResetType type)
{
    SpacemitK3SpiState *s = SPACEMIT_K3_SPI(obj);

    s->mcr = 0;
    s->ipcr = 0;
    s->sfar = 0;
    s->fr = 0;
    memset(s->lut1, 0, 4 * sizeof(uint32_t));

    memset(s->rxbuf, 0, SPACEMIT_K3_SPI_RXFIFO_SIZE);
    s->rxlevel = 0;

    memset(s->txbuf, 0, SPACEMIT_K3_SPI_TXFIFO_SIZE);
    s->txlevel = 0;
}

static void spacemit_k3_spi_realize(DeviceState *dev, Error **errp)
{
    SpacemitK3SpiState *s = SPACEMIT_K3_SPI(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    s->spi = ssi_create_bus(dev, "ssi");
    memory_region_init_io(&s->mmio, OBJECT(s), &spacemit_k3_spi_ops, s,
                          "spacemit-k3-spi", 4 * KiB);
    sysbus_init_mmio(sbd, &s->mmio);
    memory_region_init_io(&s->ahb, OBJECT(s), &spacemit_k3_spi_ahb_ops, s,
                          "spacemit-k3-spi-ahb", 0x8000000);
    sysbus_init_mmio(sbd, &s->ahb);
    sysbus_init_irq(sbd, &s->irq);
    qdev_init_gpio_out_named(dev, &s->cs_line, "cs", 1);
}

static const Property spacemit_k3_spi_properties[] = {
    DEFINE_PROP_UINT32("ahb-base", SpacemitK3SpiState, ahb_base, 0xb8000000),
};

static void spacemit_k3_spi_class_init(ObjectClass *klass, const void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Spacemit K3 SPI controller";
    dc->realize = spacemit_k3_spi_realize;
    device_class_set_props(dc, spacemit_k3_spi_properties);
    rc->phases.enter = spacemit_k3_spi_enter_reset;
}

static const TypeInfo spacemit_k3_spi_types[] = {
    {
        .name = TYPE_SPACEMIT_K3_SPI,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(SpacemitK3SpiState),
        .class_init = spacemit_k3_spi_class_init,
    },
};
DEFINE_TYPES(spacemit_k3_spi_types)
