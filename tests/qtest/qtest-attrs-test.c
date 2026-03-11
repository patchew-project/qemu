/*
 * QTest for memory access with transaction attributes
 *
 * Verify optional attrs argument support for qtest memory commands.
 *
 * Copyright (c) 2026 Phytium Technology
 *
 * Author:
 *  Tao Tang <tangtao1634@phytium.com.cn>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqtest-single.h"

/*
 * Default RAM size is 128 MiB on both machines used below.
 * Keep test addresses in low RAM and away from device MMIO regions.
 */
#define TEST_ADDR_OFFSET    0x1000ULL
#define TEST_ARM_BASE       0x40000000ULL
#define TEST_X86_BASE       0x0ULL

#define TEST_ADDR_ARM       (TEST_ARM_BASE + TEST_ADDR_OFFSET)
#define TEST_ADDR_X86       (TEST_X86_BASE + TEST_ADDR_OFFSET)

#define ARM_MACHINE_ARGS    "-machine virt,secure=on -cpu cortex-a57"
#define X86_MACHINE_ARGS    "-machine pc -accel tcg"

static void test_arm_scalar_attrs(void)
{
    QTestState *qts;
    uint8_t val;

    if (!qtest_has_machine("virt")) {
        g_test_skip("virt machine not available");
        return;
    }

    qts = qtest_init(ARM_MACHINE_ARGS);

    qtest_writeb_attrs(qts, TEST_ADDR_ARM, 0x11, NULL);
    val = qtest_readb_attrs(qts, TEST_ADDR_ARM, NULL);
    g_assert_cmpuint(val, ==, 0x11);

    qtest_writeb_attrs(qts, TEST_ADDR_ARM + 0x1, 0x22, "secure");
    val = qtest_readb_attrs(qts, TEST_ADDR_ARM + 0x1, "secure");
    g_assert_cmpuint(val, ==, 0x22);

    qtest_writeb_attrs(qts, TEST_ADDR_ARM + 0x2, 0x33, "space=realm");
    val = qtest_readb_attrs(qts, TEST_ADDR_ARM + 0x2, "space=realm");
    g_assert_cmpuint(val, ==, 0x33);

    qtest_writeb_attrs(qts, TEST_ADDR_ARM + 0x3, 0x44, "space=root");
    val = qtest_readb_attrs(qts, TEST_ADDR_ARM + 0x3, "space=root");
    g_assert_cmpuint(val, ==, 0x44);

    qtest_writeb_attrs(qts, TEST_ADDR_ARM + 0x4, 0x55, "space=secure");
    val = qtest_readb_attrs(qts, TEST_ADDR_ARM + 0x4, "space=secure");
    g_assert_cmpuint(val, ==, 0x55);

    /* space=non-secure is equivalent to no attrs argument */
    qtest_writeb_attrs(qts, TEST_ADDR_ARM + 0x5, 0x66, "space=non-secure");
    val = qtest_readb_attrs(qts, TEST_ADDR_ARM + 0x5, NULL);
    g_assert_cmpuint(val, ==, 0x66);

    qtest_writeb_attrs(qts, TEST_ADDR_ARM + 0x6, 0x77, NULL);
    val = qtest_readb_attrs(qts, TEST_ADDR_ARM + 0x6, "space=non-secure");
    g_assert_cmpuint(val, ==, 0x77);

    qtest_quit(qts);
}

static void test_arm_bulk_attrs(void)
{
    QTestState *qts;
    uint8_t wbuf[16] = {
        0x00, 0x11, 0x22, 0x33,
        0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb,
        0xcc, 0xdd, 0xee, 0xff,
    };
    uint8_t rbuf[16];
    size_t i;

    if (!qtest_has_machine("virt")) {
        g_test_skip("virt machine not available");
        return;
    }

    qts = qtest_init(ARM_MACHINE_ARGS);

    qtest_memwrite_attrs(qts, TEST_ADDR_ARM + 0x100,
                         wbuf, sizeof(wbuf), NULL);
    qtest_memread_attrs(qts, TEST_ADDR_ARM + 0x100,
                        rbuf, sizeof(rbuf), NULL);
    g_assert(memcmp(wbuf, rbuf, sizeof(wbuf)) == 0);

    qtest_memwrite_attrs(qts, TEST_ADDR_ARM + 0x200,
                         wbuf, sizeof(wbuf), "secure");
    qtest_memread_attrs(qts, TEST_ADDR_ARM + 0x200,
                        rbuf, sizeof(rbuf), "secure");
    g_assert(memcmp(wbuf, rbuf, sizeof(wbuf)) == 0);

    qtest_memwrite_attrs(qts, TEST_ADDR_ARM + 0x300,
                         wbuf, sizeof(wbuf), "space=realm");
    qtest_memread_attrs(qts, TEST_ADDR_ARM + 0x300,
                        rbuf, sizeof(rbuf), "space=realm");
    g_assert(memcmp(wbuf, rbuf, sizeof(wbuf)) == 0);

    qtest_memset_attrs(qts, TEST_ADDR_ARM + 0x400,
                       0xa5, sizeof(rbuf), "space=root");
    qtest_memread_attrs(qts, TEST_ADDR_ARM + 0x400,
                        rbuf, sizeof(rbuf), "space=root");
    for (i = 0; i < sizeof(rbuf); i++) {
        g_assert_cmpuint(rbuf[i], ==, 0xa5);
    }

    qtest_memset_attrs(qts, TEST_ADDR_ARM + 0x500,
                       0x5a, sizeof(rbuf), "space=non-secure");
    qtest_memread_attrs(qts, TEST_ADDR_ARM + 0x500,
                        rbuf, sizeof(rbuf), NULL);
    for (i = 0; i < sizeof(rbuf); i++) {
        g_assert_cmpuint(rbuf[i], ==, 0x5a);
    }

    qtest_quit(qts);
}

static void test_arm_single_shortcuts_attrs(void)
{
    uint8_t val;
    uint8_t wbuf[4] = { 0x10, 0x20, 0x30, 0x40 };
    uint8_t rbuf[4];

    if (!qtest_has_machine("virt")) {
        g_test_skip("virt machine not available");
        return;
    }

    qtest_start(ARM_MACHINE_ARGS);

    writeb_attrs(TEST_ADDR_ARM + 0x600, 0x5a, "secure");
    val = readb_attrs(TEST_ADDR_ARM + 0x600, "secure");
    g_assert_cmpuint(val, ==, 0x5a);

    writel_attrs(TEST_ADDR_ARM + 0x604,
                 0xa5a5a5a5, "space=realm");
    g_assert_cmphex(readl_attrs(TEST_ADDR_ARM + 0x604, "space=realm"), ==,
                    0xa5a5a5a5U);

    memwrite_attrs(TEST_ADDR_ARM + 0x608,
                   wbuf, sizeof(wbuf), "space=non-secure");
    memread_attrs(TEST_ADDR_ARM + 0x608,
                  rbuf, sizeof(rbuf), NULL);
    g_assert(memcmp(wbuf, rbuf, sizeof(wbuf)) == 0);

    qtest_end();
}

static void test_x86_scalar_attrs(void)
{
    QTestState *qts;
    uint8_t val;

    if (!qtest_has_machine("pc")) {
        g_test_skip("pc machine not available");
        return;
    }

    qts = qtest_init(X86_MACHINE_ARGS);

    qtest_writeb_attrs(qts, TEST_ADDR_X86, 0x11, NULL);
    val = qtest_readb_attrs(qts, TEST_ADDR_X86, NULL);
    g_assert_cmpuint(val, ==, 0x11);

    qtest_writeb_attrs(qts, TEST_ADDR_X86 + 0x1, 0xaa, "secure");
    val = qtest_readb_attrs(qts, TEST_ADDR_X86 + 0x1, "secure");
    g_assert_cmpuint(val, ==, 0xaa);

    qtest_quit(qts);
}

static void test_x86_bulk_attrs(void)
{
    QTestState *qts;
    uint8_t wbuf[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    uint8_t rbuf[8];
    size_t i;

    if (!qtest_has_machine("pc")) {
        g_test_skip("pc machine not available");
        return;
    }

    qts = qtest_init(X86_MACHINE_ARGS);

    qtest_memwrite_attrs(qts, TEST_ADDR_X86 + 0x100, wbuf, sizeof(wbuf), NULL);
    qtest_memread_attrs(qts, TEST_ADDR_X86 + 0x100, rbuf, sizeof(rbuf), NULL);
    g_assert(memcmp(wbuf, rbuf, sizeof(wbuf)) == 0);

    qtest_memwrite_attrs(qts, TEST_ADDR_X86 + 0x180,
                         wbuf, sizeof(wbuf), "secure");
    qtest_memread_attrs(qts, TEST_ADDR_X86 + 0x180,
                        rbuf, sizeof(rbuf), "secure");
    g_assert(memcmp(wbuf, rbuf, sizeof(wbuf)) == 0);

    qtest_memset_attrs(qts, TEST_ADDR_X86 + 0x200,
                       0x3c, sizeof(rbuf), "secure");
    qtest_memread_attrs(qts, TEST_ADDR_X86 + 0x200,
                        rbuf, sizeof(rbuf), "secure");
    for (i = 0; i < sizeof(rbuf); i++) {
        g_assert_cmpuint(rbuf[i], ==, 0x3c);
    }

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/qtest/arm/attrs/scalar", test_arm_scalar_attrs);
    qtest_add_func("/qtest/arm/attrs/bulk", test_arm_bulk_attrs);
    qtest_add_func("/qtest/arm/attrs/single_shortcuts",
                   test_arm_single_shortcuts_attrs);

    qtest_add_func("/qtest/x86/attrs/scalar", test_x86_scalar_attrs);
    qtest_add_func("/qtest/x86/attrs/bulk", test_x86_bulk_attrs);

    return g_test_run();
}
