/*
 * QEMU OpenTitan UART device
 *
 * Copyright (c) 2020 Western Digital
 *
 * For details check the documentation here:
 *    https://docs.opentitan.org/hw/ip/uart/doc/
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/char/ot_uart.h"
#include "qemu/fifo8.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/core/registerfields.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

REG32(INTR_STATE, 0x00)
    SHARED_FIELD(INTR_TX_WATERMARK, 0, 1)
    SHARED_FIELD(INTR_RX_WATERMARK, 1, 1)
    SHARED_FIELD(INTR_TX_DONE, 2, 1)
    SHARED_FIELD(INTR_RX_OVERFLOW, 3, 1)
    SHARED_FIELD(INTR_RX_FRAME_ERR, 4, 1)
    SHARED_FIELD(INTR_RX_BREAK_ERR, 5, 1)
    SHARED_FIELD(INTR_RX_TIMEOUT, 6, 1)
    SHARED_FIELD(INTR_RX_PARITY_ERR, 7, 1)
    SHARED_FIELD(INTR_TX_EMPTY, 8, 1)
REG32(INTR_ENABLE, 0x04)
REG32(INTR_TEST, 0x08)
REG32(ALERT_TEST, 0x0C)
    FIELD(ALERT_TEST, FATAL_FAULT, 0, 1)
REG32(CTRL, 0x10)
    FIELD(CTRL, TX, 0, 1)
    FIELD(CTRL, RX, 1, 1)
    FIELD(CTRL, NF, 2, 1)
    FIELD(CTRL, SLPBK, 4, 1)
    FIELD(CTRL, LLPBK, 5, 1)
    FIELD(CTRL, PARITY_EN, 6, 1)
    FIELD(CTRL, PARITY_ODD, 7, 1)
    FIELD(CTRL, RXBLVL, 8, 2)
    FIELD(CTRL, NCO, 16, 16)
REG32(STATUS, 0x14)
    FIELD(STATUS, TXFULL, 0, 1)
    FIELD(STATUS, RXFULL, 1, 1)
    FIELD(STATUS, TXEMPTY, 2, 1)
    FIELD(STATUS, TXIDLE, 3, 1)
    FIELD(STATUS, RXIDLE, 4, 1)
    FIELD(STATUS, RXEMPTY, 5, 1)
REG32(RDATA, 0x18)
    FIELD(RDATA, RDATA, 0, 8)
REG32(WDATA, 0x1C)
    FIELD(WDATA, WDATA, 0, 8)
REG32(FIFO_CTRL, 0x20)
    FIELD(FIFO_CTRL, RXRST, 0, 1)
    FIELD(FIFO_CTRL, TXRST, 1, 1)
    FIELD(FIFO_CTRL, RXILVL, 2, 3)
    FIELD(FIFO_CTRL, TXILVL, 5, 3)
REG32(FIFO_STATUS, 0x24)
    FIELD(FIFO_STATUS, TXLVL, 0, 8)
    FIELD(FIFO_STATUS, RXLVL, 16, 8)
REG32(OVRD, 0x28)
    FIELD(OVRD, TXEN, 0, 1)
    FIELD(OVRD, TXVAL, 1, 1)
REG32(VAL, 0x2C)
    FIELD(VAL, RX, 0, 16)
REG32(TIMEOUT_CTRL, 0x30)
    FIELD(TIMEOUT_CTRL, VAL, 0, 24)
    FIELD(TIMEOUT_CTRL, EN, 31, 1)

#define INTR_MASK \
    (INTR_TX_WATERMARK_MASK | INTR_RX_WATERMARK_MASK | INTR_TX_DONE_MASK | \
     INTR_RX_OVERFLOW_MASK | INTR_RX_FRAME_ERR_MASK | INTR_RX_BREAK_ERR_MASK | \
     INTR_RX_TIMEOUT_MASK | INTR_RX_PARITY_ERR_MASK | INTR_TX_EMPTY_MASK)

#define CTRL_MASK \
    (R_CTRL_TX_MASK | R_CTRL_RX_MASK | R_CTRL_NF_MASK | R_CTRL_SLPBK_MASK | \
     R_CTRL_LLPBK_MASK | R_CTRL_PARITY_EN_MASK | R_CTRL_PARITY_ODD_MASK | \
     R_CTRL_RXBLVL_MASK | R_CTRL_NCO_MASK)

#define CTRL_SUP_MASK \
    (R_CTRL_RX_MASK | R_CTRL_TX_MASK | R_CTRL_SLPBK_MASK | R_CTRL_NCO_MASK)

#define OT_UART_NCO_BITS     16
#define OT_UART_TX_FIFO_SIZE 128
#define OT_UART_RX_FIFO_SIZE 128
#define OT_UART_IRQ_NUM      9

#define R32_OFF(_r_) ((_r_) / sizeof(uint32_t))

#define R_LAST_REG (R_TIMEOUT_CTRL)
#define REGS_COUNT (R_LAST_REG + 1u)
#define REGS_SIZE  (REGS_COUNT * sizeof(uint32_t))

static void ot_uart_update_irqs(OtUARTState *s)
{
    uint32_t state_masked = s->regs[R_INTR_STATE] & s->regs[R_INTR_ENABLE];

    for (int index = 0; index < OT_UART_IRQ_NUM; index++) {
        bool level = (state_masked & (1U << index)) != 0;
        qemu_set_irq(s->irqs[index], level);
    }
}

static int ot_uart_can_receive(void *opaque)
{
    OtUARTState *s = opaque;

    if (s->regs[R_CTRL] & R_CTRL_RX_MASK) {
        return (int)fifo8_num_free(&s->rx_fifo);
    }

    return 0;
}

static uint32_t ot_uart_get_rx_watermark_level(const OtUARTState *s)
{
    uint32_t rx_ilvl = FIELD_EX32(s->regs[R_FIFO_CTRL], FIFO_CTRL, RXILVL);

    return rx_ilvl < 7 ? (1 << rx_ilvl) : 126;
}

static void ot_uart_receive(void *opaque, const uint8_t *buf, int size)
{
    OtUARTState *s = opaque;
    uint32_t rx_watermark_level;
    size_t count = MIN(fifo8_num_free(&s->rx_fifo), (size_t)size);

    for (int index = 0; index < size; index++) {
        fifo8_push(&s->rx_fifo, buf[index]);
    }

    /* update INTR_STATE */
    if (count != size) {
        s->regs[R_INTR_STATE] |= INTR_RX_OVERFLOW_MASK;
    }
    rx_watermark_level = ot_uart_get_rx_watermark_level(s);
    if (rx_watermark_level && size >= rx_watermark_level) {
        s->regs[R_INTR_STATE] |= INTR_RX_WATERMARK_MASK;
    }

    ot_uart_update_irqs(s);
}

static void ot_uart_reset_tx_fifo(OtUARTState *s)
{
    fifo8_reset(&s->tx_fifo);
    s->regs[R_INTR_STATE] |= INTR_TX_EMPTY_MASK;
    s->regs[R_INTR_STATE] |= INTR_TX_DONE_MASK;
    if (s->tx_watermark_level) {
        s->regs[R_INTR_STATE] |= INTR_TX_WATERMARK_MASK;
        s->tx_watermark_level = 0;
    }
}

static void ot_uart_reset_rx_fifo(OtUARTState *s)
{
    fifo8_reset(&s->rx_fifo);
    s->regs[R_INTR_STATE] &= ~INTR_RX_WATERMARK_MASK;
    s->regs[R_INTR_STATE] &= ~INTR_RX_OVERFLOW_MASK;
    if (FIELD_EX32(s->regs[R_CTRL], CTRL, RX)) {
        qemu_chr_fe_accept_input(&s->chr);
    }
}

static uint32_t ot_uart_get_tx_watermark_level(const OtUARTState *s)
{
    uint32_t tx_ilvl = FIELD_EX32(s->regs[R_FIFO_CTRL], FIFO_CTRL, TXILVL);

    return tx_ilvl < 7 ? (1 << tx_ilvl) : 64;
}

static void ot_uart_xmit(OtUARTState *s)
{
    const uint8_t *buf;
    uint32_t size;
    int ret;

    if (fifo8_is_empty(&s->tx_fifo)) {
        return;
    }

    /* instant drain the fifo when there's no back-end */
    if (!qemu_chr_fe_backend_connected(&s->chr)) {
        ot_uart_reset_tx_fifo(s);
        ot_uart_update_irqs(s);
        return;
    }

    /* get a continuous buffer from the FIFO */
    buf =
        fifo8_peek_bufptr(&s->tx_fifo, fifo8_num_used(&s->tx_fifo), &size);
    /* send as much as possible */
    ret = qemu_chr_fe_write(&s->chr, buf, (int)size);
    /* if some characters where sent, remove them from the FIFO */
    if (ret >= 0) {
        fifo8_drop(&s->tx_fifo, ret);
    }

    /* update INTR_STATE */
    if (fifo8_is_empty(&s->tx_fifo)) {
        s->regs[R_INTR_STATE] |= INTR_TX_EMPTY_MASK;
        s->regs[R_INTR_STATE] |= INTR_TX_DONE_MASK;
    }
    if (s->tx_watermark_level &&
        fifo8_num_used(&s->tx_fifo) < s->tx_watermark_level) {
        s->regs[R_INTR_STATE] |= INTR_TX_WATERMARK_MASK;
        s->tx_watermark_level = 0;
    }

    ot_uart_update_irqs(s);
}

static void uart_write_tx_fifo(OtUARTState *s, uint8_t val)
{
    uint64_t current_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    if (fifo8_is_full(&s->tx_fifo)) {
        qemu_log_mask(LOG_GUEST_ERROR, "ot_uart: TX FIFO overflow");
        return;
    }

    fifo8_push(&s->tx_fifo, val);

    s->tx_watermark_level = ot_uart_get_tx_watermark_level(s);
    if (fifo8_num_used(&s->tx_fifo) < s->tx_watermark_level) {
        /*
         * TX watermark interrupt is raised when FIFO depth goes from above
         * watermark to below. If we haven't reached watermark, reset cached
         * watermark level
         */
        s->tx_watermark_level = 0;
    }

    if (FIELD_EX32(s->regs[R_CTRL], CTRL, TX)) {
        ot_uart_xmit(s);
    }

    timer_mod(s->fifo_trigger_handle, current_time +
              (s->char_tx_time * 4));
}

static void ot_uart_reset_enter(Object *obj, ResetType type)
{
    OtUARTClass *c = OT_UART_GET_CLASS(obj);
    OtUARTState *s = OT_UART(obj);

    if (c->parent_phases.enter) {
        c->parent_phases.enter(obj, type);
    }

    memset(&s->regs[0], 0, sizeof(s->regs));

    s->regs[R_STATUS] = 0x0000003c;

    s->tx_watermark_level = 0;
    for (unsigned index = 0; index < ARRAY_SIZE(s->irqs); index++) {
        qemu_set_irq(s->irqs[index], 0);
    }
    ot_uart_reset_tx_fifo(s);
    ot_uart_reset_rx_fifo(s);

    s->tx_level = 0;
    s->rx_level = 0;

    s->char_tx_time = (NANOSECONDS_PER_SECOND / 230400) * 10;

    ot_uart_update_irqs(s);
}

static uint64_t ot_uart_get_baud(OtUARTState *s)
{
    uint64_t baud;

    baud = ((s->regs[R_CTRL] & R_CTRL_NCO_MASK) >> 16);
    baud *= clock_get_hz(s->f_clk);
    baud >>= 20;

    return baud;
}

static uint64_t ot_uart_read(void *opaque, hwaddr addr, unsigned int size)
{
    OtUARTState *s = opaque;
    uint64_t retvalue = 0;

    switch (addr >> 2) {
    case R_INTR_STATE:
        retvalue = s->regs[R_INTR_STATE];
        break;
    case R_INTR_ENABLE:
        retvalue = s->regs[R_INTR_ENABLE];
        break;
    case R_INTR_TEST:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: wdata is write only\n", __func__);
        break;

    case R_CTRL:
        retvalue = s->regs[R_CTRL];
        break;
    case R_STATUS:
        retvalue = s->regs[R_STATUS];
        break;

    case R_RDATA:
        retvalue = s->regs[R_RDATA];
        if ((s->regs[R_CTRL] & R_CTRL_RX_MASK) && (s->rx_level > 0)) {
            qemu_chr_fe_accept_input(&s->chr);

            s->rx_level -= 1;
            s->regs[R_STATUS] &= ~R_STATUS_RXFULL_MASK;
            if (s->rx_level == 0) {
                s->regs[R_STATUS] |= R_STATUS_RXIDLE_MASK;
                s->regs[R_STATUS] |= R_STATUS_RXEMPTY_MASK;
            }
        }
        break;
    case R_WDATA:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: wdata is write only\n", __func__);
        break;

    case R_FIFO_CTRL:
        retvalue = s->regs[R_FIFO_CTRL];
        break;
    case R_FIFO_STATUS:
        retvalue = s->regs[R_FIFO_STATUS];

        retvalue |= (s->rx_level & 0x1F) << R_FIFO_STATUS_RXLVL_SHIFT;
        retvalue |= (s->tx_level & 0x1F) << R_FIFO_STATUS_TXLVL_SHIFT;

        qemu_log_mask(LOG_UNIMP,
                      "%s: RX fifos are not supported\n", __func__);
        break;

    case R_OVRD:
        retvalue = s->regs[R_OVRD];
        qemu_log_mask(LOG_UNIMP,
                      "%s: ovrd is not supported\n", __func__);
        break;
    case R_VAL:
        retvalue = s->regs[R_VAL];
        qemu_log_mask(LOG_UNIMP,
                      "%s: val is not supported\n", __func__);
        break;
    case R_TIMEOUT_CTRL:
        retvalue = s->regs[R_TIMEOUT_CTRL];
        qemu_log_mask(LOG_UNIMP,
                      "%s: timeout_ctrl is not supported\n", __func__);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
        return 0;
    }

    return retvalue;
}

static void ot_uart_write(void *opaque, hwaddr addr, uint64_t val64,
                          unsigned int size)
{
    OtUARTState *s = opaque;
    uint32_t value = val64;

    switch (addr >> 2) {
    case R_INTR_STATE:
        /* Write 1 clear */
        value &= INTR_MASK;
        s->regs[R_INTR_STATE] &= ~value;
        ot_uart_update_irqs(s);
        break;
    case R_INTR_ENABLE:
        value &= INTR_MASK;
        s->regs[R_INTR_ENABLE] = value;
        ot_uart_update_irqs(s);
        break;
    case R_INTR_TEST:
        value &= INTR_MASK;
        s->regs[R_INTR_STATE] |= value;
        ot_uart_update_irqs(s);
        break;

    case R_CTRL:
        s->regs[R_CTRL] = value;

        if (value & R_CTRL_NF_MASK) {
            qemu_log_mask(LOG_UNIMP,
                          "%s: UART_CTRL_NF is not supported\n", __func__);
        }
        if (value & R_CTRL_SLPBK_MASK) {
            qemu_log_mask(LOG_UNIMP,
                          "%s: UART_CTRL_SLPBK is not supported\n", __func__);
        }
        if (value & R_CTRL_LLPBK_MASK) {
            qemu_log_mask(LOG_UNIMP,
                          "%s: UART_CTRL_LLPBK is not supported\n", __func__);
        }
        if (value & R_CTRL_PARITY_EN_MASK) {
            qemu_log_mask(LOG_UNIMP,
                          "%s: UART_CTRL_PARITY_EN is not supported\n",
                          __func__);
        }
        if (value & R_CTRL_PARITY_ODD_MASK) {
            qemu_log_mask(LOG_UNIMP,
                          "%s: UART_CTRL_PARITY_ODD is not supported\n",
                          __func__);
        }
        if (value & R_CTRL_RXBLVL_MASK) {
            qemu_log_mask(LOG_UNIMP,
                          "%s: UART_CTRL_RXBLVL is not supported\n", __func__);
        }
        if (value & R_CTRL_NCO_MASK) {
            uint64_t baud = ot_uart_get_baud(s);

            s->char_tx_time = (NANOSECONDS_PER_SECOND / baud) * 10;
        }
        break;
    case R_STATUS:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: status is read only\n", __func__);
        break;

    case R_RDATA:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: rdata is read only\n", __func__);
        break;
    case R_WDATA:
        uart_write_tx_fifo(s, value);
        break;

    case R_FIFO_CTRL:
        s->regs[R_FIFO_CTRL] = value;

        if (value & R_FIFO_CTRL_RXRST_MASK) {
            s->rx_level = 0;
            qemu_log_mask(LOG_UNIMP,
                          "%s: RX fifos are not supported\n", __func__);
        }
        if (value & R_FIFO_CTRL_TXRST_MASK) {
            s->tx_level = 0;
        }
        break;
    case R_FIFO_STATUS:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: fifo_status is read only\n", __func__);
        break;
    case R_OVRD:
        if (value & R_OVRD_TXEN_MASK) {
            qemu_log_mask(LOG_UNIMP, "%s: OVRD.TXEN is not supported\n",
                          __func__);
        }
        s->regs[R_OVRD] = value & R_OVRD_TXVAL_MASK;
        break;
    case R_VAL:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: val is read only\n", __func__);
        break;
    case R_TIMEOUT_CTRL:
        s->regs[R_TIMEOUT_CTRL] =
            value & (R_TIMEOUT_CTRL_EN_MASK | R_TIMEOUT_CTRL_VAL_MASK);
        qemu_log_mask(LOG_UNIMP,
                      "%s: timeout_ctrl is not supported\n", __func__);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
    }
}

static void ot_uart_clk_update(void *opaque, ClockEvent event)
{
    OtUARTState *s = opaque;

    /* recompute uart's speed on clock change */
    uint64_t baud = ot_uart_get_baud(s);

    s->char_tx_time = (NANOSECONDS_PER_SECOND / baud) * 10;
}

static void fifo_trigger_update(void *opaque)
{
    OtUARTState *s = opaque;

    if (s->regs[R_CTRL] & R_CTRL_TX_MASK) {
        ot_uart_xmit(s);
    }
}

static const MemoryRegionOps ot_uart_ops = {
    .read = ot_uart_read,
    .write = ot_uart_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static int ot_uart_post_load(void *opaque, int version_id)
{
    OtUARTState *s = opaque;

    ot_uart_update_irqs(s);
    return 0;
}

static const VMStateDescription vmstate_ot_uart = {
    .name = TYPE_OT_UART,
    .version_id = 2,
    .minimum_version_id = 2,
    .post_load = ot_uart_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(tx_fifo, OtUARTState, 1, vmstate_fifo8, Fifo8),
        VMSTATE_STRUCT(rx_fifo, OtUARTState, 1, vmstate_fifo8, Fifo8),
        VMSTATE_UINT32(tx_level, OtUARTState),
        VMSTATE_UINT64(char_tx_time, OtUARTState),
        VMSTATE_TIMER_PTR(fifo_trigger_handle, OtUARTState),
        VMSTATE_ARRAY(regs, OtUARTState, REGS_COUNT, 1, vmstate_info_uint32,
                      uint32_t),
        VMSTATE_END_OF_LIST()
    }
};

static const Property ot_uart_properties[] = {
    DEFINE_PROP_CHR("chardev", OtUARTState, chr),
};

static void ot_uart_init(Object *obj)
{
    OtUARTState *s = OT_UART(obj);

    s->f_clk = qdev_init_clock_in(DEVICE(obj), "f_clock",
                                  ot_uart_clk_update, s, ClockUpdate);
    clock_set_hz(s->f_clk, OT_UART_CLOCK);

    for (unsigned index = 0; index < OT_UART_IRQ_NUM; index++) {
        sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irqs[index]);
    }

    memory_region_init_io(&s->mmio, obj, &ot_uart_ops, s, TYPE_OT_UART,
                          REGS_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);

    /*
     * These arrays have fixed sizes in the header. These assertions are used to
     * check that they are consistent with the definitions in this file. This is
     * ostensibly a runtime check, but may be optimised away by the compiler.
     */
    assert(REGS_SIZE == sizeof(s->regs));
    assert(OT_UART_IRQ_NUM * sizeof(qemu_irq) == sizeof(s->irqs));
}

static void ot_uart_realize(DeviceState *dev, Error **errp)
{
    OtUARTState *s = OT_UART(dev);

    s->fifo_trigger_handle = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                          fifo_trigger_update, s);

    fifo8_create(&s->tx_fifo, OT_UART_TX_FIFO_SIZE);
    fifo8_create(&s->rx_fifo, OT_UART_RX_FIFO_SIZE);

    qemu_chr_fe_set_handlers(&s->chr, ot_uart_can_receive,
                             ot_uart_receive, NULL, NULL,
                             s, NULL, true);
}

static void ot_uart_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    OtUARTClass *uc = OT_UART_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = ot_uart_realize;
    dc->vmsd = &vmstate_ot_uart;
    device_class_set_props(dc, ot_uart_properties);
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);

    resettable_class_set_parent_phases(rc, &ot_uart_reset_enter, NULL, NULL,
                                       &uc->parent_phases);
}

static const TypeInfo ot_uart_info = {
    .name          = TYPE_OT_UART,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(OtUARTState),
    .instance_init = ot_uart_init,
    .class_size    = sizeof(OtUARTClass),
    .class_init    = ot_uart_class_init,
};

static void ot_uart_register_types(void)
{
    type_register_static(&ot_uart_info);
}

type_init(ot_uart_register_types)
