/*
 * QTest testcase for the RP2040 ring oscillator block.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/bitops.h"

#define ROSC_BASE              0x40060000
#define ROSC_CTRL              0x00
#define ROSC_FREQA             0x04
#define ROSC_DORMANT           0x0c
#define ROSC_DIV               0x10
#define ROSC_PHASE             0x14
#define ROSC_STATUS            0x18
#define ROSC_RANDOMBIT         0x1c
#define ROSC_COUNT             0x20

#define ROSC_ENABLE_ENABLE     0xfab
#define ROSC_ENABLE_DISABLE    0xd1e
#define ROSC_FREQ_RANGE_LOW    0xfa4
#define ROSC_WAKE_VALUE        0x77616b65
#define ROSC_STATUS_STABLE     BIT(31)
#define ROSC_STATUS_BADWRITE   BIT(24)
#define ROSC_STATUS_DIV_RUNNING BIT(16)
#define ROSC_STATUS_ENABLED    BIT(12)
#define ROSC_STATUS_RUNNING \
    (ROSC_STATUS_STABLE | ROSC_STATUS_DIV_RUNNING | ROSC_STATUS_ENABLED)

static QTestState *rp2040_start(void)
{
    return qtest_init("-machine raspi-pico");
}

static uint32_t read_random_bits(QTestState *qts, unsigned count)
{
    uint32_t value = 0;
    unsigned i;

    for (i = 0; i < count; i++) {
        value |= (qtest_readl(qts, ROSC_BASE + ROSC_RANDOMBIT) & BIT(0)) << i;
    }

    return value;
}

static void test_rosc_reset_values(void)
{
    QTestState *qts = rp2040_start();

    g_assert_cmphex(qtest_readl(qts, ROSC_BASE + ROSC_CTRL), ==,
                    (ROSC_ENABLE_ENABLE << 12) | ROSC_FREQ_RANGE_LOW);
    g_assert_cmphex(qtest_readl(qts, ROSC_BASE + ROSC_DORMANT), ==,
                    ROSC_WAKE_VALUE);
    g_assert_cmphex(qtest_readl(qts, ROSC_BASE + ROSC_DIV), ==, 0x00000ab0);
    g_assert_cmphex(qtest_readl(qts, ROSC_BASE + ROSC_STATUS), ==,
                    ROSC_STATUS_RUNNING);

    qtest_quit(qts);
}

static void test_rosc_badwrite_and_clear(void)
{
    QTestState *qts = rp2040_start();

    qtest_writel(qts, ROSC_BASE + ROSC_FREQA, 0x00007777);
    g_assert_cmphex(qtest_readl(qts, ROSC_BASE + ROSC_FREQA), ==, 0);
    g_assert_cmphex(qtest_readl(qts, ROSC_BASE + ROSC_STATUS) &
                    ROSC_STATUS_BADWRITE, ==, ROSC_STATUS_BADWRITE);

    qtest_writel(qts, ROSC_BASE + ROSC_STATUS, ROSC_STATUS_BADWRITE);
    g_assert_cmphex(qtest_readl(qts, ROSC_BASE + ROSC_STATUS) &
                    ROSC_STATUS_BADWRITE, ==, 0);

    qtest_quit(qts);
}

static void test_rosc_count_reaches_zero(void)
{
    QTestState *qts = rp2040_start();
    int i;

    qtest_writel(qts, ROSC_BASE + ROSC_COUNT, 1);
    for (i = 0; i < 16 && qtest_readl(qts, ROSC_BASE + ROSC_COUNT); i++) {
        qtest_clock_step(qts, 1000);
    }

    g_assert_cmphex(qtest_readl(qts, ROSC_BASE + ROSC_COUNT), ==, 0);

    qtest_quit(qts);
}

static void test_rosc_misc_values(void)
{
    QTestState *qts = rp2040_start();

    g_assert_cmphex(qtest_readl(qts, ROSC_BASE + ROSC_RANDOMBIT) & ~BIT(0),
                    ==, 0);
    qtest_writel(qts, ROSC_BASE + ROSC_DIV, 0xaa1);
    g_assert_cmphex(qtest_readl(qts, ROSC_BASE + ROSC_DIV), ==, 0xaa1);
    qtest_writel(qts, ROSC_BASE + ROSC_PHASE, 0xaa8);
    g_assert_cmphex(qtest_readl(qts, ROSC_BASE + ROSC_PHASE), ==, 0xaa8);

    qtest_writel(qts, ROSC_BASE + ROSC_CTRL,
                 (ROSC_ENABLE_DISABLE << 12) | ROSC_FREQ_RANGE_LOW);
    g_assert_cmphex(qtest_readl(qts, ROSC_BASE + ROSC_STATUS) &
                    ROSC_STATUS_ENABLED, ==, 0);

    qtest_quit(qts);
}

static void test_rosc_seeded_randombit(void)
{
    QTestState *qts_a;
    QTestState *qts_b;
    uint32_t bits_a;
    uint32_t bits_b;

    qts_a = qtest_init("-machine raspi-pico,rosc-random-seed=0x1234");
    bits_a = read_random_bits(qts_a, 32);
    qtest_quit(qts_a);

    qts_b = qtest_init("-machine raspi-pico,rosc-random-seed=0x1234");
    bits_b = read_random_bits(qts_b, 32);
    qtest_quit(qts_b);

    g_assert_cmphex(bits_a, ==, bits_b);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/rp2040-rosc/reset-values", test_rosc_reset_values);
    qtest_add_func("/rp2040-rosc/badwrite-and-clear",
                   test_rosc_badwrite_and_clear);
    qtest_add_func("/rp2040-rosc/count-reaches-zero",
                   test_rosc_count_reaches_zero);
    qtest_add_func("/rp2040-rosc/misc-values", test_rosc_misc_values);
    qtest_add_func("/rp2040-rosc/seeded-randombit",
                   test_rosc_seeded_randombit);

    return g_test_run();
}
