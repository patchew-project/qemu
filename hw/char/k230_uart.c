/*
 * K230 UART
 *
 * Copyright (c) 2026 zhenbaii <1640586082@qq.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/char/k230_uart.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties-system.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/host-utils.h"
#include "system/memory.h"

/* Serial clock from the K230 DT "clock-frequency" property (50 MHz). */
#define K230_UART_SCLK_HZ   50000000ull

static void k230_uart_xmit(K230UartState *s);
static void k230_uart_update_all(K230UartState *s);
static uint64_t k230_uart_read_rbr(K230UartState *s);
static void k230_uart_push_rx_byte(K230UartState *s, uint8_t ch);
static void k230_uart_reset_hold(Object *obj, ResetType type);

/* ---- interrupt line ---- */

/*
 * RX FIFO trigger level. When rx_count reaches this,
 * the RX Data Available interrupt (IID=0x4) fires.
 */
static uint32_t k230_uart_rx_trigger_level(const K230UartState *s)
{
    if (!FIELD_EX32(s->fcr, FCR, FIFOE)) {
        return 1;  /* non-FIFO mode: 1 byte */
    }
    switch (FIELD_EX32(s->fcr, FCR, RT)) {
    case 0:  return 1;
    case 1:  return K230_UART_FIFO_DEPTH / 4;
    case 2:  return K230_UART_FIFO_DEPTH / 2;
    case 3:  return K230_UART_FIFO_DEPTH - 2;
    default: return 1;
    }
}

static void k230_uart_update_irq(K230UartState *s)
{
    bool irq = false;
    uint32_t rx_itl = k230_uart_rx_trigger_level(s);

    /* RX trigger level > 1 */
    if (FIELD_EX32(s->ier, IER, ERBFI) &&
        (s->rx_count >= rx_itl || s->timeout_ipending)) {
        irq = true;
    }
    /* Transmit Holding Register Empty Interrupt */
    if (FIELD_EX32(s->ier, IER, ETBEI) && s->thr_ipending) {
        irq = true;
    }
    /* Receiver Line Status Interrupt */
    if (FIELD_EX32(s->ier, IER, ELSI) &&
        (FIELD_EX32(s->lsr, LSR, OE) || FIELD_EX32(s->lsr, LSR, PE) ||
         FIELD_EX32(s->lsr, LSR, FE) || FIELD_EX32(s->lsr, LSR, BI))) {
        irq = true;
    }
    /* Busy detect interrupt (DW-specific, no IER gate). */
    if (s->busy_ipending) {
        irq = true;
    }
    qemu_set_irq(s->irq, irq ? 1 : 0);
}

/* ---- FIFO reset ---- */

static void k230_uart_fifo_reset(K230UartState *s, bool rx, bool tx)
{
    if (rx) {
        s->rx_head = s->rx_tail = s->rx_count = 0;
        s->lsr = FIELD_DP32(s->lsr, LSR, DR, 0);
        s->timeout_ipending = 0;
        timer_del(&s->rx_timeout);
    }
    if (tx) {
        s->tx_head = s->tx_tail = s->tx_count = 0;
        s->lsr = FIELD_DP32(s->lsr, LSR, THRE, 1);
        s->lsr = FIELD_DP32(s->lsr, LSR, TEMT, 1);
        s->thr_ipending = 1;
    }
    k230_uart_update_all(s);
}

/* ---- transmit ---- */

static gboolean k230_uart_chr_can_write(void *do_not_use, GIOCondition cond,
                                        void *opaque)
{
    K230UartState *s = K230_UART(opaque);
    if (!s->htx) {
        k230_uart_xmit(s);
    }
    return G_SOURCE_REMOVE;
}

/* ---- baud / char-transmit time ---- */

/*
 * Recompute the per-character transmit time from the current divisor latch:
 *
 *   baud    = sclk / (16 * divisor)
 *   char_ns = 10 * 16 * divisor * 1e9 / sclk     (10 = 8N1 start+data+stop)
 *
 * Drives the RX timeout interrupt (IID=0xc), which fires after 4 char times.
 * divisor == 0 means "unprogrammed": keep the previous (reset default) value.
 */
static void k230_uart_update_char_time(K230UartState *s)
{
    uint32_t divisor = ((uint32_t)s->dlh << 8) | s->dll;
    if (divisor == 0) {
        return;
    }
    s->char_transmit_time = muldiv64(10ull * 16 * divisor,
                                     NANOSECONDS_PER_SECOND,
                                     K230_UART_SCLK_HZ);
}

/* Send all characters in THR or TX-FIFO into char backend */
static void k230_uart_xmit(K230UartState *s)
{
    int ret;

    if (s->tx_count == 0 || s->htx) {
        return;
    }

    while (s->tx_count > 0) {
        uint8_t ch = (uint8_t)s->tx_fifo[s->tx_tail];

        if (FIELD_EX32(s->mcr, MCR, LOOPBACK)) {
            /* Go to RX fifo */
            k230_uart_push_rx_byte(s, ch);
            ret = 1;
        } else {
            /* Go to char backend */
            ret = qemu_chr_fe_write(&s->chr, &ch, 1);
        }
        if (ret < 0) {
            /* Wait for backend to be accessible then call the callback */
            qemu_chr_fe_add_watch(&s->chr, G_IO_OUT,
                                  k230_uart_chr_can_write, s);
            break;
        }

        s->tx_tail = (s->tx_tail + 1) % K230_UART_FIFO_DEPTH;
        s->tx_count--;
    }

    s->lsr = FIELD_DP32(s->lsr, LSR, TEMT, (s->tx_count == 0) ? 1 : 0);
    k230_uart_update_all(s);
}

/* ---- RX timeout ---- */

/* Character timeout callback */
static void k230_uart_rx_timeout(void *opaque)
{
    K230UartState *s = K230_UART(opaque);
    if (s->rx_count > 0) {
        s->timeout_ipending = 1;
        k230_uart_update_all(s);
    }
}

/* ---- receive ---- */

/* Push a byte to rxfifo(when FIFO_EN is set) or RBR(FIFO_EN not set). */
static void k230_uart_push_rx_byte(K230UartState *s, uint8_t ch)
{
    bool fifo_en = FIELD_EX32(s->fcr, FCR, FIFOE);
    uint16_t capacity = fifo_en ? K230_UART_FIFO_DEPTH : 1;

    if (s->rx_count < capacity) {
        s->rx_fifo[s->rx_head] = ch;
        s->rx_head = (s->rx_head + 1) % K230_UART_FIFO_DEPTH;
        s->rx_count++;
        s->lsr = FIELD_DP32(s->lsr, LSR, DR, 1);
        timer_mod(&s->rx_timeout,
                  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)
                  + 4 * s->char_transmit_time);
    } else {
        s->lsr = FIELD_DP32(s->lsr, LSR, OE, 1);
    }
    k230_uart_update_all(s);
}

/* ---- derived register recomputation ---- */

static void k230_uart_update_thre(K230UartState *s)
{
    bool should_set = (s->tx_count == 0);
#if K230_UART_THRE_MODE
    if (FIELD_EX32(s->ier, IER, PTIME)
        && FIELD_EX32(s->fcr, FCR, FIFOE)) {
        uint8_t tet = FIELD_EX32(s->fcr, FCR, TET);
        uint16_t threshold;
        switch (tet) {
        case 0x0:
            threshold = 0;
            break;
        case 0x1:
            threshold = 2;
            break;
        case 0x2:
            threshold = K230_UART_FIFO_DEPTH / 4;
            break;
        case 0x3:
            threshold = K230_UART_FIFO_DEPTH / 2;
            break;
        default:
            threshold = 0;
            break;
        }
        should_set = (s->tx_count <= threshold);
    }
#endif

    uint8_t old_thre = FIELD_EX32(s->lsr, LSR, THRE);
    s->lsr = FIELD_DP32(s->lsr, LSR, THRE, should_set ? 1 : 0);

    /* Edge trigger */
    if (should_set && !old_thre) {
        s->thr_ipending = 1;
    }
}

static bool k230_uart_is_busy(const K230UartState *s)
{
    return !FIELD_EX32(s->lsr, LSR, TEMT) || FIELD_EX32(s->lsr, LSR, DR);
}

static void k230_uart_update_usr(K230UartState *s)
{
    bool fifo_en = FIELD_EX32(s->fcr, FCR, FIFOE);
    uint16_t capacity = fifo_en ? K230_UART_FIFO_DEPTH : 1;

    s->usr = 0;
#if !K230_UART_16550_COMPATIBLE
    if (k230_uart_is_busy(s)) {
        s->usr = FIELD_DP32(s->usr, USR, BUSY, 1);
    }
#endif
#if K230_UART_FIFO_STAT && K230_UART_FIFO_MODE
    s->usr = FIELD_DP32(s->usr, USR, TFNF, s->tx_count < capacity ? 1 : 0);
    s->usr = FIELD_DP32(s->usr, USR, TFE, s->tx_count == 0 ? 1 : 0);
    s->usr = FIELD_DP32(s->usr, USR, RFNE, s->rx_count != 0 ? 1 : 0);
    s->usr = FIELD_DP32(s->usr, USR, RFF, s->rx_count == capacity ? 1 : 0);
#endif
}

static void k230_uart_update_iir(K230UartState *s)
{
    s->iir = 0;
    /* Indicate whether FIFO is enabled or not */
    if (FIELD_EX32(s->fcr, FCR, FIFOE)) {
        s->iir = FIELD_DP32(s->iir, IIR, FIFOSE, 0x3);
    }

    if (FIELD_EX32(s->ier, IER, ELSI) &&
        (FIELD_EX32(s->lsr, LSR, OE) || FIELD_EX32(s->lsr, LSR, PE) ||
         FIELD_EX32(s->lsr, LSR, FE) || FIELD_EX32(s->lsr, LSR, BI))) {
        /* Receiver Line Status Interrupt */
        s->iir = FIELD_DP32(s->iir, IIR, IID, 0x6);
    } else if (FIELD_EX32(s->ier, IER, ERBFI) &&
               (s->rx_count >= k230_uart_rx_trigger_level(s))) {
        /* Receiver Data Available Interrupt */
        s->iir = FIELD_DP32(s->iir, IIR, IID, 0x4);
    } else if (FIELD_EX32(s->ier, IER, ERBFI) && s->timeout_ipending) {
        /* Character timeout interrupt */
        s->iir = FIELD_DP32(s->iir, IIR, IID, 0xc);
    } else if (FIELD_EX32(s->ier, IER, ETBEI) && s->thr_ipending) {
        /* THR Empty Interrupt */
        s->iir = FIELD_DP32(s->iir, IIR, IID, 0x2);
    } else if (s->busy_ipending) {
        /* Busy detect: LCR written while USR.BUSY=1 (DW-specific) */
        s->iir = FIELD_DP32(s->iir, IIR, IID, 0x7);
    } else {
        s->iir = FIELD_DP32(s->iir, IIR, IID, 0x1);
    }
}


static void k230_uart_update_msr(K230UartState *s)
{
    uint8_t old_status = s->msr & 0xf0;
    uint8_t new_status;

    if (FIELD_EX32(s->mcr, MCR, LOOPBACK)) {
        /*
         * Standard 16550 loopback mapping:
         * DTR -> DSR, RTS -> CTS, OUT1 -> RI, OUT2 -> DCD
         */
        new_status = 0;
        if (FIELD_EX32(s->mcr, MCR, RTS)) {
            new_status |= 0x10; /* CTS */
        }
        if (FIELD_EX32(s->mcr, MCR, DTR)) {
            new_status |= 0x20; /* DSR */
        }
        if (FIELD_EX32(s->mcr, MCR, OUT1)) {
            new_status |= 0x40; /* RI  */
        }
        if (FIELD_EX32(s->mcr, MCR, OUT2)) {
            new_status |= 0x80; /* DCD */
        }
    } else {
        /* No real modem: report CTS/DSR/DCD as active (ready) */
        new_status = 0xb0; /* CTS | DSR | DCD */
    }

    /* Accumulate delta bits on status changes */
    uint8_t deltas = s->msr & 0x0f;
    uint8_t changes = old_status ^ new_status;
    if (changes & 0x10) {
        deltas |= 0x01; /* DCTS */
    }
    if (changes & 0x20) {
        deltas |= 0x02; /* DDSR */
    }
    if ((old_status & 0x40) && !(new_status & 0x40)) {
        deltas |= 0x04; /* TERI */
    }
    if (changes & 0x80) {
        deltas |= 0x08; /* DDCD */
    }

    s->msr = new_status | deltas;
}

static void k230_uart_update_all(K230UartState *s)
{
    k230_uart_update_thre(s);
    k230_uart_update_usr(s);
    k230_uart_update_iir(s);
    k230_uart_update_irq(s);
}

/* ---- MMIO read ---- */

static uint64_t k230_uart_read_rbr(K230UartState *s)
{
    uint64_t ret = 0;

    if (s->rx_count > 0) {
        ret = s->rx_fifo[s->rx_tail];
        s->rx_tail = (s->rx_tail + 1) % K230_UART_FIFO_DEPTH;
        s->rx_count--;
        s->timeout_ipending = 0;
        if (s->rx_count == 0) {
            s->lsr = FIELD_DP32(s->lsr, LSR, DR, 0);
            timer_del(&s->rx_timeout);
        } else {
            timer_mod(&s->rx_timeout,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)
                      + 4 * s->char_transmit_time);
        }
        qemu_chr_fe_accept_input(&s->chr);
    }

    if (!FIELD_EX32(s->ier, IER, ELCOLR)) {
        s->lsr = FIELD_DP32(s->lsr, LSR, OE, 0);
        s->lsr = FIELD_DP32(s->lsr, LSR, PE, 0);
        s->lsr = FIELD_DP32(s->lsr, LSR, FE, 0);
        s->lsr = FIELD_DP32(s->lsr, LSR, BI, 0);
    }

    k230_uart_update_all(s);
    return ret;
}

static uint64_t k230_uart_read(void *opaque, hwaddr addr, unsigned int size)
{
    K230UartState *s = K230_UART(opaque);
    uint64_t ret = 0;

    switch (addr >> 2) {
    case R_RBR_DLL_THR:
        if (FIELD_EX32(s->lcr, LCR, DLAB)) {
            /* DLL accessible only when not busy. */
            ret = (K230_UART_16550_COMPATIBLE || !k230_uart_is_busy(s))
                  ? s->dll : 0;
        } else {
            ret = k230_uart_read_rbr(s);
        }
        break;
    case R_IER_DLH:
        if (FIELD_EX32(s->lcr, LCR, DLAB)) {
            /* DLH accessible only when not busy. */
            ret = (K230_UART_16550_COMPATIBLE || !k230_uart_is_busy(s))
                  ? s->dlh : 0;
        } else {
            /* IER */
            ret = s->ier;
        }
        break;
    case R_IIR:
        ret = s->iir;
        if (FIELD_EX32(s->iir, IIR, IID) == 0x2) {
            s->thr_ipending = 0;
            k230_uart_update_all(s);
        }
        break;
    case R_LCR:
        ret = s->lcr;
        break;
    case R_LSR:
        ret = s->lsr;
        /* Clear OE PE FE and Break Interrupt status bits */
        s->lsr = FIELD_DP32(s->lsr, LSR, OE, 0);
        s->lsr = FIELD_DP32(s->lsr, LSR, PE, 0);
        s->lsr = FIELD_DP32(s->lsr, LSR, FE, 0);
        s->lsr = FIELD_DP32(s->lsr, LSR, BI, 0);
        k230_uart_update_all(s);
        break;
    case R_MCR:
        ret = s->mcr;
        break;
    case R_MSR:
        ret = s->msr;
        s->msr &= 0xf0; /* clear delta bits on read */
        break;
    case R_SCR:
        ret = s->scr;
        break;

    /* Shadow registers: aliases of standard registers. */
    case R_SRTS:
        ret = FIELD_EX32(s->mcr, MCR, RTS);
        break;
    case R_SBCR:
        ret = FIELD_EX32(s->lcr, LCR, BC);
        break;
    case R_SDMAM:
        ret = FIELD_EX32(s->fcr, FCR, DMAM);
        break;
    case R_SFE:
        ret = FIELD_EX32(s->fcr, FCR, FIFOE);
        break;
    case R_SRT:
        ret = FIELD_EX32(s->fcr, FCR, RT);
        break;
    case R_STET:
        ret = FIELD_EX32(s->fcr, FCR, TET);
        break;
    case R_HTX:
        ret = s->htx;
        break;

    case R_CPR:
        ret = (K230_UART_APB_DATA_WIDTH          << 0)   |
                   (K230_UART_AFCE_MODE               << 4)   |
                   (K230_UART_THRE_MODE                << 5)   |
                   (K230_UART_SIR_MODE                 << 6)   |
                   (K230_UART_SIR_LP_MODE              << 7)   |
                   (K230_UART_ADDITIONAL_FEATURES      << 8)   |
                   (K230_UART_FIFO_ACCESS              << 9)   |
                   (K230_UART_FIFO_STAT                << 10)  |
                   (K230_UART_SHADOW                   << 11)  |
                   (K230_UART_ADD_ENCODED_PARAMS       << 12)  |
                   (K230_UART_DMA_EXTRA                << 13)  |
                   (K230_UART_FIFO_MODE                << 16);
        break;
    case R_UCV:
        /* Version */
        ret = 0x342e3061;
        break;
    case R_DLF:
        ret = 0;
        break;
    case R_USR:
        ret = s->usr;
        /* Reading USR clears the busy-detect interrupt. */
        if (s->busy_ipending) {
            s->busy_ipending = 0;
            k230_uart_update_all(s);
        }
        break;
    case R_TFL:
        /* Transmit FIFO Level: number of bytes in TX FIFO. */
        ret = s->tx_count;
        break;
    case R_RFL:
        /* Receive FIFO Level: number of bytes in RX FIFO. */
        ret = s->rx_count;
        break;
    case R_CTR:
        /* Component Type Register: fixed ID. */
        ret = K230_UART_CTR_VALUE;
        break;
    default:
        ret = 0;
        break;
    }
    return ret;
}

/* ---- MMIO write ---- */

static void k230_uart_write(void *opaque, hwaddr addr,
                                uint64_t val64, unsigned int size)
{
    K230UartState *s = K230_UART(opaque);
    uint32_t val = (uint32_t)val64;
    uint32_t offset = addr >> 2;

    switch (offset) {
    case R_RBR_DLL_THR:
        if (FIELD_EX32(s->lcr, LCR, DLAB)) {
            /* DLL writable only when not busy. */
            if (K230_UART_16550_COMPATIBLE || !k230_uart_is_busy(s)) {
                s->dll = val & 0xff;
                k230_uart_update_char_time(s);
            }
        } else {
            bool fifo_en = FIELD_EX32(s->fcr, FCR, FIFOE);
            uint16_t capacity = fifo_en ? K230_UART_FIFO_DEPTH : 1;
            if (s->tx_count < capacity) {
                s->tx_fifo[s->tx_head] = val & 0xff;
                s->tx_head = (s->tx_head + 1) % K230_UART_FIFO_DEPTH;
                s->tx_count++;
            } else if (!fifo_en) {
                s->tx_fifo[s->tx_tail] = val & 0xff;
            }
            s->lsr = FIELD_DP32(s->lsr, LSR, THRE, 0);
            s->thr_ipending = 0;
            s->lsr = FIELD_DP32(s->lsr, LSR, TEMT, 0);
            k230_uart_xmit(s);
        }
        break;
    case R_IER_DLH:
        if (FIELD_EX32(s->lcr, LCR, DLAB)) {
            /* DLH writable only when not busy. */
            if (K230_UART_16550_COMPATIBLE || !k230_uart_is_busy(s)) {
                s->dlh = val & 0xff;
                k230_uart_update_char_time(s);
            }
        } else {
            uint8_t old_etbei = FIELD_EX32(s->ier, IER, ETBEI);
            s->ier = FIELD_DP32(s->ier, IER, ERBFI,
                                FIELD_EX32(val, IER, ERBFI));
            s->ier = FIELD_DP32(s->ier, IER, ETBEI,
                                FIELD_EX32(val, IER, ETBEI));
            s->ier = FIELD_DP32(s->ier, IER, ELSI,
                                FIELD_EX32(val, IER, ELSI));
            s->ier = FIELD_DP32(s->ier, IER, EDSSI,
                                FIELD_EX32(val, IER, EDSSI));
            s->ier = FIELD_DP32(s->ier, IER, ELCOLR,
                                FIELD_EX32(val, IER, ELCOLR));
    #if K230_UART_THRE_MODE
            s->ier = FIELD_DP32(s->ier, IER, PTIME,
                                FIELD_EX32(val, IER, PTIME));
    #endif
            if (!old_etbei && FIELD_EX32(s->ier, IER, ETBEI)
                && FIELD_EX32(s->lsr, LSR, THRE)) {
                s->thr_ipending = 1;
            }
        }
        break;
    case R_FCR:
        if ((val ^ s->fcr) & R_FCR_FIFOE_MASK) {
            val |= R_FCR_RFIFOR_MASK | R_FCR_XFIFOR_MASK;
        }
        s->fcr = FIELD_DP32(s->fcr, FCR, FIFOE,
                            FIELD_EX32(val, FCR, FIFOE));
        s->fcr = FIELD_DP32(s->fcr, FCR, DMAM,
                            FIELD_EX32(val, FCR, DMAM));
        s->fcr = FIELD_DP32(s->fcr, FCR, RT,
                            FIELD_EX32(val, FCR, RT));
    #if K230_UART_THRE_MODE
        s->fcr = FIELD_DP32(s->fcr, FCR, TET,
                            FIELD_EX32(val, FCR, TET));
    #endif
        if (FIELD_EX32(val, FCR, RFIFOR)) {
            k230_uart_fifo_reset(s, true, false);
        }
        if (FIELD_EX32(val, FCR, XFIFOR)) {
            k230_uart_fifo_reset(s, false, true);
        }
        break;
    case R_LCR:
        /*
         * When UART_16550_COMPATIBLE == NO (the K230 case),
         * LCR is writable only when USR.BUSY == 0.  A write while busy is
         * ignored and triggers the busy-detect interrupt (IID=0x7), which is
         * cleared by reading USR.  The 8250_dw driver detects this via
         * dw8250_check_lcr() and retries after draining the FIFOs.
         */
        if (!K230_UART_16550_COMPATIBLE && k230_uart_is_busy(s)) {
            s->busy_ipending = 1;
        } else {
            s->lcr = val & 0xff;
        }
        break;
    case R_MCR:
        /*
         * K230 advertises AFCE_MODE=0 and SIR_MODE=0 in CPR, so MCR[5:6]
         * (AFCE/SIRE) are read-only 0.  MCR[7] is reserved and also 0.
         */
        s->mcr = val & 0x1f;
        k230_uart_update_msr(s);
        break;
    case R_SCR:
        s->scr = val & 0xff;
        break;

    /*
     * Shadow registers: aliases of standard register fields.
     * They let software update a single bit without read-modify-write on the
     * original register.  SBCR shadows LCR[6] (BC), which is gated by the
     * busy-detect logic just like a direct LCR write.
     */
    case R_SRTS:
        s->mcr = FIELD_DP32(s->mcr, MCR, RTS, val & 0x1);
        k230_uart_update_msr(s);
        break;
    case R_SBCR:
        if (!K230_UART_16550_COMPATIBLE && k230_uart_is_busy(s)) {
            s->busy_ipending = 1;
        } else {
            s->lcr = FIELD_DP32(s->lcr, LCR, BC, val & 0x1);
        }
        break;
    case R_SDMAM:
        s->fcr = FIELD_DP32(s->fcr, FCR, DMAM, val & 0x1);
        break;
    case R_SFE:
        /* Shadow of FCR[0]; changing FIFOE resets both FIFOs */
        if ((s->fcr ^ val) & R_FCR_FIFOE_MASK) {
            k230_uart_fifo_reset(s, true, true);
        }
        s->fcr = FIELD_DP32(s->fcr, FCR, FIFOE, val & 0x1);
        break;
    case R_SRT:
        s->fcr = FIELD_DP32(s->fcr, FCR, RT, val & 0x3);
        break;
    case R_STET:
    #if K230_UART_THRE_MODE
        s->fcr = FIELD_DP32(s->fcr, FCR, TET, val & 0x3);
    #endif
        break;
    case R_HTX:
        s->htx = val & 0x1;
        /* Clearing HTX resumes transmission of any buffered TX data. */
        if (!s->htx) {
            k230_uart_xmit(s);
        }
        break;

    case R_SRR:
        if (FIELD_EX32(val, SRR, UR)) {
            device_cold_reset(DEVICE(s));
            return;
        }
        if (FIELD_EX32(val, SRR, RFR)) {
            k230_uart_fifo_reset(s, true, false);
        }
        if (FIELD_EX32(val, SRR, XFR)) {
            k230_uart_fifo_reset(s, false, true);
        }
        break;

    default:
        break;
    }
    k230_uart_update_all(s);
}

/* ---- device lifecycle ---- */

static const MemoryRegionOps k230_uart_ops = {
    .read = k230_uart_read,
    .write = k230_uart_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static void k230_uart_init(Object *obj)
{
    K230UartState *s = K230_UART(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, OBJECT(s), &k230_uart_ops, s,
                          TYPE_K230_UART, 0x100);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
}

static int k230_uart_can_receive(void *opaque)
{
    K230UartState *s = K230_UART(opaque);
    return K230_UART_FIFO_DEPTH - s->rx_count;
}

static void k230_uart_receive(void *opaque, const uint8_t *buf, int size)
{
    K230UartState *s = K230_UART(opaque);
    for (int i = 0; i < size; i++) {
        k230_uart_push_rx_byte(s, buf[i]);
    }
}

static void k230_uart_event(void *opaque, QEMUChrEvent event)
{
    K230UartState *s = K230_UART(opaque);

    if (event == CHR_EVENT_BREAK) {
        k230_uart_push_rx_byte(s, 0);
        s->lsr = FIELD_DP32(s->lsr, LSR, BI, 1);
        s->lsr = FIELD_DP32(s->lsr, LSR, FE, 1);
        k230_uart_update_all(s);
    }
}

static void k230_uart_reset_hold(Object *obj, ResetType type)
{
    K230UartState *s = K230_UART(obj);
    s->lcr = 0;
    s->fcr = 0;
    s->ier = 0;
    s->mcr = 0;
    s->scr = 0;
    s->dll = 0;
    s->dlh = 0;
    s->lsr = 0x60;
    s->msr = 0xb0;

    s->rx_head = s->rx_tail = s->rx_count = 0;
    s->tx_head = s->tx_tail = s->tx_count = 0;
    s->htx = 0;

    qemu_set_irq(s->irq, 0);

    s->thr_ipending = 0;
    s->timeout_ipending = 0;
    s->busy_ipending = 0;
    timer_del(&s->rx_timeout);
    /* Default to 115200 baud until the driver programs the divisor latch. */
    s->char_transmit_time = (NANOSECONDS_PER_SECOND / 115200) * 10;

    k230_uart_update_all(s);
}

static void k230_uart_realize(DeviceState *dev, Error **errp)
{
    K230UartState *s = K230_UART(dev);
    qemu_chr_fe_set_handlers(&s->chr, k230_uart_can_receive,
                             k230_uart_receive, k230_uart_event,
                             NULL, s, NULL, true);

    timer_init_ns(&s->rx_timeout, QEMU_CLOCK_VIRTUAL,
                  k230_uart_rx_timeout, s);
}

static int k230_uart_post_load(void *opaque, int version_id)
{
    K230UartState *s = K230_UART(opaque);

    /*
     * iir and usr are derived from the saved state; recompute them after
     * migration so the device is consistent.
     */
    k230_uart_update_all(s);
    return 0;
}

static const VMStateDescription vmstate_k230_uart = {
    .name = "k230.uart",
    .version_id = 2,
    .minimum_version_id = 1,
    .post_load = k230_uart_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(lcr,   K230UartState),
        VMSTATE_UINT8(fcr,   K230UartState),
        VMSTATE_UINT8(ier,   K230UartState),
        VMSTATE_UINT8(dll,   K230UartState),
        VMSTATE_UINT8(dlh,   K230UartState),
        VMSTATE_UINT8(mcr,   K230UartState),
        VMSTATE_UINT8(lsr,   K230UartState),
        VMSTATE_UINT8(msr,   K230UartState),
        VMSTATE_UINT8(scr,   K230UartState),
        VMSTATE_UINT8(htx,   K230UartState),
        VMSTATE_UINT16_ARRAY(rx_fifo, K230UartState,
                             K230_UART_FIFO_DEPTH),
        VMSTATE_UINT16_ARRAY(tx_fifo, K230UartState,
                             K230_UART_FIFO_DEPTH),
        VMSTATE_UINT32(rx_head, K230UartState),
        VMSTATE_UINT32(rx_tail, K230UartState),
        VMSTATE_UINT32(rx_count, K230UartState),
        VMSTATE_UINT32(tx_head, K230UartState),
        VMSTATE_UINT32(tx_tail, K230UartState),
        VMSTATE_UINT32(tx_count, K230UartState),
        VMSTATE_UINT8(thr_ipending, K230UartState),
        VMSTATE_UINT8(timeout_ipending, K230UartState),
        VMSTATE_UINT8(busy_ipending, K230UartState),
        VMSTATE_UINT64(char_transmit_time, K230UartState),
        VMSTATE_END_OF_LIST()
    }
};

static const Property k230_uart_properties[] = {
    DEFINE_PROP_CHR("chardev", K230UartState, chr),
};

static void k230_uart_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = k230_uart_realize;
    rc->phases.hold = k230_uart_reset_hold;
    dc->vmsd = &vmstate_k230_uart;
    dc->desc = "K230 UART (16550-compatible)";
    device_class_set_props(dc, k230_uart_properties);
}

static const TypeInfo k230_uart_info = {
    .name = TYPE_K230_UART,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(K230UartState),
    .instance_init = k230_uart_init,
    .class_init = k230_uart_class_init
};

static void k230_uart_register_types(void)
{
    type_register_static(&k230_uart_info);
}

type_init(k230_uart_register_types)
