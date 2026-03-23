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
 * The Arm virt test uses both the default non-secure RAM at 0x4000_0000 and
 * the secure-only RAM window at 0x0e00_0000. The x86 q35 test only exercises
 * regular RAM that is visible from both the default and SMM address spaces.
 */
#define TEST_ADDR_OFFSET_NS     0x1000ULL
#define TEST_ADDR_OFFSET_S      0xe000000ULL
#define TEST_ARM_SEC_BASE       0x0ULL
#define TEST_ARM_NS_BASE        0x40000000ULL
#define TEST_X86_BASE           0x0ULL

#define TEST_ADDR_ARM_S         (TEST_ARM_SEC_BASE + TEST_ADDR_OFFSET_S)
#define TEST_ADDR_ARM_NS        (TEST_ARM_NS_BASE + TEST_ADDR_OFFSET_NS)
#define TEST_ADDR_X86           (TEST_X86_BASE + TEST_ADDR_OFFSET_NS)

#define ARM_MACHINE_ARGS        "-machine virt,secure=on -accel tcg"
#define X86_MACHINE_ARGS        "-machine q35,smm=on -m 1G -accel tcg"

static void G_GNUC_PRINTF(2, 3) assert_qtest_error(QTestState *qts,
                                                   const char *fmt, ...)
{
    va_list ap;
    g_autofree gchar *cmd = NULL;
    g_auto(GStrv) response = NULL;

    va_start(ap, fmt);
    cmd = g_strdup_vprintf(fmt, ap);
    va_end(ap);

    response = qtest_raw_cmd(qts, "%s", cmd);
    g_assert_cmpstr(response[0], ==, "ERR");
}

static void test_arm_scalar_attrs(void)
{
    QTestState *qts;
    uint8_t val;

    if (!qtest_has_machine("virt")) {
        g_test_skip("virt machine not available");
        return;
    }

    qts = qtest_init(ARM_MACHINE_ARGS);

    qtest_writeb_attrs(qts, TEST_ADDR_ARM_NS, 0x11, NULL);
    val = qtest_readb_attrs(qts, TEST_ADDR_ARM_NS, NULL);
    g_assert_cmpuint(val, ==, 0x11);

    qtest_writeb_attrs(qts, TEST_ADDR_ARM_NS + 0x1, 0x22, "space=non-secure");
    val = qtest_readb_attrs(qts, TEST_ADDR_ARM_NS + 0x1, "space=non-secure");
    g_assert_cmpuint(val, ==, 0x22);

    qtest_writeb_attrs(qts, TEST_ADDR_ARM_S + 0x2, 0x33, "secure");
    val = qtest_readb_attrs(qts, TEST_ADDR_ARM_S + 0x2, "secure");
    g_assert_cmpuint(val, ==, 0x33);

    assert_qtest_error(qts, "writeb 0x%" PRIx64 " 0x44 space=realm\n",
                       (uint64_t)(TEST_ADDR_ARM_S + 0x3));
    assert_qtest_error(qts, "readb 0x%" PRIx64 " space=realm\n",
                       (uint64_t)(TEST_ADDR_ARM_S + 0x3));

    qtest_writeb_attrs(qts, TEST_ADDR_ARM_S + 0x4, 0x55, "space=root");
    val = qtest_readb_attrs(qts, TEST_ADDR_ARM_S + 0x4, "space=root");
    g_assert_cmpuint(val, ==, 0x55);

    qtest_writeb_attrs(qts, TEST_ADDR_ARM_S + 0x5, 0x66, "space=secure");
    val = qtest_readb_attrs(qts, TEST_ADDR_ARM_S + 0x5, "space=secure");
    g_assert_cmpuint(val, ==, 0x66);

    qtest_writeb(qts, TEST_ADDR_ARM_NS + 0x6, 0x77);
    val = qtest_readb(qts, TEST_ADDR_ARM_NS + 0x6);
    g_assert_cmpuint(val, ==, 0x77);
    val = qtest_readb_attrs(qts, TEST_ADDR_ARM_NS + 0x6, "space=non-secure");
    g_assert_cmpuint(val, ==, 0x77);

    assert_qtest_error(qts, "writeb 0x%" PRIx64 " 0x77 space=non-secure\n",
                       (uint64_t)(TEST_ADDR_ARM_S + 0x7));
    assert_qtest_error(qts, "readb 0x%" PRIx64 " space=non-secure\n",
                       (uint64_t)(TEST_ADDR_ARM_S + 0x7));

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

    qtest_memwrite_attrs(qts, TEST_ADDR_ARM_NS + 0x100,
                         wbuf, sizeof(wbuf), NULL);
    qtest_memread_attrs(qts, TEST_ADDR_ARM_NS + 0x100,
                        rbuf, sizeof(rbuf), NULL);
    g_assert(memcmp(wbuf, rbuf, sizeof(wbuf)) == 0);

    qtest_memwrite_attrs(qts, TEST_ADDR_ARM_NS + 0x200,
                         wbuf, sizeof(wbuf), "space=non-secure");
    qtest_memread_attrs(qts, TEST_ADDR_ARM_NS + 0x200,
                        rbuf, sizeof(rbuf), "space=non-secure");
    g_assert(memcmp(wbuf, rbuf, sizeof(wbuf)) == 0);

    qtest_memwrite_attrs(qts, TEST_ADDR_ARM_S + 0x300,
                         wbuf, sizeof(wbuf), "secure");
    qtest_memread_attrs(qts, TEST_ADDR_ARM_S + 0x300,
                        rbuf, sizeof(rbuf), "secure");
    g_assert(memcmp(wbuf, rbuf, sizeof(wbuf)) == 0);

    qtest_memset_attrs(qts, TEST_ADDR_ARM_S + 0x400,
                       0xa5, sizeof(rbuf), "space=root");
    qtest_memread_attrs(qts, TEST_ADDR_ARM_S + 0x400,
                        rbuf, sizeof(rbuf), "space=root");
    for (i = 0; i < sizeof(rbuf); i++) {
        g_assert_cmpuint(rbuf[i], ==, 0xa5);
    }

    qtest_bufwrite_attrs(qts, TEST_ADDR_ARM_NS + 0x500,
                         wbuf, sizeof(wbuf), "space=non-secure");
    qtest_bufread_attrs(qts, TEST_ADDR_ARM_NS + 0x500,
                        rbuf, sizeof(rbuf), "space=non-secure");
    g_assert(memcmp(wbuf, rbuf, sizeof(wbuf)) == 0);

    qtest_bufwrite_attrs(qts, TEST_ADDR_ARM_S + 0x600,
                         wbuf, sizeof(wbuf), "secure");
    qtest_bufread_attrs(qts, TEST_ADDR_ARM_S + 0x600,
                        rbuf, sizeof(rbuf), "secure");
    g_assert(memcmp(wbuf, rbuf, sizeof(wbuf)) == 0);

    qtest_memwrite(qts, TEST_ADDR_ARM_NS + 0x700, wbuf, 4);
    qtest_memread(qts, TEST_ADDR_ARM_NS + 0x700, rbuf, 4);
    g_assert(memcmp(wbuf, rbuf, 4) == 0);

    qtest_memset(qts, TEST_ADDR_ARM_NS + 0x710, 0xa5, 4);
    qtest_memread(qts, TEST_ADDR_ARM_NS + 0x710, rbuf, 4);
    for (i = 0; i < 4; i++) {
        g_assert_cmpuint(rbuf[i], ==, 0xa5);
    }

    qtest_bufwrite(qts, TEST_ADDR_ARM_NS + 0x720, wbuf, 4);
    qtest_bufread(qts, TEST_ADDR_ARM_NS + 0x720, rbuf, 4);
    g_assert(memcmp(wbuf, rbuf, 4) == 0);

    assert_qtest_error(qts, "write 0x%" PRIx64 " 0x%zx 0x00112233 "
                       "space=non-secure\n",
                       (uint64_t)(TEST_ADDR_ARM_S + 0x730),
                       (size_t)4);
    assert_qtest_error(qts, "read 0x%" PRIx64 " 0x%zx space=non-secure\n",
                       (uint64_t)(TEST_ADDR_ARM_S + 0x730), (size_t)4);
    assert_qtest_error(qts, "memset 0x%" PRIx64 " 0x%zx 0xa5 "
                       "space=non-secure\n",
                       (uint64_t)(TEST_ADDR_ARM_S + 0x740),
                       (size_t)4);
    assert_qtest_error(qts, "b64write 0x%" PRIx64 " 0x%zx AQIDBA== "
                       "space=non-secure\n",
                       (uint64_t)(TEST_ADDR_ARM_S + 0x750),
                       (size_t)4);
    assert_qtest_error(qts, "b64read 0x%" PRIx64 " 0x%zx space=non-secure\n",
                       (uint64_t)(TEST_ADDR_ARM_S + 0x750), (size_t)4);

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

    writeb_attrs(TEST_ADDR_ARM_S + 0x700, 0x5a, "secure");
    val = readb_attrs(TEST_ADDR_ARM_S + 0x700, "secure");
    g_assert_cmpuint(val, ==, 0x5a);

    writel_attrs(TEST_ADDR_ARM_S + 0x704,
                 0xa5a5a5a5, "space=root");
    g_assert_cmphex(readl_attrs(TEST_ADDR_ARM_S + 0x704, "space=root"), ==,
                    0xa5a5a5a5U);

    memwrite_attrs(TEST_ADDR_ARM_NS + 0x708,
                   wbuf, sizeof(wbuf), "space=non-secure");
    memread_attrs(TEST_ADDR_ARM_NS + 0x708,
                  rbuf, sizeof(rbuf), "space=non-secure");
    g_assert(memcmp(wbuf, rbuf, sizeof(wbuf)) == 0);

    qtest_end();
}

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
    val = qtest_readb_attrs(qts, TEST_ADDR_X86, "secure");
    g_assert_cmpuint(val, ==, 0x11);

    qtest_writeb_attrs(qts, TEST_ADDR_X86 + 0x1, 0x22, "secure");
    val = qtest_readb_attrs(qts, TEST_ADDR_X86 + 0x1, "secure");
    g_assert_cmpuint(val, ==, 0x22);
    val = qtest_readb_attrs(qts, TEST_ADDR_X86 + 0x1, NULL);
    g_assert_cmpuint(val, ==, 0x22);

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
    qtest_memread_attrs(qts, TEST_ADDR_X86 + 0x100,
                        rbuf, sizeof(rbuf), "secure");
    g_assert(memcmp(wbuf, rbuf, sizeof(wbuf)) == 0);

    qtest_memwrite_attrs(qts, TEST_ADDR_X86 + 0x180,
                         wbuf, sizeof(wbuf), "secure");
    qtest_memread_attrs(qts, TEST_ADDR_X86 + 0x180,
                        rbuf, sizeof(rbuf), NULL);
    g_assert(memcmp(wbuf, rbuf, sizeof(wbuf)) == 0);

    qtest_memset_attrs(qts, TEST_ADDR_X86 + 0x200,
                       0x3c, sizeof(rbuf), "secure");
    qtest_memread_attrs(qts, TEST_ADDR_X86 + 0x200,
                        rbuf, sizeof(rbuf), NULL);
    for (i = 0; i < sizeof(rbuf); i++) {
        g_assert_cmpuint(rbuf[i], ==, 0x3c);
    }

    qtest_bufwrite_attrs(qts, TEST_ADDR_X86 + 0x280,
                         wbuf, sizeof(wbuf), NULL);
    qtest_bufread_attrs(qts, TEST_ADDR_X86 + 0x280,
                        rbuf, sizeof(rbuf), "secure");
    g_assert(memcmp(wbuf, rbuf, sizeof(wbuf)) == 0);

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
