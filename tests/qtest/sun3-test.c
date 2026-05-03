/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QTest device validation for the Sun-3 Machine
 *
 * Copyright (c) 2026
 */

#include "qemu/osdep.h"
#include "libqtest.h"

/* OBIO Memory Map Addresses */
#define SUN3_INTREG_BASE 0x0FEA0000
#define SUN3_MEMERR_BASE 0x0FE80000

static void test_intreg(void)
{
    QTestState *s;
    uint8_t val;

    s = qtest_init("-machine sun3 -nographic");

    /* Default state should be 0 */
    val = qtest_readb(s, SUN3_INTREG_BASE);
    g_assert_cmphex(val, ==, 0x00);

    /* Write and verify */
    qtest_writeb(s, SUN3_INTREG_BASE, 0x0f);
    val = qtest_readb(s, SUN3_INTREG_BASE);
    g_assert_cmphex(val, ==, 0x0f);

    qtest_quit(s);
}

static void test_memerr(void)
{
    QTestState *s;
    uint8_t val;

    s = qtest_init("-machine sun3 -nographic");

    /* Default state should be 0x40 (default no-error) */
    val = qtest_readb(s, SUN3_MEMERR_BASE);
    g_assert_cmphex(val, ==, 0x40);

    /* Test parity error spoof sequence */
    qtest_writeb(s, SUN3_MEMERR_BASE, 0x20); /* Set test_parity_written */
    qtest_writeb(s, SUN3_MEMERR_BASE, 0x50); /* Trigger spoof */
    val = qtest_readb(s, SUN3_MEMERR_BASE);

    /*
     * Since we didn't clock enough parity lanes, high bit and lane 8 should
     * be set (0x50 | 0x80 | 0x08 = 0xd8)
     */
    g_assert_cmphex(val, ==, 0xd8);

    qtest_quit(s);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/m68k/sun3/intreg", test_intreg);
    qtest_add_func("/m68k/sun3/memerr", test_memerr);

    return g_test_run();
}
