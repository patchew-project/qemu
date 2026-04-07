/*
 * QEMU OpenTitan UART device
 *
 * Copyright (c) 2020 Western Digital
 * Copyright (c) 2022-2025 Rivos, Inc.
 * Copyright (c) 2025-2026 lowRISC contributors.
 *
 * For details check the documentation here:
 *    https://opentitan.org/book/hw/ip/uart/doc/
 *
 * Author(s):
 *  Alistair Francis <alistair.francis@wdc.com>
 *  Loïc Lefort <loic@rivosinc.com>
 *  Lex Bailey <lex.bailey@lowrisc.com>
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "qemu/osdep.h"
#include "hw/char/ot_uart.h"
#include "qemu/fifo8.h"
#include "hw/core/cpu.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/core/registerfields.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"

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

#define OT_UART_NCO_BITS     16
#define OT_UART_TX_FIFO_SIZE 128
#define OT_UART_RX_FIFO_SIZE 128
#define OT_UART_IRQ_NUM      9

#define R32_OFF(_r_) ((_r_) / sizeof(uint32_t))

#define R_LAST_REG (R_TIMEOUT_CTRL)
#define REGS_COUNT (R_LAST_REG + 1u)
#define REGS_SIZE  (REGS_COUNT * sizeof(uint32_t))
#define REG_NAME(_reg_) \
    ((((_reg_) < REGS_COUNT) && REG_NAMES[_reg_]) ? REG_NAMES[_reg_] : "?")

#define REG_NAME_ENTRY(_reg_) [R_##_reg_] = stringify(_reg_)
static const char *REG_NAMES[REGS_COUNT] = {
    REG_NAME_ENTRY(INTR_STATE),
    REG_NAME_ENTRY(INTR_ENABLE),
    REG_NAME_ENTRY(INTR_TEST),
    REG_NAME_ENTRY(ALERT_TEST),
    REG_NAME_ENTRY(CTRL),
    REG_NAME_ENTRY(STATUS),
    REG_NAME_ENTRY(RDATA),
    REG_NAME_ENTRY(WDATA),
    REG_NAME_ENTRY(FIFO_CTRL),
    REG_NAME_ENTRY(FIFO_STATUS),
    REG_NAME_ENTRY(OVRD),
    REG_NAME_ENTRY(VAL),
    REG_NAME_ENTRY(TIMEOUT_CTRL),
};
#undef REG_NAME_ENTRY

static void ot_uart_update_irqs(OtUARTState *s)
{
    uint32_t state_masked = s->regs[R_INTR_STATE] & s->regs[R_INTR_ENABLE];

    trace_ot_uart_irqs(s->ot_id, s->regs[R_INTR_STATE], s->regs[R_INTR_ENABLE],
                       state_masked);

    for (int index = 0; index < OT_UART_IRQ_NUM; index++) {
        bool level = (state_masked & (1U << index)) != 0;
        qemu_set_irq(s->irqs[index], level);
    }
}

static bool ot_uart_is_sys_loopack_enabled(const OtUARTState *s)
{
    return FIELD_EX32(s->regs[R_CTRL], CTRL, SLPBK);
}

static bool ot_uart_is_tx_enabled(const OtUARTState *s)
{
    return FIELD_EX32(s->regs[R_CTRL], CTRL, TX);
}

static bool ot_uart_is_rx_enabled(const OtUARTState *s)
{
    return FIELD_EX32(s->regs[R_CTRL], CTRL, RX);
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

    if (size && !s->toggle_break) {
        /* no longer breaking, so emulate idle in oversampled VAL register */
        s->in_break = false;
    }

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
    if (ot_uart_is_rx_enabled(s)) {
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

    if (ot_uart_is_sys_loopack_enabled(s)) {
        /* system loopback mode, just forward to RX FIFO */
        uint32_t count = fifo8_num_used(&s->tx_fifo);
        buf = fifo8_pop_bufptr(&s->tx_fifo, count, &size);
        ot_uart_receive(s, buf, (int)size);
        count -= size;
        /*
         * there may be more data to send if data wraps around the end of TX
         * FIFO
         */
        if (count) {
            buf = fifo8_pop_bufptr(&s->tx_fifo, count, &size);
            ot_uart_receive(s, buf, (int)size);
        }
    } else {
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

    if (ot_uart_is_tx_enabled(s)) {
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

    s->tx_watermark_level = 0;
    for (unsigned index = 0; index < ARRAY_SIZE(s->irqs); index++) {
        qemu_set_irq(s->irqs[index], 0);
    }
    ot_uart_reset_tx_fifo(s);
    ot_uart_reset_rx_fifo(s);

    s->tx_level = 0;
    s->rx_level = 0;

    s->char_tx_time = (NANOSECONDS_PER_SECOND / 230400) * 10;

    /*
     * do not reset `s->in_break`, as that tracks whether we are currently
     * receiving a break condition over UART RX from some device talking
     * to OpenTitan, which should survive resets. The QEMU CharDev only
     * supports transient break events and not the notion of holding the
     * UART in break, so remembering breaks like this is required to
     * support mocking of break conditions in the oversampled `VAL` reg.
     */
    if (s->in_break) {
        /* ignore CTRL.RXBLVL as we have no notion of break "time" */
        s->regs[R_INTR_STATE] |= INTR_RX_BREAK_ERR_MASK;
    }

    ot_uart_update_irqs(s);
}

static void ot_uart_event_handler(void *opaque, QEMUChrEvent event)
{
    OtUARTState *s = opaque;

    if (event == CHR_EVENT_BREAK) {
        if (!s->in_break || !s->oversample_break) {
            /* ignore CTRL.RXBLVL as we have no notion of break "time" */
            s->regs[R_INTR_STATE] |= INTR_RX_BREAK_ERR_MASK;
            ot_uart_update_irqs(s);
            /* emulate break in the oversampled VAL register */
            s->in_break = true;
        } else if (s->toggle_break) {
            /* emulate toggling break off in the oversampled VAL register */
            s->in_break = false;
        }
    }
}

static uint8_t ot_uart_read_rx_fifo(OtUARTState *s)
{
    uint8_t val;

    if (!(s->regs[R_CTRL] & R_CTRL_RX_MASK)) {
        return 0;
    }

    if (fifo8_is_empty(&s->rx_fifo)) {
        return 0;
    }

    val = fifo8_pop(&s->rx_fifo);

    if (ot_uart_is_rx_enabled(s) && !ot_uart_is_sys_loopack_enabled(s)) {
        qemu_chr_fe_accept_input(&s->chr);
    }

    return val;
}

static gboolean ot_uart_watch_cb(void *do_not_use, GIOCondition cond,
                                 void *opaque)
{
    OtUARTState *s = opaque;

    s->watch_tag = 0;
    ot_uart_xmit(s);

    return FALSE;
}

static uint64_t ot_uart_get_baud(OtUARTState *s)
{
    uint64_t baud;

    baud = ((s->regs[R_CTRL] & R_CTRL_NCO_MASK) >> 16);
    baud *= clock_get_hz(s->f_clk);
    baud >>= 20;

    if (baud) {
        trace_ot_uart_check_baudrate(s->ot_id, baud);
    }

    return baud;
}

static uint64_t ot_uart_read(void *opaque, hwaddr addr, unsigned int size)
{
    OtUARTState *s = opaque;
    uint64_t retvalue = 0;

    hwaddr reg = R32_OFF(addr);
    switch (reg) {
    case R_INTR_STATE:
    case R_INTR_ENABLE:
    case R_CTRL:
    case R_FIFO_CTRL:
        retvalue = s->regs[reg];
        break;
    case R_STATUS:
        /* assume that UART always report RXIDLE */
        retvalue = R_STATUS_RXIDLE_MASK;
        /* report RXEMPTY or RXFULL */
        switch (fifo8_num_used(&s->rx_fifo)) {
        case 0:
            retvalue |= R_STATUS_RXEMPTY_MASK;
            break;
        case OT_UART_RX_FIFO_SIZE:
            retvalue |= R_STATUS_RXFULL_MASK;
            break;
        default:
            break;
        }
        /* report TXEMPTY+TXIDLE or TXFULL */
        switch (fifo8_num_used(&s->tx_fifo)) {
        case 0:
            retvalue |= R_STATUS_TXEMPTY_MASK | R_STATUS_TXIDLE_MASK;
            break;
        case OT_UART_TX_FIFO_SIZE:
            retvalue |= R_STATUS_TXFULL_MASK;
            break;
        default:
            break;
        }
        if (!ot_uart_is_tx_enabled(s)) {
            retvalue |= R_STATUS_TXIDLE_MASK;
        }
        if (!ot_uart_is_rx_enabled(s)) {
            retvalue |= R_STATUS_RXIDLE_MASK;
        }
        break;

    case R_RDATA:
        retvalue = (uint32_t)ot_uart_read_rx_fifo(s);
        break;

    case R_FIFO_STATUS:
        retvalue =
            (fifo8_num_used(&s->rx_fifo) & 0xffu) << R_FIFO_STATUS_RXLVL_SHIFT;
        retvalue |=
            (fifo8_num_used(&s->tx_fifo) & 0xffu) << R_FIFO_STATUS_TXLVL_SHIFT;
        break;

    case R_VAL:
        /*
         * This is not trivially implemented due to the QEMU UART
         * interface. There is no way to reliably sample or oversample
         * given our emulated interface, but some software might poll the
         * value of this register to determine break conditions.
         *
         * As such, default to reporting 16 of the last sample received
         * instead. This defaults to 16 idle high samples (as a stop bit is
         * always the last received), except for when the `oversample-break`
         * property is set and a break condition is received over UART RX,
         * where we then show 16 low samples until the next valid UART
         * transmission is received (or break is toggled off with the
         * `toggle-break` property enabled). This will not be accurate, but
         * should be sufficient to support basic software flows that
         * essentially use UART break as a strapping mechanism.
         */
        retvalue = (s->in_break && s->oversample_break) ? 0u : UINT16_MAX;
        qemu_log_mask(LOG_UNIMP, "%s: VAL only shows idle%s\n", __func__,
                      (s->oversample_break ? "/break" : ""));
        break;
    case R_OVRD:
    case R_TIMEOUT_CTRL:
        retvalue = s->regs[reg];
        qemu_log_mask(LOG_UNIMP,
                      "%s: %s is not supported\n", __func__, REG_NAME(reg));
        break;

    case R_ALERT_TEST:
    case R_INTR_TEST:
    case R_WDATA:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: %s is write only\n", __func__, REG_NAME(reg));
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
        return 0;
    }

    uint32_t pc = current_cpu->cc->get_pc(current_cpu);
    trace_ot_uart_io_read_out(s->ot_id, (uint32_t)addr, REG_NAME(reg),
                              retvalue, pc);

    return retvalue;
}

static void ot_uart_write(void *opaque, hwaddr addr, uint64_t val64,
                          unsigned int size)
{
    OtUARTState *s = opaque;
    uint32_t value = val64;

    hwaddr reg = R32_OFF(addr);

    uint32_t pc = current_cpu->cc->get_pc(current_cpu);
    trace_ot_uart_io_write(s->ot_id, (uint32_t)addr, REG_NAME(reg), value, pc);

    switch (reg) {
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
    case R_ALERT_TEST:
        value &= R_ALERT_TEST_FATAL_FAULT_MASK;
        s->regs[reg] = value;
        /* This will also set an IRQ once the alert handler is added */
        break;
    case R_CTRL:
        s->regs[R_CTRL] = value;

        if (value & R_CTRL_NF_MASK) {
            qemu_log_mask(LOG_UNIMP,
                          "%s: UART_CTRL_NF is not supported\n", __func__);
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
    case R_WDATA:
        uart_write_tx_fifo(s, (uint8_t)(value & R_WDATA_WDATA_MASK));
        break;

    case R_FIFO_CTRL:
        s->regs[R_FIFO_CTRL] =
            value & (R_FIFO_CTRL_RXILVL_MASK | R_FIFO_CTRL_TXILVL_MASK);
        if (value & R_FIFO_CTRL_RXRST_MASK) {
            ot_uart_reset_rx_fifo(s);
            ot_uart_update_irqs(s);
        }
        if (value & R_FIFO_CTRL_TXRST_MASK) {
            ot_uart_reset_tx_fifo(s);
            ot_uart_update_irqs(s);
        }
        break;
    case R_OVRD:
        if (value & R_OVRD_TXEN_MASK) {
            qemu_log_mask(LOG_UNIMP, "%s: OVRD.TXEN is not supported\n",
                          __func__);
        }
        s->regs[R_OVRD] = value & R_OVRD_TXVAL_MASK;
        break;

    case R_TIMEOUT_CTRL:
        s->regs[R_TIMEOUT_CTRL] =
            value & (R_TIMEOUT_CTRL_EN_MASK | R_TIMEOUT_CTRL_VAL_MASK);
        qemu_log_mask(LOG_UNIMP,
                      "%s: timeout_ctrl is not supported\n", __func__);
        break;

    case R_STATUS:
    case R_RDATA:
    case R_FIFO_STATUS:
    case R_VAL:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: %s is read only\n", __func__, REG_NAME(reg));
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
    DEFINE_PROP_STRING("ot-id", OtUARTState, ot_id),
    DEFINE_PROP_CHR("chardev", OtUARTState, chr),
    DEFINE_PROP_BOOL("oversample-break", OtUARTState, oversample_break, false),
    DEFINE_PROP_BOOL("toggle-break", OtUARTState, toggle_break, false),
};

static int ot_uart_fe_change(void *opaque)
{
    OtUARTState *s = opaque;

    qemu_chr_fe_set_handlers(&s->chr, ot_uart_can_receive, ot_uart_receive,
                             ot_uart_event_handler, ot_uart_fe_change, s, NULL,
                             true);

    if (s->watch_tag > 0) {
        g_source_remove(s->watch_tag);
        s->watch_tag = qemu_chr_fe_add_watch(&s->chr, G_IO_OUT | G_IO_HUP,
                                             ot_uart_watch_cb, s);
    }

    return 0;
}

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

    g_assert(s->ot_id);

    s->fifo_trigger_handle = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                          fifo_trigger_update, s);

    fifo8_create(&s->tx_fifo, OT_UART_TX_FIFO_SIZE);
    fifo8_create(&s->rx_fifo, OT_UART_RX_FIFO_SIZE);

    qemu_chr_fe_set_handlers(&s->chr, ot_uart_can_receive, ot_uart_receive,
                             ot_uart_event_handler, ot_uart_fe_change, s, NULL,
                             true);
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
