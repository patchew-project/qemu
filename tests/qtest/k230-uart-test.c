/*
 * QTest for the K230 UART — functional-path coverage.
 *
 * Tests are organised around driver usage scenarios rather than
 * enumerating every register in isolation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include <string.h>

#define UART_BASE  0x91400000
#define R(off)     (UART_BASE + (off))

/* register offsets */
#define THR    0x00
#define IER    0x04
#define IIR    0x08
#define LCR    0x0c
#define MCR    0x10
#define LSR    0x14
#define SCR    0x1c
#define USR    0x7c
#define TFL    0x80
#define RFL    0x84
#define SRR    0x88
#define SRTS   0x8c
#define SBCR   0x90
#define SDMAM  0x94
#define SFE    0x98
#define SRT    0x9c
#define STET   0xa0
#define HTX    0xa4
#define CPR    0xf4
#define CTR    0xfc

/* bit fields */
#define LCR_DLAB     0x80
#define LCR_BC       0x40
#define LCR_8N1      0x03

#define LSR_DR       0x01
#define LSR_OE       0x02
#define LSR_THRE     0x20
#define LSR_TEMT     0x40
#define LSR_RESET    0x60

#define IIR_IID      0x0f
#define IIR_NONE     0x01
#define IIR_THR      0x02
#define IIR_RX       0x04
#define IIR_LINE     0x06
#define IIR_BUSY     0x07
#define IIR_TO       0x0c
#define IIR_FF       0xc0

#define IER_RX       0x01
#define IER_TX       0x02
#define IER_LS       0x04
#define IER_COLR     0x10
#define IER_PTIME    0x80

#define MCR_LB       0x10
#define MCR_RTS      0x02

#define FCR_FE       0x01
#define FCR_RR       0x02
#define FCR_XR       0x04
#define FCR_TET_H    (3 << 4)
#define FCR_RT_Q     (1 << 6)
#define FCR_RT_F     (3 << 6)

#define USR_BUSY     0x01
#define USR_RESET    0x06

#define SRR_UR       0x01
#define SRR_RFR      0x02

/* helpers */
static uint32_t rd(QTestState *qts, uint32_t o)
{
    return qtest_readl(qts, R(o));
}
static uint32_t iid(QTestState *qts)
{
    return rd(qts, IIR) & IIR_IID;
}

static void poll_lsr(QTestState *qts, uint32_t m)
{
    int i;

    for (i = 0; i < 1000; i++) {
        if (rd(qts, LSR) & m) {
            return;
        }
        g_usleep(1000);
    }
    g_assert_not_reached();
}

static void s1(QTestState *qts, int fd, char c)
{
    g_assert_cmpint(send(fd, &c, 1, 0), ==, 1);
    poll_lsr(qts, LSR_DR);
}

static void sn(QTestState *qts, int fd, const char *d, int n)
{
    g_assert_cmpint(send(fd, d, n, 0), ==, n);
    poll_lsr(qts, LSR_DR);
}

static void oe_nf(QTestState *qts, int fd)
{
    int i;

    s1(qts, fd, 'A');
    {
        char c = 'B';
        g_assert_cmpint(send(fd, &c, 1, 0), ==, 1);
    }
    for (i = 0; i < 200; i++) {
        rd(qts, SCR);
        g_usleep(1000);
    }
}

/* 1. device probe */
static void test_device_probe(void)
{
    QTestState *qts = qtest_init("-machine k230");
    uint32_t cpr = rd(qts, CPR);

    g_assert_cmphex(cpr & 0x3, ==, 0x2);
    g_assert_cmphex(cpr & (1 << 5), ==, (1 << 5));
    g_assert_cmphex(cpr & (1 << 8), ==, (1 << 8));
    g_assert_cmphex((cpr >> 16) & 0xff, ==, 0x2);
    g_assert_cmphex(cpr & (1 << 4), ==, 0);
    g_assert_cmphex(rd(qts, CTR), ==, 0x44570110);
    g_assert_cmphex(rd(qts, LSR), ==, LSR_RESET);
    g_assert_cmphex(rd(qts, USR), ==, USR_RESET);
    g_assert_cmphex(iid(qts), ==, IIR_NONE);
    g_assert_cmphex(rd(qts, IIR) & IIR_FF, ==, 0);
    g_assert_cmphex(rd(qts, USR) & USR_BUSY, ==, 0);

    qtest_writel(qts, R(IIR), FCR_FE);
    g_assert_cmphex(rd(qts, IIR) & IIR_FF, ==, IIR_FF);
    qtest_quit(qts);
}

/* 2. init & baud */
static void test_init_and_baud(void)
{
    int fd;
    QTestState *qts = qtest_init_with_serial("-machine k230", &fd);

    qtest_writel(qts, R(LCR), LCR_DLAB);
    qtest_writel(qts, R(THR), 0x55);
    g_assert_cmphex(rd(qts, THR), ==, 0x55);
    qtest_writel(qts, R(LCR), LCR_8N1);
    g_assert_cmphex(rd(qts, THR), ==, 0x00);

    qtest_writel(qts, R(IIR), FCR_FE | FCR_RT_F);
    qtest_writel(qts, R(IER), IER_RX);

    /* divisor=1 -> timeout=12800ns */
    qtest_writel(qts, R(LCR), LCR_8N1 | LCR_DLAB);
    qtest_writel(qts, R(THR), 1); qtest_writel(qts, R(IER), 0);
    qtest_writel(qts, R(LCR), LCR_8N1);
    s1(qts, fd, 'X');
    qtest_clock_step(qts, 13000);
    g_assert_cmphex(iid(qts), ==, IIR_TO);

    /* divisor=100 -> timeout=1.28e6ns; 13000ns too short */
    rd(qts, THR);
    qtest_writel(qts, R(LCR), LCR_8N1 | LCR_DLAB);
    qtest_writel(qts, R(THR), 100); qtest_writel(qts, R(IER), 0);
    qtest_writel(qts, R(LCR), LCR_8N1);
    s1(qts, fd, 'Y');
    qtest_clock_step(qts, 13000);
    g_assert_cmphex(iid(qts), !=, IIR_TO);
    qtest_clock_step(qts, 1300000);
    g_assert_cmphex(iid(qts), ==, IIR_TO);

    close(fd); qtest_quit(qts);
}

/* 3. TX / RX datapath */
static void test_tx_rx_datapath(void)
{
    int fd;
    QTestState *qts = qtest_init_with_serial("-machine k230", &fd);

    qtest_writel(qts, R(LCR), LCR_8N1);
    g_assert_cmphex(rd(qts, LSR) & (LSR_THRE | LSR_TEMT),
                    ==, LSR_THRE | LSR_TEMT);
    qtest_writel(qts, R(THR), 'A');
    g_assert_cmphex(rd(qts, LSR) & (LSR_THRE | LSR_TEMT),
                    ==, LSR_THRE | LSR_TEMT);

    /* external RX (FIFO mode) */
    qtest_writel(qts, R(IIR), FCR_FE);
    qtest_writel(qts, R(IER), IER_RX);
    sn(qts, fd, "K230", 4);
    for (int i = 0; i < 4; i++) {
        if (i < 3) {
            g_assert_cmphex(iid(qts), ==, IIR_RX);
        }
        g_assert_cmphex(rd(qts, THR), ==, "K230"[i]);
    }
    g_assert_cmphex(rd(qts, LSR) & LSR_DR, ==, 0);

    /* loopback */
    qtest_writel(qts, R(MCR), MCR_LB);
    qtest_writel(qts, R(THR), 'L');
    g_assert_cmphex(rd(qts, THR), ==, 'L');
    g_assert_cmphex(rd(qts, LSR) & LSR_DR, ==, 0);

    /* no loopback */
    qtest_writel(qts, R(MCR), 0);
    qtest_writel(qts, R(THR), 'Z');
    g_assert_cmphex(rd(qts, LSR) & LSR_DR, ==, 0);

    /* TFL / RFL */
    qtest_writel(qts, R(MCR), MCR_LB);
    qtest_writel(qts, R(IIR), FCR_FE | FCR_RR | FCR_XR);
    qtest_writel(qts, R(THR), 'X'); qtest_writel(qts, R(THR), 'Y');
    g_assert_cmphex(rd(qts, TFL), ==, 0);
    g_assert_cmphex(rd(qts, RFL), ==, 2);

    /* non-FIFO mode */
    qtest_writel(qts, R(MCR), 0);
    qtest_writel(qts, R(IIR), 0);
    s1(qts, fd, 'N');
    g_assert_cmphex(rd(qts, THR), ==, 'N');
    g_assert_cmphex(rd(qts, LSR) & LSR_DR, ==, 0);

    /* non-FIFO overrun */
    int i;
    s1(qts, fd, 'n');
    g_assert_cmphex(rd(qts, LSR) & LSR_DR, ==, LSR_DR);
    {
        char c = 'o';
        send(fd, &c, 1, 0);
    }
    for (i = 0; i < 200; i++) {
        rd(qts, SCR);
        g_usleep(1000);
    }
    g_assert_cmphex(rd(qts, LSR) & LSR_OE, ==, LSR_OE);

    close(fd); qtest_quit(qts);
}

/* 4. THRE interrupt */
static void test_thre_interrupt(void)
{
    QTestState *qts = qtest_init("-machine k230 "
                                 "-chardev null,id=c0 -serial chardev:c0");
    qtest_writel(qts, R(LCR), LCR_8N1);

    qtest_writel(qts, R(THR), 'A');
    g_assert_cmphex(iid(qts), ==, IIR_NONE);

    qtest_writel(qts, R(IER), IER_TX);
    g_assert_cmphex(iid(qts), ==, IIR_THR);

    qtest_writel(qts, R(THR), 'B');
    g_assert_cmphex(iid(qts), ==, IIR_THR);

    g_assert_cmphex(iid(qts), ==, IIR_NONE);
    qtest_quit(qts);
}

/* 5. RX interrupts: timeout & trigger level */
static void test_rx_interrupts(void)
{
    int fd;
    QTestState *qts = qtest_init_with_serial("-machine k230", &fd);

    qtest_writel(qts, R(LCR), LCR_8N1);
    qtest_writel(qts, R(IIR), FCR_FE | FCR_RT_F);
    qtest_writel(qts, R(IER), IER_RX);

    /* basic timeout */
    s1(qts, fd, 'T');
    qtest_clock_step(qts, 400000);
    g_assert_cmphex(iid(qts), ==, IIR_TO);
    g_assert_cmphex(rd(qts, THR), ==, 'T');
    g_assert_cmphex(iid(qts), !=, IIR_TO);

    /* timeout reset by new byte */
    s1(qts, fd, 'A'); qtest_clock_step(qts, 200000);
    s1(qts, fd, 'B');
    qtest_clock_step(qts, 200000);
    g_assert_cmphex(iid(qts), ==, IIR_NONE);
    qtest_clock_step(qts, 200000);
    g_assert_cmphex(iid(qts), ==, IIR_TO);
    rd(qts, THR);
    rd(qts, THR);

    /* timeout rearmed by partial drain */
    s1(qts, fd, 'X');
    s1(qts, fd, 'Y');
    qtest_clock_step(qts, 400000);
    g_assert_cmphex(iid(qts), ==, IIR_TO);
    g_assert_cmphex(rd(qts, THR), ==, 'X');
    qtest_clock_step(qts, 200000);
    g_assert_cmphex(iid(qts), !=, IIR_TO);
    qtest_clock_step(qts, 250000);
    g_assert_cmphex(iid(qts), ==, IIR_TO);
    g_assert_cmphex(rd(qts, THR), ==, 'Y');

    /* RX trigger level: RT=Q -> trigger at 8 bytes */
    qtest_writel(qts, R(IIR), FCR_FE | FCR_RT_Q);
    sn(qts, fd, "ABCDEFG", 7);
    g_usleep(20000);
    g_assert_cmphex(iid(qts), ==, IIR_NONE);
    qtest_clock_step(qts, 400000);
    g_assert_cmphex(iid(qts), ==, IIR_TO);
    for (int i = 0; i < 7; i++) {
        rd(qts, THR);
    }

    sn(qts, fd, "12345678", 8);
    for (int i = 0; i < 1000; i++) {
        if (iid(qts) == IIR_RX) {
            break;
        }
        g_usleep(1000);
    }
    g_assert_cmphex(iid(qts), ==, IIR_RX);

    close(fd); qtest_quit(qts);
}

/* 6. error interrupts & IIR priority */
static void test_error_interrupts(void)
{
    int fd;
    QTestState *qts = qtest_init_with_serial("-machine k230", &fd);

    qtest_writel(qts, R(LCR), LCR_8N1);
    qtest_writel(qts, R(IER), IER_LS);

    /* OE -> IIR=0x6; LSR clears; ELCOLR=0: RBR clears; ELCOLR=1: RBR keeps */
    oe_nf(qts, fd);
    g_assert_cmphex(iid(qts), ==, IIR_LINE);
    rd(qts, LSR);
    g_assert_cmphex(iid(qts), !=, IIR_LINE);

    oe_nf(qts, fd);
    g_assert_cmphex(iid(qts), ==, IIR_LINE);
    rd(qts, THR);
    g_assert_cmphex(iid(qts), !=, IIR_LINE);

    qtest_writel(qts, R(IER), IER_LS | IER_COLR);
    oe_nf(qts, fd);
    g_assert_cmphex(iid(qts), ==, IIR_LINE);
    rd(qts, THR);
    g_assert_cmphex(iid(qts), ==, IIR_LINE);
    rd(qts, LSR);
    g_assert_cmphex(iid(qts), !=, IIR_LINE);

    /* IIR priority: RX > TX (loopback) */
    qtest_writel(qts, R(IIR), FCR_FE);
    qtest_writel(qts, R(MCR), MCR_LB);
    qtest_writel(qts, R(IER), IER_TX | IER_RX);
    qtest_writel(qts, R(THR), 'P');
    g_assert_cmphex(iid(qts), ==, IIR_RX);
    g_assert_cmphex(rd(qts, THR), ==, 'P');
    g_assert_cmphex(iid(qts), ==, IIR_THR);
    iid(qts);
    g_assert_cmphex(iid(qts), ==, IIR_NONE);

    /* OE in FIFO mode via loopback */
    qtest_writel(qts, R(IER), IER_LS);
    for (int i = 0; i < 32; i++) {
        qtest_writel(qts, R(THR), 'a');
    }
    g_assert_cmphex(rd(qts, LSR) & LSR_OE, ==, 0);
    qtest_writel(qts, R(THR), 'z');
    g_assert_cmphex(iid(qts), ==, IIR_LINE);
    g_assert_cmphex(rd(qts, LSR) & LSR_OE, ==, LSR_OE);

    close(fd); qtest_quit(qts);
}

/* 7. USR & busy detect */
static void test_busy_detect(void)
{
    int fd;
    QTestState *qts = qtest_init_with_serial("-machine k230", &fd);

    qtest_writel(qts, R(LCR), LCR_8N1);
    qtest_writel(qts, R(IIR), FCR_FE);

    g_assert_cmphex(rd(qts, USR) & 0x1e, ==, 0x06);

    qtest_writel(qts, R(MCR), MCR_LB);
    qtest_writel(qts, R(THR), 'A');
    g_assert_cmphex(rd(qts, USR) & 0x08, ==, 0x08);
    rd(qts, THR);
    g_assert_cmphex(rd(qts, USR) & 0x08, ==, 0);
    qtest_writel(qts, R(MCR), 0);

    /* BUSY via RX & loopback */
    g_assert_cmphex(rd(qts, USR) & USR_BUSY, ==, 0);
    s1(qts, fd, 'Z');
    g_assert_cmphex(rd(qts, USR) & USR_BUSY, ==, USR_BUSY);
    rd(qts, THR);
    g_assert_cmphex(rd(qts, USR) & USR_BUSY, ==, 0);

    qtest_writel(qts, R(MCR), MCR_LB);
    qtest_writel(qts, R(THR), 'L');
    g_assert_cmphex(rd(qts, USR) & USR_BUSY, ==, USR_BUSY);
    rd(qts, THR);
    g_assert_cmphex(rd(qts, USR) & USR_BUSY, ==, 0);

    /* busy-detect: LCR write rejected while BUSY=1 */
    qtest_writel(qts, R(MCR), 0);
    qtest_writel(qts, R(IIR), FCR_FE | FCR_RR | FCR_XR);
    s1(qts, fd, 'B');
    g_assert_cmphex(rd(qts, USR) & USR_BUSY, ==, USR_BUSY);
    qtest_writel(qts, R(LCR), LCR_8N1 | LCR_DLAB);
    g_assert_cmphex(rd(qts, LCR), ==, LCR_8N1);
    g_assert_cmphex(iid(qts), ==, IIR_BUSY);
    rd(qts, USR);
    g_assert_cmphex(iid(qts), ==, IIR_NONE);
    qtest_writel(qts, R(IIR), FCR_FE | FCR_RR);
    g_assert_cmphex(rd(qts, USR) & USR_BUSY, ==, 0);
    qtest_writel(qts, R(LCR), LCR_8N1 | LCR_DLAB);
    g_assert_cmphex(rd(qts, LCR), ==, LCR_8N1 | LCR_DLAB);

    close(fd); qtest_quit(qts);
}

/* 8. advanced features */
static void test_advanced_features(void)
{
    QTestState *qts = qtest_init("-machine k230 "
                                 "-chardev null,id=c0 -serial chardev:c0");

    /* shadow: SRTS<->MCR.RTS, SBCR<->LCR.BC, SDMAM, SFE, SRT, STET, HTX */
    qtest_writel(qts, R(SRTS), 1);
    g_assert_cmphex(rd(qts, MCR) & MCR_RTS, ==, MCR_RTS);
    qtest_writel(qts, R(MCR), 0); g_assert_cmphex(rd(qts, SRTS), ==, 0);
    qtest_writel(qts, R(SBCR), 1);
    g_assert_cmphex(rd(qts, LCR) & LCR_BC, ==, LCR_BC);
    qtest_writel(qts, R(LCR), 0); g_assert_cmphex(rd(qts, SBCR), ==, 0);
    qtest_writel(qts, R(SDMAM), 1); g_assert_cmphex(rd(qts, SDMAM), ==, 1);
    qtest_writel(qts, R(SRT), 0x2); g_assert_cmphex(rd(qts, SRT), ==, 0x2);
    qtest_writel(qts, R(STET), 0x3); g_assert_cmphex(rd(qts, STET), ==, 0x3);
    qtest_writel(qts, R(SFE), 1); g_assert_cmphex(rd(qts, SFE), ==, 1);
    g_assert_cmphex(rd(qts, IIR) & IIR_FF, ==, IIR_FF);
    qtest_writel(qts, R(HTX), 1); g_assert_cmphex(rd(qts, HTX), ==, 1);
    qtest_writel(qts, R(HTX), 0); g_assert_cmphex(rd(qts, HTX), ==, 0);

    /* HTX halt TX */
    qtest_writel(qts, R(LCR), LCR_8N1);
    qtest_writel(qts, R(IIR), FCR_FE);
    qtest_writel(qts, R(MCR), MCR_LB);

    qtest_writel(qts, R(HTX), 1);
    qtest_writel(qts, R(THR), 'H');
    g_assert_cmphex(rd(qts, TFL), ==, 1);
    g_assert_cmphex(rd(qts, RFL), ==, 0);

    qtest_writel(qts, R(HTX), 0);
    g_assert_cmphex(rd(qts, TFL), ==, 0);
    g_assert_cmphex(rd(qts, RFL), ==, 1);
    g_assert_cmphex(rd(qts, THR), ==, 'H');

    /* SRR: RFR & UR */
    qtest_writel(qts, R(THR), 'Z');
    g_assert_cmphex(rd(qts, RFL), ==, 1);
    g_assert_cmphex(rd(qts, LSR) & LSR_DR, ==, LSR_DR);
    qtest_writel(qts, R(SRR), SRR_RFR);
    g_assert_cmphex(rd(qts, RFL), ==, 0);
    g_assert_cmphex(rd(qts, LSR) & LSR_DR, ==, 0);
    g_assert_cmphex(rd(qts, SRR), ==, 0);
    qtest_writel(qts, R(THR), 'Y');
    qtest_writel(qts, R(SRR), SRR_UR);
    g_assert_cmphex(rd(qts, LSR), ==, LSR_RESET);
    g_assert_cmphex(iid(qts), ==, IIR_NONE);
    g_assert_cmphex(rd(qts, RFL), ==, 0);

    /* PTIME + TET programmable THRE */
    qtest_writel(qts, R(LCR), LCR_8N1);
    qtest_writel(qts, R(IIR), FCR_FE | FCR_TET_H);
    qtest_writel(qts, R(IER), IER_TX | IER_PTIME);
    qtest_writel(qts, R(THR), 'A');
    g_assert_cmphex(rd(qts, LSR) & LSR_THRE, ==, LSR_THRE);
    g_assert_cmphex(iid(qts), ==, IIR_THR);

    qtest_quit(qts);
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/k230-uart/device_probe",       test_device_probe);
    qtest_add_func("/k230-uart/init_and_baud",      test_init_and_baud);
    qtest_add_func("/k230-uart/tx_rx_datapath",     test_tx_rx_datapath);
    qtest_add_func("/k230-uart/thre_interrupt",     test_thre_interrupt);
    qtest_add_func("/k230-uart/rx_interrupts",      test_rx_interrupts);
    qtest_add_func("/k230-uart/error_interrupts",   test_error_interrupts);
    qtest_add_func("/k230-uart/busy_detect",        test_busy_detect);
    qtest_add_func("/k230-uart/advanced_features",  test_advanced_features);
    return g_test_run();
}
