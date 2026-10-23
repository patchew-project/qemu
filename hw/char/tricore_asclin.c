/*
 * TriCore ASCLIN (Asynchronous/Synchronous Interface) UART controller
 *
 * Copyright (c) 2017 David Brenken <david.brenken@efs-auto.de>
 * Copyright (c) 2024 Siemens AG
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/core/registerfields.h"
#include "chardev/char-fe.h"
#include "chardev/char-serial.h"
#include "migration/vmstate.h"
#include "hw/char/tricore_asclin.h"
#include "hw/core/qdev-properties-system.h"

/*
 * Register offsets (byte address / 4) for TC3x ASCLIN.
 * Each register occupies one 4-byte slot from 0x00 to 0x50.
 */
enum {
    R_CLC = 0,
    R_IOCR,
    R_ID,
    R_TXFIFOCON,
    R_RXFIFOCON,
    R_BITCON,
    R_FRAMECON,
    R_DATCON,
    R_BRG,
    R_BRD,
    R_LINCON,
    R_LINBTIMER,
    R_LINHTIMER,
    R_FLAGS,
    R_FLAGSSET,
    R_FLAGSCLEAR,
    R_FLAGSENABLE,
    R_TXDATA,
    R_RXDATA,
    R_CSR,
    R_RXDATAD,
};

/* FLAGS register bits */
#define ASCLIN_FLAGS_TH     (1u << 0)
#define ASCLIN_FLAGS_TR     (1u << 1)
#define ASCLIN_FLAGS_RH     (1u << 2)
#define ASCLIN_FLAGS_RR     (1u << 3)
#define ASCLIN_FLAGS_FED    (1u << 5)
#define ASCLIN_FLAGS_RED    (1u << 6)
#define ASCLIN_FLAGS_PE     (1u << 16)
#define ASCLIN_FLAGS_TC     (1u << 17)
#define ASCLIN_FLAGS_FE     (1u << 18)
#define ASCLIN_FLAGS_HT     (1u << 19)
#define ASCLIN_FLAGS_RT     (1u << 20)
#define ASCLIN_FLAGS_BD     (1u << 21)
#define ASCLIN_FLAGS_LP     (1u << 22)
#define ASCLIN_FLAGS_LA     (1u << 23)
#define ASCLIN_FLAGS_LC     (1u << 24)
#define ASCLIN_FLAGS_CE     (1u << 25)
#define ASCLIN_FLAGS_RFO    (1u << 26)
#define ASCLIN_FLAGS_RFU    (1u << 27)
#define ASCLIN_FLAGS_RFL    (1u << 28)
#define ASCLIN_FLAGS_TFO    (1u << 30)
#define ASCLIN_FLAGS_TFL    (1u << 31)

/* RXFIFOCON bits */
#define ASCLIN_RXFIFOCON_FLUSH  0x1
#define ASCLIN_RXFIFOCON_ENI    0x2

/* TXFIFOCON.INW: bytes written to the Tx FIFO per bus write */
#define ASCLIN_TXFIFOCON_INW_SHIFT  6
#define ASCLIN_TXFIFOCON_INW_MASK   (0x3u << ASCLIN_TXFIFOCON_INW_SHIFT)

/* Interrupt line grouping masks */
#define ASCLIN_TX_INT_MASK  (ASCLIN_FLAGS_TH | ASCLIN_FLAGS_TR | \
                             ASCLIN_FLAGS_TFL)
#define ASCLIN_RX_INT_MASK  (ASCLIN_FLAGS_RH | ASCLIN_FLAGS_RR | \
                             ASCLIN_FLAGS_RFL)
#define ASCLIN_ERR_INT_MASK (ASCLIN_FLAGS_FED | ASCLIN_FLAGS_RED | \
                             ASCLIN_FLAGS_PE  | ASCLIN_FLAGS_TC  | \
                             ASCLIN_FLAGS_FE  | ASCLIN_FLAGS_HT  | \
                             ASCLIN_FLAGS_RT  | ASCLIN_FLAGS_BD  | \
                             ASCLIN_FLAGS_LP  | ASCLIN_FLAGS_LA  | \
                             ASCLIN_FLAGS_LC  | ASCLIN_FLAGS_CE  | \
                             ASCLIN_FLAGS_RFO | ASCLIN_FLAGS_RFU | \
                             ASCLIN_FLAGS_TFO)

/* FIFO depth and FILL field position in TX/RXFIFOCON */
#define ASCLIN_HW_FIFO_DEPTH    16
#define ASCLIN_FILL_SHIFT       16
#define ASCLIN_FILL_MASK        (0x1fu << ASCLIN_FILL_SHIFT)

static void asclin_rx_buf_reset(TriCoreASCLINState *s)
{
    memset(s->rxbuf, 0, ASCLIN_RX_BUF_SIZE);
    s->rx_rdidx = 0;
    s->rx_wridx = 0;
}

static uint32_t asclin_rx_buf_used(TriCoreASCLINState *s)
{
    return ((s->rx_wridx + ASCLIN_RX_BUF_SIZE) - s->rx_rdidx)
           % ASCLIN_RX_BUF_SIZE;
}

static uint32_t asclin_rx_buf_free(TriCoreASCLINState *s)
{
    return (ASCLIN_RX_BUF_SIZE - 1) - asclin_rx_buf_used(s);
}

/*
 * Pulse the appropriate IRQ line for each flag bit that is both
 * set and enabled.  The TriCore interrupt router is edge-triggered.
 */
static void asclin_pulse_irq(TriCoreASCLINState *s, uint32_t pulse_mask)
{
    uint32_t fired = pulse_mask & s->regs[R_FLAGSENABLE];

    if (fired & ASCLIN_TX_INT_MASK) {
        qemu_irq_pulse(s->irq_tx);
    }
    if (fired & ASCLIN_RX_INT_MASK) {
        qemu_irq_pulse(s->irq_rx);
    }
    if (fired & ASCLIN_ERR_INT_MASK) {
        qemu_irq_pulse(s->irq_err);
    }
}

static unsigned asclin_tx_inlet_bytes(TriCoreASCLINState *s)
{
    static const uint8_t inw_bytes[] = { 0, 1, 2, 4 };

    return inw_bytes[(s->regs[R_TXFIFOCON] & ASCLIN_TXFIFOCON_INW_MASK) >>
                     ASCLIN_TXFIFOCON_INW_SHIFT];
}

/*
 * Watch callback: retries TX when the chardev was previously busy.
 */
static gboolean asclin_tx_watch(void *do_not_use, GIOCondition cond,
                                void *opaque)
{
    TriCoreASCLINState *s = TRICORE_ASCLIN(opaque);
    int ret;

    s->watch_tag = 0;

    ret = qemu_chr_fe_write_all(&s->chr, (uint8_t *)&s->txbuf,
                                asclin_tx_inlet_bytes(s));
    if (ret <= 0) {
        s->watch_tag = qemu_chr_fe_add_watch(&s->chr,
                                             G_IO_OUT | G_IO_HUP,
                                             asclin_tx_watch, s);
        if (!s->watch_tag) {
            goto drained;
        }
        return G_SOURCE_REMOVE;
    }

drained:
    qatomic_or(&s->regs[R_FLAGS], ASCLIN_FLAGS_TFL | ASCLIN_FLAGS_TC);
    asclin_pulse_irq(s, ASCLIN_FLAGS_TFL | ASCLIN_FLAGS_TC);
    return G_SOURCE_REMOVE;
}

/*
 * Transmit TXFIFOCON.INW bytes immediately.  Re-asserts TFL and TC flags
 * so that interrupt-driven drivers can continue filling the FIFO.
 */
static void asclin_txdata_write(TriCoreASCLINState *s, uint32_t value)
{
    unsigned len = asclin_tx_inlet_bytes(s);
    int ret;

    if (!len) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: TXFIFOCON.INW is 0, data dropped\n", __func__);
        return;
    }

    s->txbuf = cpu_to_le32(value);
    ret = qemu_chr_fe_write_all(&s->chr, (uint8_t *)&s->txbuf, len);
    if (ret <= 0) {
        s->watch_tag = qemu_chr_fe_add_watch(&s->chr,
                                             G_IO_OUT | G_IO_HUP,
                                             asclin_tx_watch, s);
        if (!s->watch_tag) {
            goto drained;
        }
        return;
    }

drained:
    qatomic_or(&s->regs[R_FLAGS], ASCLIN_FLAGS_TFL | ASCLIN_FLAGS_TC);
    asclin_pulse_irq(s, ASCLIN_FLAGS_TFL | ASCLIN_FLAGS_TC);
}

static void asclin_txfifocon_write(TriCoreASCLINState *s, uint32_t value)
{
    /* FILL field is read-only hardware status */
    s->regs[R_TXFIFOCON] = value & ~(ASCLIN_FILL_MASK | ASCLIN_RXFIFOCON_FLUSH);
}

static uint32_t asclin_txfifocon_read(TriCoreASCLINState *s)
{
    /* Instant-TX model: FILL always reads as 0 */
    return s->regs[R_TXFIFOCON] & ~ASCLIN_FILL_MASK;
}

static void asclin_rxfifocon_write(TriCoreASCLINState *s, uint32_t value)
{
    s->regs[R_RXFIFOCON] = value & ~(ASCLIN_FILL_MASK | ASCLIN_RXFIFOCON_FLUSH);

    if (value & ASCLIN_RXFIFOCON_FLUSH) {
        asclin_rx_buf_reset(s);
    }
    if (value & ASCLIN_RXFIFOCON_ENI) {
        qemu_chr_fe_accept_input(&s->chr);
    }
}

static uint32_t asclin_rxfifocon_read(TriCoreASCLINState *s)
{
    uint32_t used = asclin_rx_buf_used(s);
    uint32_t fill = MIN(used, ASCLIN_HW_FIFO_DEPTH);

    return (s->regs[R_RXFIFOCON] & ~ASCLIN_FILL_MASK) |
           (fill << ASCLIN_FILL_SHIFT);
}

static uint32_t asclin_rxdata_read(TriCoreASCLINState *s, bool peek)
{
    uint32_t r;

    if (s->rx_rdidx == s->rx_wridx) {
        return 0;
    }

    r = s->rxbuf[s->rx_rdidx];
    if (!peek) {
        s->rx_rdidx = (s->rx_rdidx + 1) % ASCLIN_RX_BUF_SIZE;
    }
    return r;
}

static uint32_t asclin_csr_read(TriCoreASCLINState *s)
{
    uint32_t csr = s->regs[R_CSR];

    /* CLKSEL valid: set CON bit (bit 31) when a clock is selected */
    if (csr & 0x1f) {
        csr |= (1u << 31);
    }
    return csr;
}

static void asclin_flagsset_write(TriCoreASCLINState *s, uint32_t value)
{
    qatomic_or(&s->regs[R_FLAGS], value);
    asclin_pulse_irq(s, value);
}

static void asclin_flagsclear_write(TriCoreASCLINState *s, uint32_t value)
{
    qatomic_and(&s->regs[R_FLAGS], ~value);
}

/*
 * FLAGSENABLE write: newly-enabled bits whose FLAGS is already set
 * must produce a rising edge on the corresponding interrupt line.
 */
static void asclin_flagsenable_write(TriCoreASCLINState *s, uint32_t value)
{
    uint32_t old_en = s->regs[R_FLAGSENABLE];
    uint32_t newly_enabled = value & ~old_en;

    s->regs[R_FLAGSENABLE] = value;
    asclin_pulse_irq(s, newly_enabled & s->regs[R_FLAGS]);
}

static uint32_t asclin_reg_read(TriCoreASCLINState *s, hwaddr offset)
{
    hwaddr reg = offset >> 2;

    switch (reg) {
    case R_CLC:
    case R_IOCR:
    case R_ID:
    case R_BITCON:
    case R_FRAMECON:
    case R_DATCON:
    case R_BRG:
    case R_BRD:
    case R_LINCON:
    case R_LINBTIMER:
    case R_LINHTIMER:
    case R_FLAGS:
    case R_FLAGSENABLE:
        return s->regs[reg];
    case R_TXFIFOCON:
        return asclin_txfifocon_read(s);
    case R_RXFIFOCON:
        return asclin_rxfifocon_read(s);
    case R_FLAGSSET:
    case R_FLAGSCLEAR:
        return 0;
    case R_TXDATA:
        return 0;
    case R_RXDATA:
        return asclin_rxdata_read(s, false);
    case R_CSR:
        return asclin_csr_read(s);
    case R_RXDATAD:
        return asclin_rxdata_read(s, true);
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: read from unknown offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return 0;
    }
}

static uint64_t asclin_read(void *opaque, hwaddr offset, unsigned size)
{
    TriCoreASCLINState *s = opaque;

    return extract32(asclin_reg_read(s, offset), (offset & 3) * 8, size * 8);
}

static void asclin_write(void *opaque, hwaddr offset, uint64_t value,
                         unsigned size)
{
    TriCoreASCLINState *s = opaque;
    hwaddr reg = offset >> 2;
    unsigned shift = (offset & 3) * 8;
    uint32_t val;

    /*
     * 8/16-bit writes: merge into the stored register, except for the
     * write-only action registers where unwritten bytes must stay 0.
     */
    if (reg >= ASCLIN_R_MAX || reg == R_FLAGSSET || reg == R_FLAGSCLEAR ||
        reg == R_TXDATA) {
        val = value << shift;
    } else {
        val = deposit32(s->regs[reg], shift, size * 8, value);
    }

    switch (reg) {
    case R_CLC:
    case R_IOCR:
    case R_ID:
    case R_BITCON:
    case R_FRAMECON:
    case R_DATCON:
    case R_BRG:
    case R_BRD:
    case R_LINCON:
    case R_LINBTIMER:
    case R_LINHTIMER:
        s->regs[reg] = val;
        break;
    case R_TXFIFOCON:
        asclin_txfifocon_write(s, val);
        break;
    case R_RXFIFOCON:
        asclin_rxfifocon_write(s, val);
        break;
    case R_FLAGS:
        /* read-only hardware status */
        break;
    case R_FLAGSSET:
        asclin_flagsset_write(s, val);
        break;
    case R_FLAGSCLEAR:
        asclin_flagsclear_write(s, val);
        break;
    case R_FLAGSENABLE:
        asclin_flagsenable_write(s, val);
        break;
    case R_TXDATA:
        asclin_txdata_write(s, val);
        break;
    case R_RXDATA:
    case R_RXDATAD:
        /* read-only */
        break;
    case R_CSR:
        s->regs[R_CSR] = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to unknown offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        break;
    }
}

static const MemoryRegionOps asclin_ops = {
    .read = asclin_read,
    .write = asclin_write,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void asclin_uart_rx(void *opaque, const uint8_t *buf, int size)
{
    TriCoreASCLINState *s = opaque;

    while (size > 0) {
        if (asclin_rx_buf_free(s) == 0) {
            qatomic_or(&s->regs[R_FLAGS], ASCLIN_FLAGS_RFO);
            asclin_pulse_irq(s, ASCLIN_FLAGS_RFO);
            break;
        }
        s->rxbuf[s->rx_wridx] = *buf++;
        s->rx_wridx = (s->rx_wridx + 1) % ASCLIN_RX_BUF_SIZE;
        size--;
    }

    if (s->rx_rdidx != s->rx_wridx) {
        qatomic_or(&s->regs[R_FLAGS], ASCLIN_FLAGS_RFL);
        asclin_pulse_irq(s, ASCLIN_FLAGS_RFL);
    }
}

static int asclin_uart_can_rx(void *opaque)
{
    TriCoreASCLINState *s = TRICORE_ASCLIN(opaque);

    if ((s->regs[R_RXFIFOCON] & ASCLIN_RXFIFOCON_ENI) &&
        asclin_rx_buf_free(s) > 0) {
        return 1;
    }
    return 0;
}

static void asclin_uart_reset_hold(Object *obj, ResetType type)
{
    TriCoreASCLINState *s = TRICORE_ASCLIN(obj);
    int i;

    for (i = 0; i < ASCLIN_R_MAX; i++) {
        s->regs[i] = 0;
    }
    asclin_rx_buf_reset(s);
}

static void asclin_uart_realize(DeviceState *dev, Error **errp)
{
    TriCoreASCLINState *s = TRICORE_ASCLIN(dev);

    qemu_chr_fe_set_handlers(&s->chr, asclin_uart_can_rx,
                             asclin_uart_rx, NULL,
                             NULL, s, NULL, true);
}

static void asclin_uart_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    TriCoreASCLINState *s = TRICORE_ASCLIN(obj);

    memory_region_init_io(&s->iomem, obj, &asclin_ops, s,
                          TYPE_TRICORE_ASCLIN, 0x100);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq_rx);
    sysbus_init_irq(sbd, &s->irq_tx);
    sysbus_init_irq(sbd, &s->irq_err);
}

static int asclin_uart_post_load(void *opaque, int version_id)
{
    TriCoreASCLINState *s = TRICORE_ASCLIN(opaque);

    if (s->regs[R_FLAGS] & ASCLIN_FLAGS_TFL) {
        s->watch_tag = qemu_chr_fe_add_watch(&s->chr,
                                             G_IO_OUT | G_IO_HUP,
                                             asclin_tx_watch, s);
    }
    return 0;
}

static const VMStateDescription vmstate_asclin_uart = {
    .name = TYPE_TRICORE_ASCLIN,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = asclin_uart_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, TriCoreASCLINState, ASCLIN_R_MAX),
        VMSTATE_END_OF_LIST()
    },
};

static const Property asclin_uart_properties[] = {
    DEFINE_PROP_CHR("chardev", TriCoreASCLINState, chr),
};

static void asclin_uart_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = asclin_uart_realize;
    rc->phases.hold = asclin_uart_reset_hold;
    dc->vmsd = &vmstate_asclin_uart;
    device_class_set_props(dc, asclin_uart_properties);
}

static const TypeInfo asclin_uart_info = {
    .name = TYPE_TRICORE_ASCLIN,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(TriCoreASCLINState),
    .instance_init = asclin_uart_init,
    .class_init = asclin_uart_class_init,
};

static void asclin_uart_register_types(void)
{
    type_register_static(&asclin_uart_info);
}

type_init(asclin_uart_register_types)
