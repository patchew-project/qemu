/*
 * PIC32MK UART × 6 (U1–U6)
 * Datasheet: DS60001519E, §21 (pp. 475–552)
 *
 * Each UART instance is a SysBusDevice with:
 *   - One MMIO region (PIC32MK_UART_BLOCK_SIZE bytes)
 *   - CharFrontend for TX/RX via QEMU backend (-serial stdio, etc.)
 *   - Three IRQ outputs: index 0 = RX, 1 = TX, 2 = Error
 *
 * Register layout (all 4-byte registers with SET/CLR/INV at +4/+8/+C):
 *   UxMODE  +0x00  Mode (UARTEN/ON bits, parity, stop bits)
 *   UxSTA   +0x10  Status + TX/RX interrupt select
 *   UxTXREG +0x20  TX data (write-only)
 *   UxRXREG +0x30  RX data (read-only)
 *   UxBRG   +0x40  Baud rate generator
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

/*
 * Device state
 * -----------------------------------------------------------------------
 */

#define TYPE_PIC32MK_UART   "pic32mk-uart"
OBJECT_DECLARE_SIMPLE_TYPE(PIC32MKUartState, PIC32MK_UART)

struct PIC32MKUartState {
    SysBusDevice parent_obj;
    MemoryRegion mr;

    /* Registers (base values; SET/CLR/INV are decoded in the write handler) */
    uint32_t mode;  /* UxMODE */
    uint32_t sta;   /* UxSTA  */
    uint32_t brg;   /* UxBRG  */

    /* RX single-byte buffer */
    uint8_t  rxbuf;
    bool     rxbuf_full;

    CharFrontend chr;

    /* IRQ outputs: 0=RX, 1=TX, 2=Error */
    qemu_irq irq_rx;
    qemu_irq irq_tx;
    qemu_irq irq_err;
};

/*
 * UxSTA status helpers
 * -----------------------------------------------------------------------
 */

static void uart_update_sta(PIC32MKUartState *s)
{
    /* TRMT (bit 8): TX shift register empty — always 1 (we forward immediately) */
    s->sta |= PIC32MK_USTA_TRMT;
    /* UTXBF (bit 9): TX buffer full — always 0 */
    s->sta &= ~PIC32MK_USTA_UTXBF;

    /* URXDA (bit 0): RX data available */
    if (s->rxbuf_full) {
        s->sta |= PIC32MK_USTA_URXDA;
    } else {
        s->sta &= ~PIC32MK_USTA_URXDA;
    }
}

static void uart_update_irq(PIC32MKUartState *s)
{
    /* RX IRQ: assert while URXDA=1 and URXEN is set */
    bool rx_pending = s->rxbuf_full && (s->sta & PIC32MK_USTA_URXEN);
    qemu_set_irq(s->irq_rx, rx_pending ? 1 : 0);

    /*
     * TX IRQ: level-based.  In QEMU the TX path is instantaneous — chars
     * go out immediately via qemu_chr_fe_write_all(), so TRMT=1 and
     * UTXBF=0 at all times.  Regardless of UTXISEL[1:0], the TX
     * interrupt condition is always met whenever ON=1 and UTXEN=1.
     *
     * The EVIC tracks source levels (irq_level[]) and re-asserts IFS
     * bits after firmware clears them, so keeping irq_tx asserted at
     * level=1 causes the IFS flag to be re-set immediately after clear
     * — matching real Microchip behavior.  The ISR must disable IEC
     * (the enable bit) to stop re-entry, which is exactly what the
     * Microchip plib does.
     */
    bool tx_active = (s->mode & PIC32MK_UMODE_ON) &&
                     (s->sta & PIC32MK_USTA_UTXEN);
    qemu_set_irq(s->irq_tx, tx_active ? 1 : 0);
}

/*
 * CharFrontend callbacks (RX path)
 * -----------------------------------------------------------------------
 */

static int uart_can_receive(void *opaque)
{
    PIC32MKUartState *s = opaque;
    return s->rxbuf_full ? 0 : 1;
}

static void uart_receive(void *opaque, const uint8_t *buf, int size)
{
    PIC32MKUartState *s = opaque;

    if (size == 0) {
        return;
    }

    if (s->rxbuf_full) {
        /* Overrun: set OERR bit */
        s->sta |= PIC32MK_USTA_OERR;
        qemu_set_irq(s->irq_err, 1);
        return;
    }

    s->rxbuf      = buf[0];
    s->rxbuf_full = true;
    uart_update_sta(s);
    uart_update_irq(s);
}

/*
 * MMIO helpers
 * -----------------------------------------------------------------------
 */

/* Apply SET/CLR/INV operation based on sub-register offset */
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

/* Map UART register base address to state field */
static uint32_t *uart_find_reg(PIC32MKUartState *s, hwaddr base)
{
    switch (base) {
    case PIC32MK_UxMODE:
        return &s->mode;
    case PIC32MK_UxSTA:
        return &s->sta;
    case PIC32MK_UxBRG:
        return &s->brg;
    default:
        return NULL;
    }
}

/*
 * MMIO read/write
 * -----------------------------------------------------------------------
 */

static uint64_t uart_read(void *opaque, hwaddr addr, unsigned size)
{
    PIC32MKUartState *s = opaque;

    uart_update_sta(s);

    /* RX register is special: reading consumes the byte */
    if ((addr & ~(hwaddr)0xF) == PIC32MK_UxRXREG) {
        if (s->rxbuf_full) {
            uint32_t val  = s->rxbuf;
            s->rxbuf_full = false;
            uart_update_sta(s);
            uart_update_irq(s);
            qemu_chr_fe_accept_input(&s->chr);
            return val;
        }
        return 0;
    }

    /* TX register is write-only */
    if ((addr & ~(hwaddr)0xF) == PIC32MK_UxTXREG) {
        return 0;
    }

    hwaddr base   = addr & ~(hwaddr)0xF;
    uint32_t *reg = uart_find_reg(s, base);
    if (reg) {
        return *reg;
    }

    qemu_log_mask(LOG_UNIMP,
                  "pic32mk_uart: unimplemented read @ 0x%04" HWADDR_PRIx "\n",
                  addr);
    return 0;
}

static void uart_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PIC32MKUartState *s = opaque;

    /* TX register: writing transmits a byte */
    if ((addr & ~(hwaddr)0xF) == PIC32MK_UxTXREG) {
        uint8_t ch = (uint8_t)(val & 0xFF);
        qemu_chr_fe_write_all(&s->chr, &ch, 1);
        return;
    }

    int sub       = (int)(addr & 0xF);
    hwaddr base   = addr & ~(hwaddr)0xF;
    uint32_t *reg = uart_find_reg(s, base);

    if (!reg) {
        qemu_log_mask(LOG_UNIMP,
                      "pic32mk_uart: unimplemented write @ 0x%04"
                      HWADDR_PRIx " = 0x%08" PRIx64 "\n",
                      addr, val);
        return;
    }

    apply_sci(reg, (uint32_t)val, sub);

    /* Keep status bits consistent after firmware writes to STA */
    if (base == PIC32MK_UxSTA) {
        /* Firmware clears OERR by writing 0 to that bit */
        if (!(s->sta & PIC32MK_USTA_OERR)) {
            qemu_set_irq(s->irq_err, 0);
        }
        uart_update_irq(s);
    }
    if (base == PIC32MK_UxMODE) {
        uart_update_irq(s);
    }
}

static const MemoryRegionOps uart_ops = {
    .read       = uart_read,
    .write      = uart_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

/*
 * Device lifecycle
 * -----------------------------------------------------------------------
 */

static void pic32mk_uart_reset(DeviceState *dev)
{
    PIC32MKUartState *s = PIC32MK_UART(dev);

    s->mode       = 0;
    /* Reset STA: TRMT=1, TX buffer not full */
    s->sta        = PIC32MK_USTA_TRMT;
    s->brg        = 0;
    s->rxbuf_full = false;
    s->rxbuf      = 0;

    qemu_set_irq(s->irq_rx,  0);
    qemu_set_irq(s->irq_tx,  0);
    qemu_set_irq(s->irq_err, 0);
}

static void pic32mk_uart_realize(DeviceState *dev, Error **errp)
{
    PIC32MKUartState *s = PIC32MK_UART(dev);

    qemu_chr_fe_set_handlers(&s->chr,
                             uart_can_receive, uart_receive,
                             NULL, NULL,
                             s, NULL, true);
}

static void pic32mk_uart_init(Object *obj)
{
    PIC32MKUartState *s = PIC32MK_UART(obj);

    memory_region_init_io(&s->mr, obj, &uart_ops, s,
                          TYPE_PIC32MK_UART, PIC32MK_UART_BLOCK_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mr);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq_rx);   /* index 0 */
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq_tx);   /* index 1 */
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq_err);  /* index 2 */
}

static const Property pic32mk_uart_props[] = {
    DEFINE_PROP_CHR("chardev", PIC32MKUartState, chr),
};

static void pic32mk_uart_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    device_class_set_legacy_reset(dc, pic32mk_uart_reset);
    device_class_set_props(dc, pic32mk_uart_props);
    dc->realize = pic32mk_uart_realize;
}

static const TypeInfo pic32mk_uart_info = {
    .name          = TYPE_PIC32MK_UART,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PIC32MKUartState),
    .instance_init = pic32mk_uart_init,
    .class_init    = pic32mk_uart_class_init,
};

static void pic32mk_uart_register_types(void)
{
    type_register_static(&pic32mk_uart_info);
}

type_init(pic32mk_uart_register_types)
