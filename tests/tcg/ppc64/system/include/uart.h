#ifndef PPC64_UART_H
#define PPC64_UART_H

/*
 * Register definitions for our standard (16550 style) UART
 */
#define UART_REG_RX             0x00
#define UART_REG_TX             0x00
#define UART_REG_DLL            0x00
#define UART_REG_IER            0x04
#define   UART_REG_IER_RDI      0x01
#define   UART_REG_IER_THRI     0x02
#define   UART_REG_IER_RLSI     0x04
#define   UART_REG_IER_MSI      0x08
#define UART_REG_DLM            0x04
#define UART_REG_IIR            0x08
#define UART_REG_FCR            0x08
#define   UART_REG_FCR_EN_FIFO  0x01
#define   UART_REG_FCR_CLR_RCVR 0x02
#define   UART_REG_FCR_CLR_XMIT 0x04
#define   UART_REG_FCR_TRIG1    0x00
#define   UART_REG_FCR_TRIG4    0x40
#define   UART_REG_FCR_TRIG8    0x80
#define   UART_REG_FCR_TRIG14   0xc0
#define UART_REG_LCR            0x0c
#define   UART_REG_LCR_5BIT     0x00
#define   UART_REG_LCR_6BIT     0x01
#define   UART_REG_LCR_7BIT     0x02
#define   UART_REG_LCR_8BIT     0x03
#define   UART_REG_LCR_STOP     0x04
#define   UART_REG_LCR_PAR      0x08
#define   UART_REG_LCR_EVEN_PAR 0x10
#define   UART_REG_LCR_STIC_PAR 0x20
#define   UART_REG_LCR_BREAK    0x40
#define   UART_REG_LCR_DLAB     0x80
#define UART_REG_MCR            0x10
#define   UART_REG_MCR_DTR      0x01
#define   UART_REG_MCR_RTS      0x02
#define   UART_REG_MCR_OUT1     0x04
#define   UART_REG_MCR_OUT2     0x08
#define   UART_REG_MCR_LOOP     0x10
#define UART_REG_LSR            0x14
#define   UART_REG_LSR_DR       0x01
#define   UART_REG_LSR_OE       0x02
#define   UART_REG_LSR_PE       0x04
#define   UART_REG_LSR_FE       0x08
#define   UART_REG_LSR_BI       0x10
#define   UART_REG_LSR_THRE     0x20
#define   UART_REG_LSR_TEMT     0x40
#define   UART_REG_LSR_FIFOE    0x80
#define UART_REG_MSR            0x18
#define UART_REG_SCR            0x1c

#endif
