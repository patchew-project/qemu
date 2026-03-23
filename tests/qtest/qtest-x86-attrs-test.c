/*
 * QTest for x86 memory access with transaction attributes
 *
 * Verify q35 SMM address-space access with the secure attribute.
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

#define TEST_ADDR_OFFSET_NS     0x1000ULL
#define TEST_X86_BASE           0x0ULL
#define TEST_X86_SMM_BASE       0xfef00000ULL

#define TEST_ADDR_X86           (TEST_X86_BASE + TEST_ADDR_OFFSET_NS)

#define X86_MACHINE_ARGS        "-machine q35,smm=on -m 1G -accel tcg " \
                                "-global mch.x-smm-test-ram=on"

static void test_x86_scalar_attrs(void)
{
    QTestState *qts;
    uint8_t val;

    if (!qtest_has_machine("q35")) {
        g_test_skip("q35 machine not available");
        return;
    }

    qts = qtest_init(X86_MACHINE_ARGS);

    qtest_writeb_attrs(qts, TEST_ADDR_X86, 0x11, NULL);
    val = qtest_readb_attrs(qts, TEST_ADDR_X86, NULL);
    g_assert_cmpuint(val, ==, 0x11);

    qtest_writeb_attrs(qts, TEST_ADDR_X86 + 0x1, 0x22, "secure");
    val = qtest_readb_attrs(qts, TEST_ADDR_X86 + 0x1, "secure");
    g_assert_cmpuint(val, ==, 0x22);


    qtest_writeb_attrs(qts, TEST_X86_SMM_BASE + 0x2, 0x33, "secure");
    val = qtest_readb_attrs(qts, TEST_X86_SMM_BASE + 0x2, "secure");
    g_assert_cmpuint(val, ==, 0x33);

    qtest_quit(qts);
}

static void test_x86_bulk_attrs(void)
{
    QTestState *qts;
    uint8_t wbuf[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    uint8_t rbuf[8];
    size_t i;

    if (!qtest_has_machine("q35")) {
        g_test_skip("q35 machine not available");
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

    qtest_memwrite_attrs(qts, TEST_X86_SMM_BASE + 0x100,
                         wbuf, sizeof(wbuf), "secure");
    qtest_memread_attrs(qts, TEST_X86_SMM_BASE + 0x100,
                        rbuf, sizeof(rbuf), "secure");
    g_assert(memcmp(wbuf, rbuf, sizeof(wbuf)) == 0);

    qtest_memset_attrs(qts, TEST_X86_SMM_BASE + 0x120,
                       0x5a, sizeof(rbuf), "secure");
    qtest_memread_attrs(qts, TEST_X86_SMM_BASE + 0x120,
                        rbuf, sizeof(rbuf), "secure");
    for (i = 0; i < sizeof(rbuf); i++) {
        g_assert_cmpuint(rbuf[i], ==, 0x5a);
    }

    qtest_bufwrite_attrs(qts, TEST_X86_SMM_BASE + 0x200,
                         wbuf, sizeof(wbuf), "secure");
    qtest_bufread_attrs(qts, TEST_X86_SMM_BASE + 0x200,
                        rbuf, sizeof(rbuf), "secure");
    g_assert(memcmp(wbuf, rbuf, sizeof(wbuf)) == 0);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/qtest/x86/attrs/scalar", test_x86_scalar_attrs);
    qtest_add_func("/qtest/x86/attrs/bulk", test_x86_bulk_attrs);

    return g_test_run();
}
