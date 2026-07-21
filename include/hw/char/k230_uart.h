/*
 * K230 UART device
 *
 * K230 Technical Reference Manual V0.3.1 (2024-11-18):
 * https://github.com/revyos/external-docs/blob/master/K230/en-us/K230_Technical_Reference_Manual_V0.3.1_20241118.pdf
 *
 * Register semantics cross-checked against the SDK Linux driver
 * src/little/linux/drivers/tty/serial/8250/8250_dw.c in
 * https://github.com/kendryte/k230_sdk (compatible "snps,dw-apb-uart").
 *
 * Copyright (c) 2026 zhenbaii <1640586082@qq.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_K230_UART_H
#define HW_K230_UART_H

#include "hw/core/sysbus.h"
#include "chardev/char-fe.h"
#include "hw/core/registerfields.h"
#include "qom/object.h"

REG32(RBR_DLL_THR, 0x00)
REG32(IER_DLH, 0x04)
REG32(IER, 0x04)
    FIELD(IER, ERBFI, 0, 1)
    FIELD(IER, ETBEI, 1, 1)
    FIELD(IER, ELSI, 2, 1)
    FIELD(IER, EDSSI, 3, 1)
    FIELD(IER, ELCOLR, 4, 1)
    FIELD(IER, PTIME, 7, 1)
REG32(FCR, 0x08)
    FIELD(FCR, FIFOE, 0, 1)
    FIELD(FCR, RFIFOR, 1, 1)
    FIELD(FCR, XFIFOR, 2, 1)
    FIELD(FCR, DMAM, 3, 1)
    FIELD(FCR, TET, 4, 2)
    FIELD(FCR, RT, 6, 2)
REG32(IIR, 0x08)
    FIELD(IIR, IID, 0, 4)
    FIELD(IIR, FIFOSE, 6, 2)
REG32(LCR, 0x0c)
    FIELD(LCR, DLS, 0, 2)
    FIELD(LCR, STOP, 2, 1)
    FIELD(LCR, PEN, 3, 1)
    FIELD(LCR, EPS, 4, 1)
    FIELD(LCR, SP, 5, 1)
    FIELD(LCR, BC, 6, 1)
    FIELD(LCR, DLAB, 7, 1)
REG32(MCR, 0x10)
    FIELD(MCR, DTR, 0, 1)
    FIELD(MCR, RTS, 1, 1)
    FIELD(MCR, OUT1, 2, 1)
    FIELD(MCR, OUT2, 3, 1)
    FIELD(MCR, LOOPBACK, 4, 1)
    FIELD(MCR, AFCE, 5, 1)
    FIELD(MCR, SIRE, 6, 1)
REG32(LSR, 0x14)
    FIELD(LSR, DR, 0, 1)
    FIELD(LSR, OE, 1, 1)
    FIELD(LSR, PE, 2, 1)
    FIELD(LSR, FE, 3, 1)
    FIELD(LSR, BI, 4, 1)
    FIELD(LSR, THRE, 5, 1)
    FIELD(LSR, TEMT, 6, 1)
    FIELD(LSR, RFE, 7, 1)
    FIELD(LSR, ADDR_RSVD, 8, 1)
REG32(MSR, 0x18)
    FIELD(MSR, DCTS, 0, 1)
    FIELD(MSR, DDSR, 1, 1)
    FIELD(MSR, TERI, 2, 1)
    FIELD(MSR, DDCD, 3, 1)
    FIELD(MSR, CTS, 4, 1)
    FIELD(MSR, DSR, 5, 1)
    FIELD(MSR, RI, 6, 1)
    FIELD(MSR, DCD, 7, 1)
REG32(RFW, 0x78)
    FIELD(RFW, RFWD, 0, 8)
    FIELD(RFW, RFPE, 8, 1)
    FIELD(RFW, RFFE, 9, 1)
REG32(USR, 0x7c)
    FIELD(USR, BUSY, 0, 1)
    FIELD(USR, TFNF, 1, 1)
    FIELD(USR, TFE, 2, 1)
    FIELD(USR, RFNE, 3, 1)
    FIELD(USR, RFF, 4, 1)
REG32(TFL, 0x80)
    FIELD(TFL, TFL, 0, 5)
REG32(RFL, 0x84)
    FIELD(RFL, RFL, 0, 5)
REG32(SRR, 0x88)
    FIELD(SRR, UR, 0, 1)
    FIELD(SRR, RFR, 1, 1)
    FIELD(SRR, XFR, 2, 1)
REG32(SRTS, 0x8c)
    FIELD(SRTS, SRTS, 0, 1)
REG32(SBCR, 0x90)
    FIELD(SBCR, SBCB, 0, 1)
REG32(SDMAM, 0x94)
    FIELD(SDMAM, SDMAM, 0, 1)
REG32(SFE, 0x98)
    FIELD(SFE, SFE, 0, 1)
REG32(SRT, 0x9c)
    FIELD(SRT, SRT, 0, 2)
REG32(STET, 0xa0)
    FIELD(STET, STET, 0, 2)
REG32(HTX, 0xa4)
    FIELD(HTX, HTX, 0, 1)
REG32(TCR, 0xac)
    FIELD(TCR, RS485_EN, 0, 1)
    FIELD(TCR, RE_POL, 1, 1)
    FIELD(TCR, DE_POL, 2, 1)
    FIELD(TCR, XFER_MODE, 3, 2)
REG32(SCR, 0x1c)
REG32(DLF, 0xc0)
REG32(CPR, 0xf4)
REG32(UCV, 0xf8)
REG32(CTR, 0xfc)

/* peripheral ID 0x44570110 ("DW\x01\x10").  Read-only. */
#define K230_UART_CTR_VALUE  0x44570110u
#define K230_UART_16550_COMPATIBLE    0
#define K230_UART_FIFO_DEPTH          32

/* CPR */
#define K230_UART_APB_DATA_WIDTH      2      /* CPR[1:0]  - 32-bit APB       */
#define K230_UART_AFCE_MODE           0      /* CPR[4]    - not implemented */
#define K230_UART_THRE_MODE           1      /* CPR[5]    - implemented     */
#define K230_UART_SIR_MODE            0      /* CPR[6]    - not implemented */
#define K230_UART_SIR_LP_MODE         0      /* CPR[7]    - not implemented */
#define K230_UART_ADDITIONAL_FEATURES 1      /* CPR[8]    - UCV/CTR present */
#define K230_UART_FIFO_ACCESS         0      /* CPR[9]    - not implemented */
#define K230_UART_FIFO_STAT           1      /* CPR[10]   - TFL/RFL present */
#define K230_UART_SHADOW              1      /* CPR[11]   - shadow regs     */
#define K230_UART_ADD_ENCODED_PARAMS  1      /* CPR[12]   - CPR present     */
#define K230_UART_DMA_EXTRA           0      /* CPR[13]   - not implemented */
#define K230_UART_FIFO_MODE           0x2    /* CPR[23:16] - 32-byte FIFO   */

#define TYPE_K230_UART "k230-uart"
OBJECT_DECLARE_SIMPLE_TYPE(K230UartState, K230_UART)
struct K230UartState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    /* Standard 16550 registers */
    uint8_t  dll;       /* Divisor Latch Low,               offset 0x00 */
    uint8_t  ier;       /* Interrupt Enable,                offset 0x04 */
    uint8_t  dlh;       /* Divisor Latch High,              offset 0x04 */
    uint8_t  fcr;       /* FIFO Control,                    offset 0x08 */
    uint8_t  iir;       /* Interrupt Identification,        offset 0x08 */
    uint8_t  lcr;       /* Line Control,                    offset 0x0c */
    uint8_t  mcr;       /* Modem Control,                   offset 0x10 */
    uint8_t  lsr;       /* Line Status,                     offset 0x14 */
    uint8_t  msr;       /* Modem Status,                    offset 0x18 */
    uint8_t  scr;       /* Scratchpad,                      offset 0x1c */

    /* DesignWare-specific registers */
    uint8_t  usr;       /* UART Status,                     offset 0x7c */
    uint8_t  htx;       /* Halt TX,                         offset 0xa4 */

    /* Internal interrupt state. */
    uint8_t  thr_ipending;       /* THR empty (IID=0x2) pending     */
    uint8_t  timeout_ipending;   /* RX FIFO timeout (IID=0xc)       */
    uint8_t  busy_ipending;      /* busy detect (IID=0x7) pending   */

    /* FIFO */
    uint16_t rx_fifo[K230_UART_FIFO_DEPTH];
    uint32_t rx_head, rx_tail, rx_count;
    uint16_t tx_fifo[K230_UART_FIFO_DEPTH];
    uint32_t tx_head, tx_tail, tx_count;

    uint64_t char_transmit_time;

    CharFrontend chr;
    qemu_irq    irq;
    QEMUTimer rx_timeout;
};

#endif
