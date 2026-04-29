/*
 * PIC32MK SPI × 6 (SPI1–SPI6)
 * Datasheet: DS60001519E, §23
 *
 * Each SPI instance is a SysBusDevice providing a stub register file.
 * Full transfer emulation (loopback, slave select, IRQs) is Phase 2B.
 * Firmware doing register-level init (SPIxCON, SPIxBRG) will succeed;
 * actual data transfers log LOG_UNIMP.
 *
 * Copyright (c) 2026 QEMU contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "chardev/char-fe.h"
#include "hw/mips/pic32mk.h"

#define TYPE_PIC32MK_SPI    "pic32mk-spi"
OBJECT_DECLARE_SIMPLE_TYPE(PIC32MKSpiState, PIC32MK_SPI)

struct PIC32MKSpiState {
    SysBusDevice parent_obj;
    MemoryRegion mr;

    uint32_t con;   /* SPIxCON */
    uint32_t stat;  /* SPIxSTAT */
    uint32_t buf;   /* SPIxBUF — TX write / RX read */
    uint32_t brg;   /* SPIxBRG */
    uint32_t con2;  /* SPIxCON2 */

    uint8_t index;  /* SPI instance index (1..6) */

    uint8_t tx_fifo[8];
    uint8_t rx_fifo[8];
    uint8_t tx_head;
    uint8_t tx_tail;
    uint8_t tx_count;
    uint8_t rx_head;
    uint8_t rx_tail;
    uint8_t rx_count;

    uint8_t frame_buf[260];
    uint16_t frame_pos;
    uint16_t frame_len;

    bool cs_active;

    CharFrontend chr;

    qemu_irq irq_rx;   /* RX interrupt */
    qemu_irq irq_tx;   /* TX interrupt */
    qemu_irq irq_err;  /* Fault/overflow */
};

/* CON bits (subset used by emulation + Harmony plib) */
#define SPI_CON_ON             (1u << 15)
#define SPI_CON_MSTEN          (1u << 5)
#define SPI_CON_MODE16         (1u << 10)
#define SPI_CON_MODE32         (1u << 11)
#define SPI_CON_ENHBUF         (1u << 16)
#define SPI_CON_SRXISEL_MASK   (3u << 0)
#define SPI_CON_STXISEL_MASK   (3u << 2)
#define SPI_CON_SRXISEL_SHIFT  0
#define SPI_CON_STXISEL_SHIFT  2

/* STAT bits (subset used by emulation + Harmony plib) */
#define SPI_STAT_SPIRBF  (1u << 0)   /* RX buffer full */
#define SPI_STAT_SPITBF  (1u << 1)   /* TX buffer full */
#define SPI_STAT_SPITBE  (1u << 3)   /* TX buffer empty */
#define SPI_STAT_SPIRBE  (1u << 5)   /* RX buffer empty */
#define SPI_STAT_SPIROV  (1u << 6)   /* RX overflow */
#define SPI_STAT_SRMT    (1u << 11)  /* Shift register empty */

/* Chardev frame flags */
#define SPI_FRAME_CS_ASSERT    (1u << 0)
#define SPI_FRAME_CS_DEASSERT  (1u << 1)

/* PIC32MK: base+0=REG, +4=CLR, +8=SET, +0xC=INV */
static void apply_sci(uint32_t *reg, uint32_t val, int sub)
{
    switch (sub) {
    case 0:
        *reg  = val;
        break;
    case 4:
        *reg &= ~val;
        break;
    case 8:
        *reg |= val;
        break;
    case 12:
        *reg ^= val;
        break;
    }
}

static int spi_fifo_depth(PIC32MKSpiState *s)
{
    return (s->con & SPI_CON_ENHBUF) ? 8 : 1;
}

static void spi_update_stat(PIC32MKSpiState *s)
{
    const int depth = spi_fifo_depth(s);

    if (s->rx_count > 0) {
        s->stat |= SPI_STAT_SPIRBF;
        s->stat &= ~SPI_STAT_SPIRBE;
    } else {
        s->stat &= ~SPI_STAT_SPIRBF;
        s->stat |= SPI_STAT_SPIRBE;
    }

    if (s->tx_count == 0) {
        s->stat |= SPI_STAT_SPITBE;
        s->stat &= ~SPI_STAT_SPITBF;
    } else if (s->tx_count >= depth) {
        s->stat |= SPI_STAT_SPITBF;
        s->stat &= ~SPI_STAT_SPITBE;
    } else {
        s->stat &= ~SPI_STAT_SPITBF;
        s->stat &= ~SPI_STAT_SPITBE;
    }

    if (s->tx_count == 0) {
        s->stat |= SPI_STAT_SRMT;
    } else {
        s->stat &= ~SPI_STAT_SRMT;
    }
}

static void spi_update_irq(PIC32MKSpiState *s)
{
    if (!(s->con & SPI_CON_ON)) {
        qemu_set_irq(s->irq_rx, 0);
        qemu_set_irq(s->irq_tx, 0);
        qemu_set_irq(s->irq_err, 0);
        return;
    }

    /* SRXISEL: 0=empty, 1=not-empty, 2=half-full, 3=full */
    int srxisel = (s->con & SPI_CON_SRXISEL_MASK) >> SPI_CON_SRXISEL_SHIFT;
    const int depth = spi_fifo_depth(s);
    bool rx_pending;
    switch (srxisel) {
    case 0:
        rx_pending = (s->rx_count == 0);
        break;
    case 1:
        rx_pending = (s->rx_count > 0);
        break;
    case 2:
        rx_pending = (s->rx_count >= depth / 2);
        break;
    case 3:
        rx_pending = (s->rx_count >= depth);
        break;
    default:
        rx_pending = false;
        break;
    }

    /* STXISEL: 0=empty, 1=empty, 2=half-empty, 3=not-full */
    int stxisel = (s->con & SPI_CON_STXISEL_MASK) >> SPI_CON_STXISEL_SHIFT;
    bool tx_pending;
    switch (stxisel) {
    case 0:
        tx_pending = (s->tx_count == 0);
        break;
    case 1:
        tx_pending = (s->tx_count == 0);
        break;
    case 2:
        tx_pending = (s->tx_count <= depth / 2);
        break;
    case 3:
        tx_pending = (s->tx_count < depth);
        break;
    default:
        tx_pending = false;
        break;
    }

    bool err_pending = (s->stat & SPI_STAT_SPIROV) != 0;

    qemu_set_irq(s->irq_rx, rx_pending ? 1 : 0);
    qemu_set_irq(s->irq_tx, tx_pending ? 1 : 0);
    qemu_set_irq(s->irq_err, err_pending ? 1 : 0);
}

static void spi_rx_push(PIC32MKSpiState *s, uint8_t val)
{
    const int depth = spi_fifo_depth(s);
    if (s->rx_count >= depth) {
        s->stat |= SPI_STAT_SPIROV;
        spi_update_irq(s);
        return;
    }

    s->rx_fifo[s->rx_head] = val;
    s->rx_head = (uint8_t)((s->rx_head + 1) % depth);
    s->rx_count++;
}

static bool spi_rx_pop(PIC32MKSpiState *s, uint8_t *val)
{
    const int depth = spi_fifo_depth(s);
    if (s->rx_count == 0) {
        *val = 0;
        return false;
    }

    *val = s->rx_fifo[s->rx_tail];
    s->rx_tail = (uint8_t)((s->rx_tail + 1) % depth);
    s->rx_count--;
    return true;
}

static void spi_tx_push(PIC32MKSpiState *s, uint8_t val)
{
    const int depth = spi_fifo_depth(s);
    if (s->tx_count >= depth) {
        return;
    }

    s->tx_fifo[s->tx_head] = val;
    s->tx_head = (uint8_t)((s->tx_head + 1) % depth);
    s->tx_count++;
}

static bool spi_tx_pop(PIC32MKSpiState *s, uint8_t *val)
{
    const int depth = spi_fifo_depth(s);
    if (s->tx_count == 0) {
        *val = 0xFFu;
        return false;
    }

    *val = s->tx_fifo[s->tx_tail];
    s->tx_tail = (uint8_t)((s->tx_tail + 1) % depth);
    s->tx_count--;
    return true;
}

static void spi_send_frame(PIC32MKSpiState *s, uint8_t flags,
                           const uint8_t *payload, uint8_t len)
{
    uint8_t hdr[2];

    if (!qemu_chr_fe_backend_connected(&s->chr)) {
        return;
    }

    hdr[0] = flags;
    hdr[1] = len;
    qemu_chr_fe_write_all(&s->chr, hdr, 2);
    if (len) {
        qemu_chr_fe_write_all(&s->chr, payload, len);
    }
}

static void spi_handle_frame(PIC32MKSpiState *s, uint8_t flags,
                             const uint8_t *payload, uint8_t len)
{
    uint8_t response[255];
    uint8_t i;

    if (flags & SPI_FRAME_CS_ASSERT) {
        s->cs_active = true;
    }

    for (i = 0; i < len; i++) {
        spi_rx_push(s, payload[i]);
        (void)spi_tx_pop(s, &response[i]);
    }

    if (len) {
        spi_send_frame(s, 0, response, len);
    }

    if (flags & SPI_FRAME_CS_DEASSERT) {
        s->cs_active = false;
    }

    spi_update_stat(s);
    spi_update_irq(s);
}

static int spi_chr_can_receive(void *opaque)
{
    PIC32MKSpiState *s = opaque;
    return qemu_chr_fe_backend_connected(&s->chr) ? 1 : 0;
}

static void spi_chr_receive(void *opaque, const uint8_t *buf, int size)
{
    PIC32MKSpiState *s = opaque;

    for (int i = 0; i < size; i++) {
        s->frame_buf[s->frame_pos++] = buf[i];
        if (s->frame_pos == 2) {
            s->frame_len = (uint16_t)s->frame_buf[1];
        }
        if (s->frame_pos >= 2 && s->frame_pos == (uint16_t)(2 + s->frame_len)) {
            uint8_t flags = s->frame_buf[0];
            uint8_t len = (uint8_t)s->frame_len;
            spi_handle_frame(s, flags, &s->frame_buf[2], len);
            s->frame_pos = 0;
            s->frame_len = 0;
        }
    }
}

static void spi_chr_event(void *opaque, QEMUChrEvent event)
{
    PIC32MKSpiState *s = opaque;

    if (event == CHR_EVENT_CLOSED) {
        s->frame_pos = 0;
        s->frame_len = 0;
    }
}

static uint32_t *spi_find_reg(PIC32MKSpiState *s, hwaddr base)
{
    switch (base) {
    case PIC32MK_SPIxCON:
        return &s->con;
    case PIC32MK_SPIxSTAT:
        return &s->stat;
    case PIC32MK_SPIxBRG:
        return &s->brg;
    case PIC32MK_SPIxCON2:
        return &s->con2;
    default:
        return NULL;
    }
}

static uint64_t spi_read(void *opaque, hwaddr addr, unsigned size)
{
    PIC32MKSpiState *s = opaque;
    hwaddr base = addr & ~(hwaddr)0xF;

    /* BUF read: pop from RX FIFO */
    if (base == PIC32MK_SPIxBUF) {
        uint32_t val = 0;
        uint8_t b0 = 0;
        uint8_t b1 = 0;
        uint8_t b2 = 0;
        uint8_t b3 = 0;

        if (s->con & SPI_CON_MODE32) {
            spi_rx_pop(s, &b0);
            spi_rx_pop(s, &b1);
            spi_rx_pop(s, &b2);
            spi_rx_pop(s, &b3);
            val = (uint32_t)b0 | ((uint32_t)b1 << 8) |
                  ((uint32_t)b2 << 16) | ((uint32_t)b3 << 24);
        } else if (s->con & SPI_CON_MODE16) {
            spi_rx_pop(s, &b0);
            spi_rx_pop(s, &b1);
            val = (uint32_t)b0 | ((uint32_t)b1 << 8);
        } else {
            spi_rx_pop(s, &b0);
            val = b0;
        }

        spi_update_stat(s);
        spi_update_irq(s);
        return val;
    }

    uint32_t *reg = spi_find_reg(s, base);
    if (reg) {
        if (base == PIC32MK_SPIxSTAT) {
            spi_update_stat(s);
            spi_update_irq(s);
        }
        return *reg;
    }

    qemu_log_mask(LOG_UNIMP,
                  "pic32mk_spi: unimplemented read @ 0x%04" HWADDR_PRIx "\n",
                  addr);
    return 0;
}

static void spi_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PIC32MKSpiState *s = opaque;
    int sub     = (int)(addr & 0xF);
    hwaddr base = addr & ~(hwaddr)0xF;

    /* BUF write: transmit */
    if (base == PIC32MK_SPIxBUF) {
        uint8_t bytes[4];
        int count = 1;

        bytes[0] = (uint8_t)val;
        bytes[1] = (uint8_t)(val >> 8);
        bytes[2] = (uint8_t)(val >> 16);
        bytes[3] = (uint8_t)(val >> 24);

        if (s->con & SPI_CON_MODE32) {
            count = 4;
        } else if (s->con & SPI_CON_MODE16) {
            count = 2;
        }

        for (int i = 0; i < count; i++) {
            spi_tx_push(s, bytes[i]);

            if (s->con & SPI_CON_MSTEN) {
                if (qemu_chr_fe_backend_connected(&s->chr)) {
                    uint8_t flags = SPI_FRAME_CS_ASSERT | SPI_FRAME_CS_DEASSERT;
                    spi_send_frame(s, flags, &bytes[i], 1);
                } else {
                    spi_rx_push(s, bytes[i]);
                }
                spi_tx_pop(s, &bytes[i]);
            }
        }

        spi_update_stat(s);
        spi_update_irq(s);
        return;
    }

    uint32_t *reg = spi_find_reg(s, base);
    if (reg) {
        apply_sci(reg, (uint32_t)val, sub);
        if (base == PIC32MK_SPIxCON && !(s->con & SPI_CON_ON)) {
            s->tx_head = s->tx_tail = s->tx_count = 0;
            s->rx_head = s->rx_tail = s->rx_count = 0;
            s->stat = SPI_STAT_SPITBE | SPI_STAT_SPIRBE | SPI_STAT_SRMT;
        }
        spi_update_stat(s);
        spi_update_irq(s);
        return;
    }

    qemu_log_mask(LOG_UNIMP,
                  "pic32mk_spi: unimplemented write @ 0x%04"
                  HWADDR_PRIx " = 0x%08" PRIx64 "\n",
                  addr, val);
}

static const MemoryRegionOps spi_ops = {
    .read       = spi_read,
    .write      = spi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
};

static void pic32mk_spi_reset(DeviceState *dev)
{
    PIC32MKSpiState *s = PIC32MK_SPI(dev);
    s->con  = 0;
    s->stat = SPI_STAT_SPITBE | SPI_STAT_SPIRBE | SPI_STAT_SRMT;
    s->buf  = 0;
    s->brg  = 0;
    s->con2 = 0;
    s->tx_head = s->tx_tail = s->tx_count = 0;
    s->rx_head = s->rx_tail = s->rx_count = 0;
    s->frame_pos = 0;
    s->frame_len = 0;
    s->cs_active = false;
    qemu_irq_lower(s->irq_rx);
    qemu_irq_lower(s->irq_tx);
    qemu_irq_lower(s->irq_err);
}

static void pic32mk_spi_init(Object *obj)
{
    PIC32MKSpiState *s = PIC32MK_SPI(obj);

    memory_region_init_io(&s->mr, obj, &spi_ops, s,
                          TYPE_PIC32MK_SPI, PIC32MK_SPI_BLOCK_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mr);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq_rx);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq_tx);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq_err);
}

static void pic32mk_spi_realize(DeviceState *dev, Error **errp)
{
    PIC32MKSpiState *s = PIC32MK_SPI(dev);

    qemu_chr_fe_set_handlers(&s->chr,
                             spi_chr_can_receive,
                             spi_chr_receive,
                             spi_chr_event,
                             NULL,
                             s,
                             NULL,
                             true);
}

static const Property pic32mk_spi_props[] = {
    DEFINE_PROP_UINT8("spi-index", PIC32MKSpiState, index, 0),
    DEFINE_PROP_CHR("chardev", PIC32MKSpiState, chr),
};

static void pic32mk_spi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    device_class_set_legacy_reset(dc, pic32mk_spi_reset);
    device_class_set_props(dc, pic32mk_spi_props);
    dc->realize = pic32mk_spi_realize;
}

static const TypeInfo pic32mk_spi_info = {
    .name          = TYPE_PIC32MK_SPI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PIC32MKSpiState),
    .instance_init = pic32mk_spi_init,
    .class_init    = pic32mk_spi_class_init,
};

static void pic32mk_spi_register_types(void)
{
    type_register_static(&pic32mk_spi_info);
}

type_init(pic32mk_spi_register_types)
