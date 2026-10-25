/*
 * QTest testcase for Xilinx AXI GPIO IP core
 *
 * Copyright (c) 2026 Pufan Jin <cyan2254650260@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qobject/qdict.h"
#include "libqtest.h"

#define GPIO_BASE           0x81400000

#define R_GPIO_DATA         0x0000
#define R_GPIO_TRI          0x0004
#define R_GPIO2_DATA        0x0008
#define R_GPIO2_TRI         0x000C
#define R_GIER              0x011C
#define R_IPISR             0x0120
#define R_IPIER             0x0128

#define GIER_GIE            0x80000000
#define IPISR_CH1           0x00000001
#define IPISR_CH2           0x00000002

#define GPIO_QOM_PATH       "/machine/unattached/device[6]"

static void test_reset_values(void)
{
    QTestState *qts = qtest_init("-machine petalogix-s3adsp1800");

    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_GPIO_TRI), ==, 0xFFFFFFFF);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_GPIO2_TRI), ==, 0xFFFFFFFF);

    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_GPIO_DATA), ==, 0x0);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_GPIO2_DATA), ==, 0x0);

    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_GIER), ==, 0x0);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_IPISR), ==, 0x0);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_IPIER), ==, 0x0);

    qtest_quit(qts);
}

static void test_gpio_data_output(void)
{
    QTestState *qts = qtest_init("-machine petalogix-s3adsp1800");

    qtest_writel(qts, GPIO_BASE + R_GPIO_TRI, 0x00000000);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_GPIO_TRI), ==, 0x00000000);

    qtest_writel(qts, GPIO_BASE + R_GPIO_DATA, 0x12345678);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_GPIO_DATA), ==, 0x12345678);

    qtest_writel(qts, GPIO_BASE + R_GPIO_DATA, 0xDEADBEEF);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_GPIO_DATA), ==, 0xDEADBEEF);

    qtest_writel(qts, GPIO_BASE + R_GPIO2_TRI, 0x00000000);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_GPIO2_TRI), ==, 0x00000000);

    qtest_writel(qts, GPIO_BASE + R_GPIO2_DATA, 0xCAFEBABE);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_GPIO2_DATA), ==, 0xCAFEBABE);

    qtest_quit(qts);
}

static void test_gpio_direction(void)
{
    QTestState *qts = qtest_init("-machine petalogix-s3adsp1800");

    qtest_writel(qts, GPIO_BASE + R_GPIO_TRI, 0xAAAA5555);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_GPIO_TRI), ==, 0xAAAA5555);

    qtest_writel(qts, GPIO_BASE + R_GPIO2_TRI, 0x5555AAAA);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_GPIO2_TRI), ==, 0x5555AAAA);

    qtest_quit(qts);
}

static void test_gpio_input_and_irq(void)
{
    QTestState *qts = qtest_init("-machine petalogix-s3adsp1800");

    qtest_irq_intercept_out_named(qts, GPIO_QOM_PATH, "sysbus-irq");

    qtest_writel(qts, GPIO_BASE + R_GIER, GIER_GIE);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_GIER), ==, GIER_GIE);

    qtest_writel(qts, GPIO_BASE + R_IPIER, IPISR_CH1 | IPISR_CH2);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_IPIER), ==,
                    IPISR_CH1 | IPISR_CH2);

    g_assert_false(qtest_get_irq(qts, 0));
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_IPISR), ==, 0x0);

    qtest_set_irq_in(qts, GPIO_QOM_PATH, NULL, 0, 1);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_GPIO_DATA), ==, 0x1);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_IPISR), ==, IPISR_CH1);
    g_assert_true(qtest_get_irq(qts, 0));

    qtest_writel(qts, GPIO_BASE + R_IPISR, IPISR_CH1);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_IPISR), ==, 0x0);
    g_assert_false(qtest_get_irq(qts, 0));

    qtest_set_irq_in(qts, GPIO_QOM_PATH, NULL, 32 + 3, 1);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_GPIO2_DATA), ==, (1 << 3));
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_IPISR), ==, IPISR_CH2);
    g_assert_true(qtest_get_irq(qts, 0));

    qtest_writel(qts, GPIO_BASE + R_IPISR, IPISR_CH2);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_IPISR), ==, 0x0);
    g_assert_false(qtest_get_irq(qts, 0));

    qtest_quit(qts);
}

static void test_gpio_boundary_registers(void)
{
    static const uint32_t unmapped_addrs[] = {
        0x0010, 0x0014, 0x0018, 0x001C, 0x0020,
        0x0100, 0x0118, 0x0124, 0x012C, 0x01FC,
    };
    QTestState *qts = qtest_init("-machine petalogix-s3adsp1800");
    size_t i;

    for (i = 0; i < ARRAY_SIZE(unmapped_addrs); i++) {
        uint32_t addr = GPIO_BASE + unmapped_addrs[i];

        g_assert_cmphex(qtest_readl(qts, addr), ==, 0x0);
        qtest_writel(qts, addr, 0xDEADBEEF);
        g_assert_cmphex(qtest_readl(qts, addr), ==, 0x0);
    }

    qtest_writel(qts, GPIO_BASE + R_GIER, 0xFFFFFFFF);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_GIER), ==, GIER_GIE);

    qtest_writel(qts, GPIO_BASE + R_IPIER, 0xFFFFFFFF);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_IPIER), ==,
                    IPISR_CH1 | IPISR_CH2);

    qtest_set_irq_in(qts, GPIO_QOM_PATH, NULL, 0, 1);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_IPISR), ==, IPISR_CH1);
    qtest_writel(qts, GPIO_BASE + R_IPISR, 0x0);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_IPISR), ==, IPISR_CH1);
    qtest_writel(qts, GPIO_BASE + R_IPISR, IPISR_CH2);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_IPISR), ==, IPISR_CH1);
    qtest_writel(qts, GPIO_BASE + R_IPISR, IPISR_CH1);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_IPISR), ==, 0x0);

    qtest_quit(qts);
}

static void test_gpio_interrupt_modes(void)
{
    QTestState *qts = qtest_init("-machine petalogix-s3adsp1800");

    qtest_irq_intercept_out_named(qts, GPIO_QOM_PATH, "sysbus-irq");

    qtest_writel(qts, GPIO_BASE + R_GIER, GIER_GIE);
    qtest_writel(qts, GPIO_BASE + R_IPIER, IPISR_CH1 | IPISR_CH2);

    qtest_set_irq_in(qts, GPIO_QOM_PATH, NULL, 0, 1);
    qtest_set_irq_in(qts, GPIO_QOM_PATH, NULL, 32 + 5, 1);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_IPISR), ==,
                    IPISR_CH1 | IPISR_CH2);
    g_assert_true(qtest_get_irq(qts, 0));

    qtest_writel(qts, GPIO_BASE + R_IPISR, IPISR_CH1);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_IPISR), ==, IPISR_CH2);
    g_assert_true(qtest_get_irq(qts, 0));

    qtest_writel(qts, GPIO_BASE + R_IPISR, IPISR_CH2);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_IPISR), ==, 0x0);
    g_assert_false(qtest_get_irq(qts, 0));

    qtest_set_irq_in(qts, GPIO_QOM_PATH, NULL, 0, 0);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_IPISR), ==, IPISR_CH1);
    g_assert_true(qtest_get_irq(qts, 0));
    qtest_writel(qts, GPIO_BASE + R_IPISR, IPISR_CH1);
    g_assert_false(qtest_get_irq(qts, 0));

    qtest_writel(qts, GPIO_BASE + R_GPIO_TRI, 0xFFFFFFFE); /* pin 0 output */
    qtest_set_irq_in(qts, GPIO_QOM_PATH, NULL, 0, 1);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_IPISR), ==, 0x0);
    g_assert_false(qtest_get_irq(qts, 0));

    qtest_writel(qts, GPIO_BASE + R_GPIO_TRI, 0xFFFFFFFF); /* pin 0 input */
    qtest_writel(qts, GPIO_BASE + R_GIER, 0x0);
    qtest_set_irq_in(qts, GPIO_QOM_PATH, NULL, 0, 0);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_IPISR), ==, IPISR_CH1);
    g_assert_false(qtest_get_irq(qts, 0));

    qtest_writel(qts, GPIO_BASE + R_GIER, GIER_GIE);
    g_assert_true(qtest_get_irq(qts, 0));

    qtest_writel(qts, GPIO_BASE + R_GIER, 0x0);
    g_assert_false(qtest_get_irq(qts, 0));

    qtest_writel(qts, GPIO_BASE + R_IPISR, IPISR_CH1);
    qtest_quit(qts);
}

static void test_physical_outputs(void)
{
    QTestState *qts = qtest_init("-machine petalogix-s3adsp1800");
    int i;

    qtest_irq_intercept_out(qts, GPIO_QOM_PATH);

    qtest_writel(qts, GPIO_BASE + R_GPIO_TRI, 0x00000000);

    qtest_writel(qts, GPIO_BASE + R_GPIO_DATA, 0x12345678);
    for (i = 0; i < 32; i++) {
        bool expected = (0x12345678 >> i) & 1;
        g_assert_cmpint(qtest_get_irq(qts, i), ==, expected);
    }

    qtest_writel(qts, GPIO_BASE + R_GPIO_DATA, 0xEDCBA987);
    for (i = 0; i < 32; i++) {
        bool expected = (0xEDCBA987 >> i) & 1;
        g_assert_cmpint(qtest_get_irq(qts, i), ==, expected);
    }

    qtest_writel(qts, GPIO_BASE + R_GPIO_TRI, 0xFFFFFFFF);
    qtest_writel(qts, GPIO_BASE + R_GPIO_DATA, 0xFFFFFFFF);
    for (i = 0; i < 32; i++) {
        bool expected = (0xEDCBA987 >> i) & 1;
        g_assert_cmpint(qtest_get_irq(qts, i), ==, expected);
    }

    qtest_quit(qts);
}

static void test_physical_outputs_ch2(void)
{
    QTestState *qts = qtest_init("-machine petalogix-s3adsp1800");
    int i;

    qtest_irq_intercept_out_named(qts, GPIO_QOM_PATH, "gpio2-out");

    qtest_writel(qts, GPIO_BASE + R_GPIO2_TRI, 0x00000000);

    qtest_writel(qts, GPIO_BASE + R_GPIO2_DATA, 0xA5A5A5A5);
    for (i = 0; i < 32; i++) {
        bool expected = (0xA5A5A5A5 >> i) & 1;
        g_assert_cmpint(qtest_get_irq(qts, i), ==, expected);
    }

    qtest_writel(qts, GPIO_BASE + R_GPIO2_TRI, 0xFFFFFFFF);
    qtest_writel(qts, GPIO_BASE + R_GPIO2_DATA, 0xFFFFFFFF);
    for (i = 0; i < 32; i++) {
        bool expected = (0xA5A5A5A5 >> i) & 1;
        g_assert_cmpint(qtest_get_irq(qts, i), ==, expected);
    }

    qtest_quit(qts);
}

static void test_mixed_direction_multiplexing(void)
{
    QTestState *qts = qtest_init("-machine petalogix-s3adsp1800");
    int i;

    qtest_writel(qts, GPIO_BASE + R_GPIO_TRI, 0xAAAAAAAA);

    qtest_writel(qts, GPIO_BASE + R_GPIO_DATA, 0x55555555);

    for (i = 1; i < 32; i += 2) {
        qtest_set_irq_in(qts, GPIO_QOM_PATH, NULL, i, 1);
    }

    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_GPIO_DATA), ==, 0xFFFFFFFF);

    for (i = 1; i < 32; i += 2) {
        qtest_set_irq_in(qts, GPIO_QOM_PATH, NULL, i, 0);
    }

    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_GPIO_DATA), ==, 0x55555555);

    qtest_quit(qts);
}

static void test_system_reset_active(void)
{
    QTestState *qts = qtest_init("-machine petalogix-s3adsp1800");

    qtest_irq_intercept_out_named(qts, GPIO_QOM_PATH, "sysbus-irq");

    qtest_writel(qts, GPIO_BASE + R_GPIO_TRI, 0xFFFFFFFD);
    qtest_writel(qts, GPIO_BASE + R_GPIO_DATA, 0xDEADBEEF);
    qtest_writel(qts, GPIO_BASE + R_GPIO2_TRI, 0x12345678);
    qtest_writel(qts, GPIO_BASE + R_GPIO2_DATA, 0x87654321);
    qtest_writel(qts, GPIO_BASE + R_GIER, GIER_GIE);
    qtest_writel(qts, GPIO_BASE + R_IPIER, IPISR_CH1 | IPISR_CH2);

    qtest_set_irq_in(qts, GPIO_QOM_PATH, NULL, 0, 1);
    g_assert_true(qtest_get_irq(qts, 0));
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_IPISR), ==, IPISR_CH1);
    qtest_set_irq_in(qts, GPIO_QOM_PATH, NULL, 0, 0);
    g_assert_true(qtest_get_irq(qts, 0));
    qtest_system_reset(qts);

    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_GPIO_TRI), ==, 0xFFFFFFFF);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_GPIO_DATA), ==, 0x0);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_GPIO2_TRI), ==, 0xFFFFFFFF);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_GPIO2_DATA), ==, 0x0);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_GIER), ==, 0x0);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_IPIER), ==, 0x0);
    g_assert_cmphex(qtest_readl(qts, GPIO_BASE + R_IPISR), ==, 0x0);

    g_assert_false(qtest_get_irq(qts, 0));

    qtest_quit(qts);
}

static void test_qom_introspection(void)
{
    QTestState *qts = qtest_init("-machine petalogix-s3adsp1800");
    QDict *r;

    r = qtest_qmp(qts, "{ 'execute': 'qom-get', 'arguments': "
                       "{ 'path': %s, 'property': 'width1' } }",
                       GPIO_QOM_PATH);
    g_assert_nonnull(r);
    g_assert_cmpuint(qdict_get_uint(r, "return"), ==, 32);
    qobject_unref(r);

    r = qtest_qmp(qts, "{ 'execute': 'qom-get', 'arguments': "
                       "{ 'path': %s, 'property': 'width2' } }",
                       GPIO_QOM_PATH);
    g_assert_nonnull(r);
    g_assert_cmpuint(qdict_get_uint(r, "return"), ==, 32);
    qobject_unref(r);

    r = qtest_qmp(qts, "{ 'execute': 'qom-get', 'arguments': "
                       "{ 'path': %s, 'property': 'is-dual' } }",
                       GPIO_QOM_PATH);
    g_assert_nonnull(r);
    g_assert_true(qdict_get_bool(r, "return"));
    qobject_unref(r);

    r = qtest_qmp(qts, "{ 'execute': 'qom-get', 'arguments': "
                       "{ 'path': %s, 'property': 'endianness' } }",
                       GPIO_QOM_PATH);
    g_assert_nonnull(r);
    g_assert_cmpstr(qdict_get_str(r, "return"), ==, "big");
    qobject_unref(r);

    qtest_quit(qts);
}


int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/xlnx-axi-gpio/reset_values", test_reset_values);
    qtest_add_func("/xlnx-axi-gpio/data_output", test_gpio_data_output);
    qtest_add_func("/xlnx-axi-gpio/direction", test_gpio_direction);
    qtest_add_func("/xlnx-axi-gpio/input_and_irq", test_gpio_input_and_irq);
    qtest_add_func("/xlnx-axi-gpio/boundaries", test_gpio_boundary_registers);
    qtest_add_func("/xlnx-axi-gpio/interrupt_modes", test_gpio_interrupt_modes);
    qtest_add_func("/xlnx-axi-gpio/physical_outputs", test_physical_outputs);
    qtest_add_func("/xlnx-axi-gpio/physical_outputs_ch2",
                   test_physical_outputs_ch2);
    qtest_add_func("/xlnx-axi-gpio/mixed_multiplexing",
                   test_mixed_direction_multiplexing);
    qtest_add_func("/xlnx-axi-gpio/system_reset", test_system_reset_active);
    qtest_add_func("/xlnx-axi-gpio/qom_introspection", test_qom_introspection);
    return g_test_run();
}
