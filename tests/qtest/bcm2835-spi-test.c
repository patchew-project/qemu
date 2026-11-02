/*
 * QTest testcase for the BCM2835 SPI controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "libqtest.h"

#define SPI0_BASE       0x3f204000
#define SPI_CS          0x00
#define SPI_FIFO        0x04

#define CS_CLEAR_RXTX   (3 << 4)
#define CS_TA           BIT(7)
#define CS_INTD         BIT(9)

/* A reset in the middle of a transfer drops the interrupt */
static void test_reset_irq(void)
{
    QTestState *qts = qtest_init("-M raspi3b");

    qtest_irq_intercept_out_named(qts, "/machine/soc/peripherals/bcm2835-spi0",
                                  "sysbus-irq");
    qtest_writel(qts, SPI0_BASE + SPI_CS, CS_CLEAR_RXTX | CS_TA);
    qtest_writel(qts, SPI0_BASE + SPI_FIFO, 0xa5);
    qtest_writel(qts, SPI0_BASE + SPI_CS, CS_TA | CS_INTD);
    g_assert_true(qtest_get_irq(qts, 0));

    qtest_system_reset(qts);
    g_assert_false(qtest_get_irq(qts, 0));

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/bcm2835-spi/reset-irq", test_reset_irq);
    return g_test_run();
}
