/*
 * QTest testcase for the ASPEED AST2700 GPIO Controller.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2025 Google LLC.
 */

#include "qemu/osdep.h"
#include "qobject/qdict.h"
#include "qemu/bitops.h"
#include "qemu/timer.h"
#include "qobject/qdict.h"
#include "libqtest-single.h"
#include "qemu/error-report.h"

#define ASPEED_SGPIO_MAX_PIN_PAIR 256
#define AST2700_SGPIO0_BASE 0x14C0C000
#define AST2700_SGPIO1_BASE 0x14C0D000
#define SGPIO_0_CONTROL 0x80

static void test_output_pins(const char *machine, const uint32_t base, int idx)
{
    QTestState *s = qtest_init(machine);
    char name[16];
    char qom_path[64];
    uint32_t offset = 0;
    uint32_t value = 0;
    for (int i = 0; i < ASPEED_SGPIO_MAX_PIN_PAIR; i++) {
        /* Odd index is output port */
        sprintf(name, "sgpio%d", i * 2 + 1);
        sprintf(qom_path, "/machine/soc/sgpio[%d]", idx);
        offset = base + SGPIO_0_CONTROL + (i * 4);
        /* set serial output */
        qtest_writel(s, offset, 0x00000001);
        value = qtest_readl(s, offset);
        g_assert_cmphex(value, ==, 0x00000001);
        g_assert_cmphex(qtest_qom_get_bool(s, qom_path, name), ==, true);

        /* clear serial output */
        qtest_writel(s, offset, 0x00000000);
        value = qtest_readl(s, offset);
        g_assert_cmphex(value, ==, 0);
        g_assert_cmphex(qtest_qom_get_bool(s, qom_path, name), ==, false);
    }
    qtest_quit(s);
}

static void test_input_pins(const char *machine, const uint32_t base, int idx)
{
    QTestState *s = qtest_init(machine);
    char name[16];
    char qom_path[64];
    uint32_t offset = 0;
    uint32_t value = 0;
    for (int i = 0; i < ASPEED_SGPIO_MAX_PIN_PAIR; i++) {
        /* Even index is input port */
        sprintf(name, "sgpio%d", i * 2);
        sprintf(qom_path, "/machine/soc/sgpio[%d]", idx);
        offset = base + SGPIO_0_CONTROL + (i * 4);
        /* set serial input */
        qtest_qom_set_bool(s, qom_path, name, true);
        value = qtest_readl(s, offset);
        g_assert_cmphex(value, ==, 0x00002000);
        g_assert_cmphex(qtest_qom_get_bool(s, qom_path, name), ==, true);

        /* clear serial input */
        qtest_qom_set_bool(s, qom_path, name, false);
        value = qtest_readl(s, offset);
        g_assert_cmphex(value, ==, 0);
        g_assert_cmphex(qtest_qom_get_bool(s, qom_path, name), ==, false);
    }
    qtest_quit(s);
}

static void test_2700_input_pins(void)
{
    test_input_pins("-machine ast2700-evb",
                    AST2700_SGPIO0_BASE, 0);
    test_input_pins("-machine ast2700-evb",
                    AST2700_SGPIO1_BASE, 1);
    test_output_pins("-machine ast2700-evb",
                    AST2700_SGPIO0_BASE, 0);
    test_output_pins("-machine ast2700-evb",
                    AST2700_SGPIO1_BASE, 1);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/ast2700/sgpio/input_pins", test_2700_input_pins);

    return g_test_run();
}
