/*
 * QTest for memory access with transaction attributes
 *
 * This test verifies if the qtest *_secure and *_space commands work correctly.
 *
 * Two architectures are covered:
 *
 * - ARM (virt machine, cortex-a57, secure=on):
 *     *_secure uses the ARM Secure AddressSpace (ARMASIdx_S = 1).
 *     *_space uses all four ARM security spaces (Secure/NonSecure/Root/Realm).
 *     secure=on is required so that the ARM Secure address space is initialised;
 *
 * - x86 (pc machine, TCG):
 *     *_secure uses the SMM AddressSpace (X86ASIdx_SMM = 1).
 *     On TCG, cpu_address_space_init() always creates X86ASIdx_SMM as a
 *     container that is an alias of all system memory, so no special machine
 *     flags are needed -- the SMM AS exists unconditionally under TCG.
 *     *_space commands are ARM-specific and have no x86 equivalents.
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
#include "hw/arm/arm-security.h"


/*
 * Define test addresses for ARM and x86.
 *
 * Default RAM size is 128 MiB for all architectures including "virt" machine in
 * ARM and "pc" machine in x86.
 * We define a 4 KiB size offset above the RAM base, both in ARM and x86, as the
 * test address.
 */
#define TEST_ADDR_OFFSET    0x1000ULL
#define TEST_ARM_BASE       0x40000000ULL
#define TEST_X86_BASE       0x0ULL

#define TEST_ADDR_ARM       (TEST_ARM_BASE + TEST_ADDR_OFFSET)
#define TEST_ADDR_X86       (TEST_X86_BASE + TEST_ADDR_OFFSET)

#define ARM_MACHINE_ARGS "-machine virt,secure=on -cpu cortex-a57"

/* ARM *_secure tests */

static void test_arm_writeb_readb_secure(void)
{
    QTestState *qts;
    uint8_t val;

    if (!qtest_has_machine("virt")) {
        g_test_skip("virt machine not available");
        return;
    }

    qts = qtest_init(ARM_MACHINE_ARGS);

    /* secure=0: NonSecure access */
    qtest_writeb_secure(qts, TEST_ADDR_ARM, 0x55, 0);
    val = qtest_readb_secure(qts, TEST_ADDR_ARM, 0);
    g_assert_cmpuint(val, ==, 0x55);

    /* secure=1: Secure access (ARM Secure AS) */
    qtest_writeb_secure(qts, TEST_ADDR_ARM, 0xAA, 1);
    val = qtest_readb_secure(qts, TEST_ADDR_ARM, 1);
    g_assert_cmpuint(val, ==, 0xAA);

    qtest_quit(qts);
}

static void test_arm_writew_readw_secure(void)
{
    QTestState *qts;
    uint16_t val;

    if (!qtest_has_machine("virt")) {
        g_test_skip("virt machine not available");
        return;
    }

    qts = qtest_init(ARM_MACHINE_ARGS);

    qtest_writew_secure(qts, TEST_ADDR_ARM, 0x1234, 0);
    val = qtest_readw_secure(qts, TEST_ADDR_ARM, 0);
    g_assert_cmpuint(val, ==, 0x1234);

    qtest_writew_secure(qts, TEST_ADDR_ARM, 0x1234, 1);
    val = qtest_readw_secure(qts, TEST_ADDR_ARM, 1);
    g_assert_cmpuint(val, ==, 0x1234);

    qtest_quit(qts);
}

static void test_arm_writel_readl_secure(void)
{
    QTestState *qts;
    uint32_t val;

    if (!qtest_has_machine("virt")) {
        g_test_skip("virt machine not available");
        return;
    }

    qts = qtest_init(ARM_MACHINE_ARGS);

    qtest_writel_secure(qts, TEST_ADDR_ARM, 0xDEADBEEF, 0);
    val = qtest_readl_secure(qts, TEST_ADDR_ARM, 0);
    g_assert_cmpuint(val, ==, 0xDEADBEEF);

    qtest_writel_secure(qts, TEST_ADDR_ARM, 0xDEADBEEF, 1);
    val = qtest_readl_secure(qts, TEST_ADDR_ARM, 1);
    g_assert_cmpuint(val, ==, 0xDEADBEEF);

    qtest_quit(qts);
}

static void test_arm_writeq_readq_secure(void)
{
    QTestState *qts;
    uint64_t val;

    if (!qtest_has_machine("virt")) {
        g_test_skip("virt machine not available");
        return;
    }

    qts = qtest_init(ARM_MACHINE_ARGS);

    qtest_writeq_secure(qts, TEST_ADDR_ARM, 0x123456789ABCDEF0ULL, 0);
    val = qtest_readq_secure(qts, TEST_ADDR_ARM, 0);
    g_assert_cmpuint(val, ==, 0x123456789ABCDEF0ULL);

    qtest_writeq_secure(qts, TEST_ADDR_ARM, 0x123456789ABCDEF0ULL, 1);
    val = qtest_readq_secure(qts, TEST_ADDR_ARM, 1);
    g_assert_cmpuint(val, ==, 0x123456789ABCDEF0ULL);

    qtest_quit(qts);
}

static void test_arm_memwrite_memread_secure(void)
{
    QTestState *qts;
    uint8_t wbuf[16] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                         0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF };
    uint8_t rbuf[16];

    if (!qtest_has_machine("virt")) {
        g_test_skip("virt machine not available");
        return;
    }

    qts = qtest_init(ARM_MACHINE_ARGS);

    qtest_memwrite_secure(qts, TEST_ADDR_ARM, wbuf, sizeof(wbuf), 0);
    qtest_memread_secure(qts, TEST_ADDR_ARM, rbuf, sizeof(rbuf), 0);
    g_assert(memcmp(wbuf, rbuf, sizeof(wbuf)) == 0);

    qtest_memwrite_secure(qts, TEST_ADDR_ARM, wbuf, sizeof(wbuf), 1);
    qtest_memread_secure(qts, TEST_ADDR_ARM, rbuf, sizeof(rbuf), 1);
    g_assert(memcmp(wbuf, rbuf, sizeof(wbuf)) == 0);

    qtest_quit(qts);
}

static void test_arm_memset_secure(void)
{
    QTestState *qts;
    uint8_t rbuf[16];
    size_t i;

    if (!qtest_has_machine("virt")) {
        g_test_skip("virt machine not available");
        return;
    }

    qts = qtest_init(ARM_MACHINE_ARGS);

    qtest_memset_secure(qts, TEST_ADDR_ARM, 0x42, sizeof(rbuf), 0);
    qtest_memread_secure(qts, TEST_ADDR_ARM, rbuf, sizeof(rbuf), 0);

    for (i = 0; i < sizeof(rbuf); i++) {
        g_assert_cmpuint(rbuf[i], ==, 0x42);
    }

    qtest_memset_secure(qts, TEST_ADDR_ARM, 0x42, sizeof(rbuf), 1);
    qtest_memread_secure(qts, TEST_ADDR_ARM, rbuf, sizeof(rbuf), 1);

    for (i = 0; i < sizeof(rbuf); i++) {
        g_assert_cmpuint(rbuf[i], ==, 0x42);
    }

    qtest_quit(qts);
}

/* ARM *_space tests (ARM-specific: Secure/NonSecure/Root/Realm) */

static void test_arm_writeb_readb_space(void)
{
    QTestState *qts;
    uint8_t val;

    if (!qtest_has_machine("virt")) {
        g_test_skip("virt machine not available");
        return;
    }

    qts = qtest_init(ARM_MACHINE_ARGS);

    /* NonSecure space (secure=0) */
    qtest_writeb_space(qts, TEST_ADDR_ARM, 0x11, ARMSS_NonSecure);
    val = qtest_readb_space(qts, TEST_ADDR_ARM, ARMSS_NonSecure);
    g_assert_cmpuint(val, ==, 0x11);

    /* Realm space (secure=0) */
    qtest_writeb_space(qts, TEST_ADDR_ARM, 0x33, ARMSS_Realm);
    val = qtest_readb_space(qts, TEST_ADDR_ARM, ARMSS_Realm);
    g_assert_cmpuint(val, ==, 0x33);

    /* Secure space (secure=1) */
    qtest_writeb_space(qts, TEST_ADDR_ARM, 0x22, ARMSS_Secure);
    val = qtest_readb_space(qts, TEST_ADDR_ARM, ARMSS_Secure);
    g_assert_cmpuint(val, ==, 0x22);

    /* Root space (secure=1) */
    qtest_writeb_space(qts, TEST_ADDR_ARM, 0x44, ARMSS_Root);
    val = qtest_readb_space(qts, TEST_ADDR_ARM, ARMSS_Root);
    g_assert_cmpuint(val, ==, 0x44);

    qtest_quit(qts);
}

static void test_arm_writew_readw_space(void)
{
    QTestState *qts;
    uint16_t val;

    if (!qtest_has_machine("virt")) {
        g_test_skip("virt machine not available");
        return;
    }

    qts = qtest_init(ARM_MACHINE_ARGS);

    qtest_writew_space(qts, TEST_ADDR_ARM + 0x10, 0x1122, ARMSS_NonSecure);
    val = qtest_readw_space(qts, TEST_ADDR_ARM + 0x10, ARMSS_NonSecure);
    g_assert_cmpuint(val, ==, 0x1122);

    qtest_writew_space(qts, TEST_ADDR_ARM + 0x20, 0x3344, ARMSS_Realm);
    val = qtest_readw_space(qts, TEST_ADDR_ARM + 0x20, ARMSS_Realm);
    g_assert_cmpuint(val, ==, 0x3344);

    qtest_writew_space(qts, TEST_ADDR_ARM + 0x30, 0x5566, ARMSS_Secure);
    val = qtest_readw_space(qts, TEST_ADDR_ARM + 0x30, ARMSS_Secure);
    g_assert_cmpuint(val, ==, 0x5566);

    qtest_writew_space(qts, TEST_ADDR_ARM + 0x40, 0x7788, ARMSS_Root);
    val = qtest_readw_space(qts, TEST_ADDR_ARM + 0x40, ARMSS_Root);
    g_assert_cmpuint(val, ==, 0x7788);

    qtest_quit(qts);
}

static void test_arm_writel_readl_space(void)
{
    QTestState *qts;
    uint32_t val;

    if (!qtest_has_machine("virt")) {
        g_test_skip("virt machine not available");
        return;
    }

    qts = qtest_init(ARM_MACHINE_ARGS);

    qtest_writel_space(qts, TEST_ADDR_ARM + 0x50, 0x11223344, ARMSS_NonSecure);
    val = qtest_readl_space(qts, TEST_ADDR_ARM + 0x50, ARMSS_NonSecure);
    g_assert_cmpuint(val, ==, 0x11223344);

    qtest_writel_space(qts, TEST_ADDR_ARM + 0x60, 0x55667788, ARMSS_Realm);
    val = qtest_readl_space(qts, TEST_ADDR_ARM + 0x60, ARMSS_Realm);
    g_assert_cmpuint(val, ==, 0x55667788);

    qtest_writel_space(qts, TEST_ADDR_ARM + 0x70, 0x99AABBCC, ARMSS_Secure);
    val = qtest_readl_space(qts, TEST_ADDR_ARM + 0x70, ARMSS_Secure);
    g_assert_cmpuint(val, ==, 0x99AABBCC);

    qtest_writel_space(qts, TEST_ADDR_ARM + 0x80, 0xDDEEFF00, ARMSS_Root);
    val = qtest_readl_space(qts, TEST_ADDR_ARM + 0x80, ARMSS_Root);
    g_assert_cmpuint(val, ==, 0xDDEEFF00);

    qtest_quit(qts);
}

static void test_arm_writeq_readq_space(void)
{
    QTestState *qts;
    uint64_t val;

    if (!qtest_has_machine("virt")) {
        g_test_skip("virt machine not available");
        return;
    }

    qts = qtest_init(ARM_MACHINE_ARGS);

    qtest_writeq_space(qts, TEST_ADDR_ARM + 0x90, 0x1122334455667788ULL,
                       ARMSS_NonSecure);
    val = qtest_readq_space(qts, TEST_ADDR_ARM + 0x90, ARMSS_NonSecure);
    g_assert_cmpuint(val, ==, 0x1122334455667788ULL);

    qtest_writeq_space(qts, TEST_ADDR_ARM + 0xA0, 0x99AABBCCDDEEFF00ULL,
                       ARMSS_Realm);
    val = qtest_readq_space(qts, TEST_ADDR_ARM + 0xA0, ARMSS_Realm);
    g_assert_cmpuint(val, ==, 0x99AABBCCDDEEFF00ULL);

    qtest_writeq_space(qts, TEST_ADDR_ARM + 0xB0, 0x0123456789ABCDEFULL,
                       ARMSS_Secure);
    val = qtest_readq_space(qts, TEST_ADDR_ARM + 0xB0, ARMSS_Secure);
    g_assert_cmpuint(val, ==, 0x0123456789ABCDEFULL);

    qtest_writeq_space(qts, TEST_ADDR_ARM + 0xC0, 0xFEDCBA9876543210ULL,
                       ARMSS_Root);
    val = qtest_readq_space(qts, TEST_ADDR_ARM + 0xC0, ARMSS_Root);
    g_assert_cmpuint(val, ==, 0xFEDCBA9876543210ULL);

    qtest_quit(qts);
}

static void test_arm_memwrite_memread_space(void)
{
    QTestState *qts;
    uint8_t wbuf[8] = { 0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0x07, 0x18 };
    uint8_t rbuf[8];

    if (!qtest_has_machine("virt")) {
        g_test_skip("virt machine not available");
        return;
    }

    qts = qtest_init(ARM_MACHINE_ARGS);

    qtest_memwrite_space(qts, TEST_ADDR_ARM, wbuf, sizeof(wbuf), ARMSS_NonSecure);
    qtest_memread_space(qts, TEST_ADDR_ARM, rbuf, sizeof(rbuf), ARMSS_NonSecure);
    g_assert(memcmp(wbuf, rbuf, sizeof(wbuf)) == 0);

    qtest_memwrite_space(qts, TEST_ADDR_ARM, wbuf, sizeof(wbuf), ARMSS_Realm);
    qtest_memread_space(qts, TEST_ADDR_ARM, rbuf, sizeof(rbuf), ARMSS_Realm);
    g_assert(memcmp(wbuf, rbuf, sizeof(wbuf)) == 0);


    qtest_memwrite_space(qts, TEST_ADDR_ARM, wbuf, sizeof(wbuf), ARMSS_Secure);
    qtest_memread_space(qts, TEST_ADDR_ARM, rbuf, sizeof(rbuf), ARMSS_Secure);
    g_assert(memcmp(wbuf, rbuf, sizeof(wbuf)) == 0);

    qtest_memwrite_space(qts, TEST_ADDR_ARM, wbuf, sizeof(wbuf), ARMSS_Root);
    qtest_memread_space(qts, TEST_ADDR_ARM, rbuf, sizeof(rbuf), ARMSS_Root);
    g_assert(memcmp(wbuf, rbuf, sizeof(wbuf)) == 0);

    qtest_quit(qts);
}

static void test_arm_memset_space(void)
{
    QTestState *qts;
    uint8_t rbuf[8];
    size_t i;

    if (!qtest_has_machine("virt")) {
        g_test_skip("virt machine not available");
        return;
    }

    qts = qtest_init(ARM_MACHINE_ARGS);

    qtest_memset_space(qts, TEST_ADDR_ARM, 0x99, sizeof(rbuf), ARMSS_NonSecure);
    qtest_memread_space(qts, TEST_ADDR_ARM, rbuf, sizeof(rbuf), ARMSS_NonSecure);

    for (i = 0; i < sizeof(rbuf); i++) {
        g_assert_cmpuint(rbuf[i], ==, 0x99);
    }

    qtest_memset_space(qts, TEST_ADDR_ARM, 0x99, sizeof(rbuf), ARMSS_Realm);
    qtest_memread_space(qts, TEST_ADDR_ARM, rbuf, sizeof(rbuf), ARMSS_Realm);

    for (i = 0; i < sizeof(rbuf); i++) {
        g_assert_cmpuint(rbuf[i], ==, 0x99);
    }

    qtest_memset_space(qts, TEST_ADDR_ARM, 0x99, sizeof(rbuf), ARMSS_Secure);
    qtest_memread_space(qts, TEST_ADDR_ARM, rbuf, sizeof(rbuf), ARMSS_Secure);

    for (i = 0; i < sizeof(rbuf); i++) {
        g_assert_cmpuint(rbuf[i], ==, 0x99);
    }

    qtest_memset_space(qts, TEST_ADDR_ARM, 0x99, sizeof(rbuf), ARMSS_Root);
    qtest_memread_space(qts, TEST_ADDR_ARM, rbuf, sizeof(rbuf), ARMSS_Root);

    for (i = 0; i < sizeof(rbuf); i++) {
        g_assert_cmpuint(rbuf[i], ==, 0x99);
    }

    qtest_quit(qts);
}

/* Test new *_secure / *_space API in libqtest-single.h */
static void test_arm_single_secure(void)
{
    uint8_t val;
    uint8_t wbuf[4] = { 0x10, 0x20, 0x30, 0x40 };
    uint8_t rbuf[4];

    if (!qtest_has_machine("virt")) {
        g_test_skip("virt machine not available");
        return;
    }

    qtest_start(ARM_MACHINE_ARGS);

    writeb_secure(TEST_ADDR_ARM, 0x5A, 0);
    val = readb_secure(TEST_ADDR_ARM, 0);
    g_assert_cmpuint(val, ==, 0x5A);

    memwrite_secure(TEST_ADDR_ARM + 0x80, wbuf, sizeof(wbuf), 0);
    memread_secure(TEST_ADDR_ARM + 0x80, rbuf, sizeof(rbuf), 0);
    g_assert(memcmp(wbuf, rbuf, sizeof(wbuf)) == 0);

    qtest_end();
}

static void test_arm_single_space(void)
{
    uint32_t val;

    if (!qtest_has_machine("virt")) {
        g_test_skip("virt machine not available");
        return;
    }

    qtest_start(ARM_MACHINE_ARGS);

    writel_space(TEST_ADDR_ARM + 0x400, 0xA5A5A5A5, ARMSS_NonSecure);
    val = readl_space(TEST_ADDR_ARM + 0x400, ARMSS_NonSecure);
    g_assert_cmpuint(val, ==, 0xA5A5A5A5);

    writel_space(TEST_ADDR_ARM + 0x404, 0x1A2B3C4D, ARMSS_Realm);
    val = readl_space(TEST_ADDR_ARM + 0x404, ARMSS_Realm);
    g_assert_cmpuint(val, ==, 0x1A2B3C4D);

    writel_space(TEST_ADDR_ARM + 0x408, 0x55667788, ARMSS_Secure);
    val = readl_space(TEST_ADDR_ARM + 0x408, ARMSS_Secure);
    g_assert_cmpuint(val, ==, 0x55667788);

    writel_space(TEST_ADDR_ARM + 0x40C, 0xCCDDEEFF, ARMSS_Root);
    val = readl_space(TEST_ADDR_ARM + 0x40C, ARMSS_Root);
    g_assert_cmpuint(val, ==, 0xCCDDEEFF);

    qtest_end();
}

#define X86_MACHINE_ARGS  "-machine pc -accel tcg"

/* x86 *_secure tests */
static void test_x86_writeb_readb_secure(void)
{
    QTestState *qts;
    uint8_t val;

    if (!qtest_has_machine("pc")) {
        g_test_skip("pc machine not available");
        return;
    }

    qts = qtest_init(X86_MACHINE_ARGS);

    /* secure=0: normal memory access (X86ASIdx_MEM) */
    qtest_writeb_secure(qts, TEST_ADDR_X86, 0x55, 0);
    val = qtest_readb_secure(qts, TEST_ADDR_X86, 0);
    g_assert_cmpuint(val, ==, 0x55);

    /* secure=1: SMM address space (X86ASIdx_SMM) */
    qtest_writeb_secure(qts, TEST_ADDR_X86, 0xAA, 1);
    val = qtest_readb_secure(qts, TEST_ADDR_X86, 1);
    g_assert_cmpuint(val, ==, 0xAA);

    qtest_quit(qts);
}

static void test_x86_writew_readw_secure(void)
{
    QTestState *qts;
    uint16_t val;

    if (!qtest_has_machine("pc")) {
        g_test_skip("pc machine not available");
        return;
    }

    qts = qtest_init(X86_MACHINE_ARGS);

    qtest_writew_secure(qts, TEST_ADDR_X86, 0x1234, 0);
    val = qtest_readw_secure(qts, TEST_ADDR_X86, 0);
    g_assert_cmpuint(val, ==, 0x1234);

    qtest_writew_secure(qts, TEST_ADDR_X86, 0x5678, 1);
    val = qtest_readw_secure(qts, TEST_ADDR_X86, 1);
    g_assert_cmpuint(val, ==, 0x5678);

    qtest_quit(qts);
}

static void test_x86_writel_readl_secure(void)
{
    QTestState *qts;
    uint32_t val;

    if (!qtest_has_machine("pc")) {
        g_test_skip("pc machine not available");
        return;
    }

    qts = qtest_init(X86_MACHINE_ARGS);

    qtest_writel_secure(qts, TEST_ADDR_X86, 0xDEADBEEF, 0);
    val = qtest_readl_secure(qts, TEST_ADDR_X86, 0);
    g_assert_cmpuint(val, ==, 0xDEADBEEF);

    qtest_writel_secure(qts, TEST_ADDR_X86, 0xCAFEBABE, 1);
    val = qtest_readl_secure(qts, TEST_ADDR_X86, 1);
    g_assert_cmpuint(val, ==, 0xCAFEBABE);

    qtest_quit(qts);
}

static void test_x86_writeq_readq_secure(void)
{
    QTestState *qts;
    uint64_t val;

    if (!qtest_has_machine("pc")) {
        g_test_skip("pc machine not available");
        return;
    }

    qts = qtest_init(X86_MACHINE_ARGS);

    qtest_writeq_secure(qts, TEST_ADDR_X86, 0x123456789ABCDEF0ULL, 0);
    val = qtest_readq_secure(qts, TEST_ADDR_X86, 0);
    g_assert_cmpuint(val, ==, 0x123456789ABCDEF0ULL);

    qtest_writeq_secure(qts, TEST_ADDR_X86, 0xFEDCBA9876543210ULL, 1);
    val = qtest_readq_secure(qts, TEST_ADDR_X86, 1);
    g_assert_cmpuint(val, ==, 0xFEDCBA9876543210ULL);

    qtest_quit(qts);
}

static void test_x86_memwrite_memread_secure(void)
{
    QTestState *qts;
    uint8_t wbuf[16] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                         0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF };
    uint8_t rbuf[16];

    if (!qtest_has_machine("pc")) {
        g_test_skip("pc machine not available");
        return;
    }

    qts = qtest_init(X86_MACHINE_ARGS);

    qtest_memwrite_secure(qts, TEST_ADDR_X86, wbuf, sizeof(wbuf), 0);
    qtest_memread_secure(qts, TEST_ADDR_X86, rbuf, sizeof(rbuf), 0);
    g_assert(memcmp(wbuf, rbuf, sizeof(wbuf)) == 0);

    qtest_memwrite_secure(qts, TEST_ADDR_X86 + 0x100, wbuf, sizeof(wbuf), 1);
    qtest_memread_secure(qts, TEST_ADDR_X86 + 0x100, rbuf, sizeof(rbuf), 1);
    g_assert(memcmp(wbuf, rbuf, sizeof(wbuf)) == 0);

    qtest_quit(qts);
}

static void test_x86_memset_secure(void)
{
    QTestState *qts;
    uint8_t rbuf[16];
    size_t i;

    if (!qtest_has_machine("pc")) {
        g_test_skip("pc machine not available");
        return;
    }

    qts = qtest_init(X86_MACHINE_ARGS);

    qtest_memset_secure(qts, TEST_ADDR_X86, 0x42, sizeof(rbuf), 0);
    qtest_memread_secure(qts, TEST_ADDR_X86, rbuf, sizeof(rbuf), 0);
    for (i = 0; i < sizeof(rbuf); i++) {
        g_assert_cmpuint(rbuf[i], ==, 0x42);
    }

    qtest_memset_secure(qts, TEST_ADDR_X86 + 0x100, 0xBE, sizeof(rbuf), 1);
    qtest_memread_secure(qts, TEST_ADDR_X86 + 0x100, rbuf, sizeof(rbuf), 1);
    for (i = 0; i < sizeof(rbuf); i++) {
        g_assert_cmpuint(rbuf[i], ==, 0xBE);
    }

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    /* ARM *_secure tests (secure/non-secure, requires secure=on) */
    qtest_add_func("/qtest/arm/secure/writeb_readb",
                   test_arm_writeb_readb_secure);
    qtest_add_func("/qtest/arm/secure/writew_readw",
                   test_arm_writew_readw_secure);
    qtest_add_func("/qtest/arm/secure/writel_readl",
                   test_arm_writel_readl_secure);
    qtest_add_func("/qtest/arm/secure/writeq_readq",
                   test_arm_writeq_readq_secure);
    qtest_add_func("/qtest/arm/secure/memwrite_memread",
                   test_arm_memwrite_memread_secure);
    qtest_add_func("/qtest/arm/secure/memset",
                   test_arm_memset_secure);

    /* ARM *_space tests (Secure/NonSecure/Root/Realm, requires secure=on) */
    qtest_add_func("/qtest/arm/space/writeb_readb",
                   test_arm_writeb_readb_space);
    qtest_add_func("/qtest/arm/space/writew_readw",
                   test_arm_writew_readw_space);
    qtest_add_func("/qtest/arm/space/writel_readl",
                   test_arm_writel_readl_space);
    qtest_add_func("/qtest/arm/space/writeq_readq",
                   test_arm_writeq_readq_space);
    qtest_add_func("/qtest/arm/space/memwrite_memread",
                   test_arm_memwrite_memread_space);
    qtest_add_func("/qtest/arm/space/memset",
                   test_arm_memset_space);
    qtest_add_func("/qtest/arm/secure/single_shortcuts",
                   test_arm_single_secure);
    qtest_add_func("/qtest/arm/space/single_shortcuts",
                   test_arm_single_space);

    /* x86 *_secure tests (SMM address space, X86ASIdx_SMM = 1) */
    qtest_add_func("/qtest/x86/secure/writeb_readb",
                   test_x86_writeb_readb_secure);
    qtest_add_func("/qtest/x86/secure/writew_readw",
                   test_x86_writew_readw_secure);
    qtest_add_func("/qtest/x86/secure/writel_readl",
                   test_x86_writel_readl_secure);
    qtest_add_func("/qtest/x86/secure/writeq_readq",
                   test_x86_writeq_readq_secure);
    qtest_add_func("/qtest/x86/secure/memwrite_memread",
                   test_x86_memwrite_memread_secure);
    qtest_add_func("/qtest/x86/secure/memset",
                   test_x86_memset_secure);

    return g_test_run();
}
