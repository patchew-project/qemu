/*
 * QTest testcase for the RP2040 testbench manager block.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/bitops.h"

#define TBMAN_BASE          0x4006c000
#define TBMAN_PLATFORM      0x00
#define TBMAN_PLATFORM_ASIC BIT(0)

static void test_tbman_platform(void)
{
    QTestState *qts = qtest_init("-machine raspi-pico");

    g_assert_cmphex(qtest_readl(qts, TBMAN_BASE + TBMAN_PLATFORM), ==,
                    TBMAN_PLATFORM_ASIC);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/rp2040-tbman/platform", test_tbman_platform);

    return g_test_run();
}
