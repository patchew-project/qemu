/*
 * NXP i.MX LPUART (Low-Power UART) device model
 *
 * Copyright (c) 2026, Kyle Fox
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Modelled on hw/char/imx_serial.c for structural conventions but with
 * the LPUART register set and semantics. TX bytes are pushed through the
 * chardev backend synchronously; RX bytes are buffered one-deep and
 * surface in DATA, with an interrupt raised when CTRL.RIE is set.
 * FIFO depth is reported as 1; no DMA, no baud-rate timing, no flow
 * control are modelled.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/char/imx_lpuart.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "migration/vmstate.h"
#include "trace.h"

static void imx_lpuart_update_irq(IMXLPUARTState *s);

static int imx_lpuart_post_load(void *opaque, int version_id)
{
    /* Recompute the IRQ line level from the restored register state. */
    imx_lpuart_update_irq(opaque);
    return 0;
}

static const VMStateDescription vmstate_imx_lpuart = {
    .name = TYPE_IMX_LPUART,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = imx_lpuart_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(baud, IMXLPUARTState),
        VMSTATE_UINT32(stat, IMXLPUARTState),
        VMSTATE_UINT32(ctrl, IMXLPUARTState),
        VMSTATE_UINT32(match, IMXLPUARTState),
        VMSTATE_UINT32(modir, IMXLPUARTState),
        VMSTATE_UINT32(fifo, IMXLPUARTState),
        VMSTATE_UINT32(water, IMXLPUARTState),
        VMSTATE_UINT32(pincfg, IMXLPUARTState),
        VMSTATE_UINT8(rx_byte, IMXLPUARTState),
        VMSTATE_BOOL(rx_full, IMXLPUARTState),
        VMSTATE_END_OF_LIST()
    },
};

/* Recompute the IRQ line from STAT & CTRL interrupt-enable bits. */
static void imx_lpuart_update_irq(IMXLPUARTState *s)
{
    bool level =
        ((s->stat & LPUART_STAT_RDRF) && (s->ctrl & LPUART_CTRL_RIE)) ||
        ((s->stat & LPUART_STAT_TDRE) && (s->ctrl & LPUART_CTRL_TIE)) ||
        ((s->stat & LPUART_STAT_TC)   && (s->ctrl & LPUART_CTRL_TCIE)) ||
        ((s->stat & LPUART_STAT_IDLE) && (s->ctrl & LPUART_CTRL_ILIE));

    qemu_set_irq(s->irq, level);
}

static void imx_lpuart_reset(IMXLPUARTState *s)
{
    /*
     * Power-on reset values per the i.MX LPUART chapter. STAT comes up
     * with TDRE and TC set because the FIFO is empty.
     */
    s->baud   = 0x0F000004;     /* default OSR = 4 + SBR field */
    s->stat   = LPUART_STAT_TDRE | LPUART_STAT_TC;
    s->ctrl   = 0;
    s->match  = 0;
    s->modir  = 0;
    s->fifo   = 0;
    s->water  = 0;
    s->pincfg = 0;
    s->rx_byte = 0;
    s->rx_full = false;
}

static void imx_lpuart_reset_at_boot_hold(Object *obj, ResetType type)
{
    IMXLPUARTState *s = IMX_LPUART(obj);

    imx_lpuart_reset(s);
    imx_lpuart_update_irq(s);
}

static uint64_t imx_lpuart_read(void *opaque, hwaddr offset, unsigned size)
{
    IMXLPUARTState *s = opaque;
    uint32_t value = 0;
    uint32_t c;

    switch (offset) {
    case LPUART_VERID:
        value = LPUART_VERID_VALUE;
        break;

    case LPUART_PARAM:
        /* TXFIFO and RXFIFO size fields both zero -> depth = 1. */
        value = 0;
        break;

    case LPUART_GLOBAL:
        value = 0;
        break;

    case LPUART_PINCFG:
        value = s->pincfg;
        break;

    case LPUART_BAUD:
        value = s->baud;
        break;

    case LPUART_STAT:
        value = s->stat;
        break;

    case LPUART_CTRL:
        value = s->ctrl;
        break;

    case LPUART_DATA:
        if (s->rx_full) {
            c = s->rx_byte;
            s->rx_full = false;
            s->stat &= ~LPUART_STAT_RDRF;
            value = c & LPUART_DATA_MASK;
            imx_lpuart_update_irq(s);
            qemu_chr_fe_accept_input(&s->chr);
        } else {
            value = LPUART_DATA_RXEMPT;
        }
        break;

    case LPUART_MATCH:
        value = s->match;
        break;

    case LPUART_MODIR:
        value = s->modir;
        break;

    case LPUART_FIFO:
        /*
         * Report the FIFO control bits the guest set, plus the read-only
         * TXEMPT/RXEMPT status computed from our 1-deep model. The status
         * bits must be derived fresh, never returned from s->fifo: the Linux
         * driver read-modify-writes UARTFIFO during setup, so a stored RXEMPT
         * would stick set and make its RX drain loop (while !(FIFO & RXEMPT))
         * believe the RX FIFO is always empty - it would never read DATA, RDRF
         * would never clear, and the RX interrupt would storm.
         */
        value = s->fifo & ~(LPUART_FIFO_TXEMPT | LPUART_FIFO_RXEMPT);
        value |= LPUART_FIFO_TXEMPT;  /* we drain TX instantly */
        if (!s->rx_full) {
            value |= LPUART_FIFO_RXEMPT;
        }
        break;

    case LPUART_WATER:
        /*
         * Reflect the current fifo state in the RX/TX count fields
         * (bits[31:24] RXCOUNT, bits[15:8] TXCOUNT). U-Boot's
         * _lpuart32_serial_tstc() polls (water >> 24) for incoming-
         * char detection, so RXCOUNT must update when imx_lpuart_receive
         * stashes a byte. We are 1-deep: 0 or 1 in either slot.
         * Preserve the guest-written watermark bits unchanged.
         */
        value = (s->water & 0x00ff00ffu)
              | ((uint32_t)(s->rx_full ? 1 : 0) << 24)
              | 0u;  /* TXCOUNT always 0 - we drain TR writes instantly */
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad read offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        break;
    }

    return value;
}

static void imx_lpuart_write(void *opaque, hwaddr offset,
                             uint64_t value, unsigned size)
{
    IMXLPUARTState *s = opaque;
    uint8_t ch;

    switch (offset) {
    case LPUART_VERID:
    case LPUART_PARAM:
        /* Read-only registers; silently ignore writes (HW behavior). */
        break;

    case LPUART_GLOBAL:
        if (value & LPUART_GLOBAL_RST) {
            imx_lpuart_reset(s);
            imx_lpuart_update_irq(s);
        }
        break;

    case LPUART_PINCFG:
        s->pincfg = value;
        break;

    case LPUART_BAUD:
        /*
         * Baud-rate timing isn't modelled, but the driver reads back what
         * it wrote. Preserve DMA-enable and other configuration bits.
         */
        s->baud = value;
        trace_imx_lpuart_baud(s->baud);
        break;

    case LPUART_STAT:
        /*
         * STAT bits IDLE and OR are W1C; the rest are read-only status
         * managed by the model.
         */
        s->stat &= ~(value & LPUART_STAT_W1C_MASK);
        imx_lpuart_update_irq(s);
        break;

    case LPUART_CTRL:
        s->ctrl = value;
        imx_lpuart_update_irq(s);
        break;

    case LPUART_DATA:
        ch = value & LPUART_DATA_MASK;
        /*
         * Always push the byte through the chardev backend, even if
         * CTRL_TE is currently 0. Real silicon would queue or drop the
         * byte until TE is enabled; in emulation we don't model that
         * gating because some consumers (notably the Linux earlycon
         * path under -kernel direct boot, which assumes firmware
         * already set TE before the kernel runs) write to DATA without
         * first enabling TE themselves. Without this, kernel console
         * output vanishes silently and earlycon appears broken.
         */
        trace_imx_lpuart_tx(ch);
        qemu_chr_fe_write_all(&s->chr, &ch, 1);
        /*
         * TDRE and TC are pinned high; the byte goes out instantly. A
         * future model would clear them for one host tick, then set TC
         * when the line goes idle.
         */
        s->stat |= LPUART_STAT_TDRE | LPUART_STAT_TC;
        imx_lpuart_update_irq(s);
        break;

    case LPUART_MATCH:
        s->match = value;
        break;

    case LPUART_MODIR:
        s->modir = value;
        break;

    case LPUART_FIFO:
        /*
         * Store only the writable configuration bits. The read-only status
         * bits (TXEMPT/RXEMPT/TXOF/RXUF) and the self-clearing flush commands
         * (TXFLUSH/RXFLUSH) must NOT be latched: the driver read-modify-writes
         * UARTFIFO, so latching RXEMPT would make it stick set and break the
         * RX drain loop (see the FIFO read).
         */
        s->fifo = value & ~LPUART_FIFO_RO_MASK;
        if (value & LPUART_FIFO_RXFLUSH) {
            s->rx_full = false;
            s->stat &= ~LPUART_STAT_RDRF;
        }
        imx_lpuart_update_irq(s);
        break;

    case LPUART_WATER:
        s->water = value;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad write offset 0x%" HWADDR_PRIx
                      " value 0x%" PRIx64 "\n",
                      __func__, offset, value);
        break;
    }
}

static int imx_lpuart_can_receive(void *opaque)
{
    IMXLPUARTState *s = opaque;

    return ((s->ctrl & LPUART_CTRL_RE) && !s->rx_full) ? 1 : 0;
}

static void imx_lpuart_receive(void *opaque, const uint8_t *buf, int size)
{
    IMXLPUARTState *s = opaque;

    if (size <= 0 || s->rx_full) {
        return;
    }

    s->rx_byte = buf[0];
    s->rx_full = true;
    trace_imx_lpuart_rx(s->rx_byte);
    s->stat |= LPUART_STAT_RDRF;
    imx_lpuart_update_irq(s);
}

static const MemoryRegionOps imx_lpuart_ops = {
    .read = imx_lpuart_read,
    .write = imx_lpuart_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void imx_lpuart_realize(DeviceState *dev, Error **errp)
{
    IMXLPUARTState *s = IMX_LPUART(dev);

    qemu_chr_fe_set_handlers(&s->chr, imx_lpuart_can_receive,
                             imx_lpuart_receive, NULL, NULL, s, NULL, true);
}

static void imx_lpuart_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    IMXLPUARTState *s = IMX_LPUART(obj);

    memory_region_init_io(&s->iomem, obj, &imx_lpuart_ops, s,
                          TYPE_IMX_LPUART, IMX_LPUART_REG_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static const Property imx_lpuart_properties[] = {
    DEFINE_PROP_CHR("chardev", IMXLPUARTState, chr),
};

static void imx_lpuart_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = imx_lpuart_realize;
    dc->vmsd = &vmstate_imx_lpuart;
    rc->phases.hold = imx_lpuart_reset_at_boot_hold;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
    dc->desc = "NXP i.MX LPUART";
    device_class_set_props(dc, imx_lpuart_properties);
}

static const TypeInfo imx_lpuart_info = {
    .name           = TYPE_IMX_LPUART,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(IMXLPUARTState),
    .instance_init  = imx_lpuart_init,
    .class_init     = imx_lpuart_class_init,
};

static void imx_lpuart_register_types(void)
{
    type_register_static(&imx_lpuart_info);
}

type_init(imx_lpuart_register_types)
