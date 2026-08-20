/*
 * QTests for AM64x main_i2c0 controller
 *
 * Copyright (c) 2026 CMBLU Energy AG
 * Author: Wadim Mueller <wafgo01@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "libqtest.h"

/* main_i2c0 window: i2c@20000000, len 0x100. */
#define I2C0_BASE 0x20000000ULL

/* OMAP I2C V2 register offsets. */
#define I2C_V2_SYSC          0x10
#define I2C_V2_IRQSTATUS_RAW 0x24
#define I2C_V2_IRQSTATUS     0x28
#define I2C_V2_SYSS          0x90
#define I2C_V2_CNT           0x98
#define I2C_V2_DATA          0x9c
#define I2C_V2_CON           0xa4
#define I2C_V2_SA            0xac

/* Register bits. */
#define I2C_SYSC_SRST   (1 << 1)  /* SYSCONFIG soft reset */
#define I2C_SYSS_RDONE  (1 << 0)  /* reset done */
#define I2C_STAT_NACK   (1 << 1)  /* no acknowledgement */
#define I2C_STAT_ARDY   (1 << 2)  /* register access ready */
#define I2C_STAT_RRDY   (1 << 3)  /* receive data ready */

#define I2C_CON_EN  (1 << 15)     /* module enable */
#define I2C_CON_MST (1 << 10)     /* master mode */
#define I2C_CON_STP (1 << 1)      /* stop condition */
#define I2C_CON_STT (1 << 0)      /* start condition */

/*
 * SYSCONFIG.SRST must make SYSS.RDONE assert, or the firmware spins in
 * the soft-reset poll.
 */
static void test_soft_reset_completes(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");
    uint32_t syss = 0;

    qtest_writew(qts, I2C0_BASE + I2C_V2_SYSC, I2C_SYSC_SRST);

    for (int i = 0; i < 16; i++) {
        syss = qtest_readw(qts, I2C0_BASE + I2C_V2_SYSS);
        if (syss & I2C_SYSS_RDONE) {
            break;
        }
    }
    g_assert_cmphex(syss & I2C_SYSS_RDONE, ==, I2C_SYSS_RDONE);
    qtest_quit(qts);
}

/*
 * Reads from an address without a slave must raise NACK promptly, since a
 * driver would otherwise wait forever for an I2C event. No slave is
 * attached here, so any address does.
 */
static void test_nack_on_absent_slave(void)
{
    QTestState *qts = qtest_init("-machine am64-virt");
    uint32_t stat;

    /* Bring the controller out of reset as firmware does. */
    qtest_writew(qts, I2C0_BASE + I2C_V2_SYSC, I2C_SYSC_SRST);
    (void)qtest_readw(qts, I2C0_BASE + I2C_V2_SYSS);

    /* Address 0x51 has no slave: one-byte master read. */
    qtest_writew(qts, I2C0_BASE + I2C_V2_SA, 0x51);
    qtest_writew(qts, I2C0_BASE + I2C_V2_CNT, 1);
    qtest_writew(qts, I2C0_BASE + I2C_V2_CON,
                 I2C_CON_EN | I2C_CON_MST | I2C_CON_STT | I2C_CON_STP);

    stat = qtest_readw(qts, I2C0_BASE + I2C_V2_IRQSTATUS_RAW);
    g_assert_cmphex(stat & I2C_STAT_NACK, ==, I2C_STAT_NACK);

    /* IRQSTATUS mirrors NACK and is write-1-to-clear. */
    stat = qtest_readw(qts, I2C0_BASE + I2C_V2_IRQSTATUS);
    g_assert_cmphex(stat & I2C_STAT_NACK, ==, I2C_STAT_NACK);
    qtest_writew(qts, I2C0_BASE + I2C_V2_IRQSTATUS, I2C_STAT_NACK);
    stat = qtest_readw(qts, I2C0_BASE + I2C_V2_IRQSTATUS);
    g_assert_cmphex(stat & I2C_STAT_NACK, ==, 0);

    qtest_quit(qts);
}

/*
 * With a slave on the bus a one-byte master read has to ACK, return DATA
 * and then finish with ARDY. An at24c EEPROM at 0x50 serves as the slave;
 * it comes up erased, so offset 0 reads back as 0x00.
 */
static void test_eeprom_read_first_byte(void)
{
    QTestState *qts = qtest_init("-machine am64-virt "
                                 "-device at24c-eeprom,address=0x50,"
                                 "rom-size=4096");
    uint32_t stat;
    uint8_t b;

    qtest_writew(qts, I2C0_BASE + I2C_V2_SYSC, I2C_SYSC_SRST);
    (void)qtest_readw(qts, I2C0_BASE + I2C_V2_SYSS);

    /* One-byte read from 0x50; address pointer starts by 0. */
    qtest_writew(qts, I2C0_BASE + I2C_V2_SA, 0x50);
    qtest_writew(qts, I2C0_BASE + I2C_V2_CNT, 1);
    qtest_writew(qts, I2C0_BASE + I2C_V2_CON,
                 I2C_CON_EN | I2C_CON_MST | I2C_CON_STT | I2C_CON_STP);

    /* Slave ACKed and data are ready. */
    stat = qtest_readw(qts, I2C0_BASE + I2C_V2_IRQSTATUS_RAW);
    g_assert_cmphex(stat & I2C_STAT_NACK, ==, 0);
    g_assert_cmphex(stat & I2C_STAT_RRDY, ==, I2C_STAT_RRDY);

    /* Offset 0 of the erased EEPROM reads as 0x00. */
    b = qtest_readw(qts, I2C0_BASE + I2C_V2_DATA) & 0xff;
    g_assert_cmphex(b, ==, 0x00);

    /* Single-byte transfer complete: ARDY, no more RRDY. */
    stat = qtest_readw(qts, I2C0_BASE + I2C_V2_IRQSTATUS_RAW);
    g_assert_cmphex(stat & I2C_STAT_ARDY, ==, I2C_STAT_ARDY);
    g_assert_cmphex(stat & I2C_STAT_RRDY, ==, 0);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/am64/i2c/soft-reset", test_soft_reset_completes);
    qtest_add_func("/am64/i2c/nack", test_nack_on_absent_slave);
    qtest_add_func("/am64/i2c/eeprom-read", test_eeprom_read_first_byte);
    return g_test_run();
}
