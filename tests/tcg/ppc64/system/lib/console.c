#include <stdint.h>
#include <stdbool.h>

#include "console.h"
#include "pnv.h"
#include "io.h"
#include "uart.h"

#define UART_BAUDS          115200
#define H_PUT_TERM_CHAR     88

/*
 * Core UART functions to implement for a port
 */

static uint64_t uart_base;

struct console_ops {
    int (*putchar)(int c);
} ops;


static unsigned long uart_divisor(unsigned long uart_freq, unsigned long bauds)
{
    return uart_freq / (bauds * 16);
}

static bool std_uart_rx_empty(void)
{
    return !(readb(uart_base + UART_REG_LSR) & UART_REG_LSR_DR);
}

static uint8_t std_uart_read(void)
{
    return readb(uart_base + UART_REG_RX);
}

static bool std_uart_tx_full(void)
{
    /* TODO: check UART LSR */
    return 0;
}

static void std_uart_write(uint8_t c)
{
    writeb(c, uart_base + UART_REG_TX);
}

static void std_uart_set_irq_en(bool rx_irq, bool tx_irq)
{
    uint8_t ier = 0;

    if (tx_irq) {
        ier |= UART_REG_IER_THRI;
    }
    if (rx_irq) {
        ier |= UART_REG_IER_RDI;
    }
    writeb(ier, uart_base + UART_REG_IER);
}

static void std_uart_init(uint64_t uart_freq)
{
    unsigned long div = uart_divisor(uart_freq, UART_BAUDS);

    writeb(UART_REG_LCR_DLAB,     uart_base + UART_REG_LCR);
    writeb(div & 0xff,            uart_base + UART_REG_DLL);
    writeb(div >> 8,              uart_base + UART_REG_DLM);
    writeb(UART_REG_LCR_8BIT,     uart_base + UART_REG_LCR);
    writeb(UART_REG_MCR_DTR |
           UART_REG_MCR_RTS,      uart_base + UART_REG_MCR);
    writeb(UART_REG_FCR_EN_FIFO |
           UART_REG_FCR_CLR_RCVR |
           UART_REG_FCR_CLR_XMIT, uart_base + UART_REG_FCR);
}

int getchar(void)
{
    while (std_uart_rx_empty()) {
        ;   /* Do nothing */
    }
    return std_uart_read();
}

int putchar(int c)
{
    return ops.putchar(c);
}

void __sys_outc(char c)
{
    putchar(c);
}

static int putchar_uart(int c)
{
    while (std_uart_tx_full()) {
        ;   /* Do Nothing */
    }
    std_uart_write(c);
    return c;
}

static int putchar_hvc(int c)
{
    register unsigned long hcall __asm__("r3") = H_PUT_TERM_CHAR;
    register unsigned long termno __asm__("r4") = 0;
    register unsigned long length __asm__("r5") = 1;
    register unsigned long str __asm__("r6") = __builtin_bswap64(c);

    __asm__ volatile ("sc 1" : : "r" (hcall), "r" (termno), "r" (length),
                      "r" (str) : );
    return c;
}

int puts(const char *str)
{
    unsigned int i;

    for (i = 0; *str; i++) {
        char c = *(str++);
        if (c == 10) {
            putchar(13);
        }
        putchar(c);
    }
    return 0;
}

size_t strlen(const char *s)
{
    size_t len = 0;

    while (*s++) {
        len++;
    }

    return len;
}

struct console_ops pseries_console = {
    .putchar = putchar_hvc,
};

struct console_ops pnv_console = {
    .putchar = putchar_uart,
};

void uart_init(void)
{
    uint64_t proc_freq; /* TODO */

    proc_freq = 0; /* TODO */

    uart_base = UART_BASE

    std_uart_init(proc_freq);
}

void console_init(void)
{
    if (is_pnv()) {
        ops = pnv_console;
        uart_init();
    } else {
        ops = pseries_console;
    }
}

void console_set_irq_en(bool rx_irq, bool tx_irq)
{
    std_uart_set_irq_en(rx_irq, tx_irq);
}
