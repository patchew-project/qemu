/*
 * QTest testcase for the ASPEED Hash and Crypto Engine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2021 IBM Corp.
 */

#include "qemu/osdep.h"

#include "libqos/libqtest.h"
#include "qemu-common.h"
#include "qemu/bitops.h"

#define HACE_BASE                0x1e6d0000

#define HACE_CMD                 0x10
#define  HACE_SHA_BE_EN                 BIT(3)
#define  HACE_MD5_LE_EN                 BIT(2)
#define  HACE_ALGO_MD5                  0
#define  HACE_ALGO_SHA1                 BIT(5)
#define  HACE_ALGO_SHA224               BIT(6)
#define  HACE_ALGO_SHA256               (BIT(4) | BIT(6))
#define  HACE_ALGO_SHA512               (BIT(5) | BIT(6))
#define  HACE_ALGO_SHA384               (BIT(5) | BIT(6) | BIT(10))
#define  HACE_SG_EN                     BIT(18)

#define HACE_STS                 (HACE_BASE + 0x1c)
#define  HACE_RSA_ISR                   BIT(13)
#define  HACE_CRYPTO_ISR                BIT(12)
#define  HACE_HASH_ISR                  BIT(9)
#define  HACE_RSA_BUSY                  BIT(2)
#define  HACE_CRYPTO_BUSY               BIT(1)
#define  HACE_HASH_BUSY                 BIT(0)
#define HACE_HASH_SRC            (HACE_BASE + 0x20)
#define HACE_HASH_DIGEST    (HACE_BASE + 0x24)
#define HACE_HASH_KEY_BUFF       (HACE_BASE + 0x28)
#define HACE_HASH_DATA_LEN       (HACE_BASE + 0x2c)
#define HACE_HASH_CMD            (HACE_BASE + 0x30)

/*
 * Test vector is the ascii "abc"
 *
 * Expected results were generated using command line utitiles:
 *
 *  echo -n -e 'abc' | dd of=/tmp/test
 *  for hash in sha512sum sha256sum md5sum; do $hash /tmp/test; done
 *
 */
static const uint8_t test_vector[] = {0x61, 0x62, 0x63};

static const uint8_t test_result_sha512[] = {
    0xdd, 0xaf, 0x35, 0xa1, 0x93, 0x61, 0x7a, 0xba, 0xcc, 0x41, 0x73, 0x49,
    0xae, 0x20, 0x41, 0x31, 0x12, 0xe6, 0xfa, 0x4e, 0x89, 0xa9, 0x7e, 0xa2,
    0x0a, 0x9e, 0xee, 0xe6, 0x4b, 0x55, 0xd3, 0x9a, 0x21, 0x92, 0x99, 0x2a,
    0x27, 0x4f, 0xc1, 0xa8, 0x36, 0xba, 0x3c, 0x23, 0xa3, 0xfe, 0xeb, 0xbd,
    0x45, 0x4d, 0x44, 0x23, 0x64, 0x3c, 0xe8, 0x0e, 0x2a, 0x9a, 0xc9, 0x4f,
    0xa5, 0x4c, 0xa4, 0x9f};

static const uint8_t test_result_sha256[] = {
    0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde,
    0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
    0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};

static const uint8_t test_result_md5[] = {
    0x90, 0x01, 0x50, 0x98, 0x3c, 0xd2, 0x4f, 0xb0, 0xd6, 0x96, 0x3f, 0x7d,
    0x28, 0xe1, 0x7f, 0x72};

static void write_regs(QTestState *s, uint64_t src, uint64_t length,
                       uint64_t out, uint32_t method)
{
        qtest_writel(s, HACE_HASH_SRC, src);
        qtest_writel(s, HACE_HASH_DIGEST, out);
        qtest_writel(s, HACE_HASH_DATA_LEN, length);
        qtest_writel(s, HACE_HASH_CMD, HACE_SHA_BE_EN | method);
}

static void test_md5(void)
{
    QTestState *s = qtest_init("-machine ast2600-evb");

    uint64_t src_addr = 0x80000000;
    uint64_t digest_addr = 0x81000000;
    uint8_t digest[16] = {0};

    /* Check engine is idle, no busy or irq bits set */
    g_assert_cmphex(qtest_readl(s, HACE_STS), ==, 0);

    /* Write test vector into memory */
    qtest_memwrite(s, src_addr, test_vector, sizeof(test_vector));

    write_regs(s, src_addr, sizeof(test_vector), digest_addr, HACE_ALGO_MD5);

    /* Check hash IRQ status is asserted */
    g_assert_cmphex(qtest_readl(s, HACE_STS), ==, 0x00000200);

    /* Clear IRQ status and check status is deasserted */
    qtest_writel(s, HACE_STS, 0x00000200);
    g_assert_cmphex(qtest_readl(s, HACE_STS), ==, 0);

    /* Read computed digest from memory */
    qtest_memread(s, digest_addr, digest, sizeof(digest));

    /* Check result of computation */
    g_assert_cmpmem(digest, sizeof(digest),
                    test_result_md5, sizeof(digest));
}

static void test_sha256(void)
{
    QTestState *s = qtest_init("-machine ast2600-evb");

    uint64_t src_addr = 0x80000000;
    uint64_t digest_addr = 0x81000000;
    uint8_t digest[32] = {0};

    /* Check engine is idle, no busy or irq bits set */
    g_assert_cmphex(qtest_readl(s, HACE_STS), ==, 0);

    /* Write test vector into memory */
    qtest_memwrite(s, src_addr, test_vector, sizeof(test_vector));

    write_regs(s, src_addr, sizeof(test_vector), digest_addr, HACE_ALGO_SHA256);

    /* Check hash IRQ status is asserted */
    g_assert_cmphex(qtest_readl(s, HACE_STS), ==, 0x00000200);

    /* Clear IRQ status and check status is deasserted */
    qtest_writel(s, HACE_STS, 0x00000200);
    g_assert_cmphex(qtest_readl(s, HACE_STS), ==, 0);

    /* Read computed digest from memory */
    qtest_memread(s, digest_addr, digest, sizeof(digest));

    /* Check result of computation */
    g_assert_cmpmem(digest, sizeof(digest),
                    test_result_sha256, sizeof(digest));
}

static void test_sha512(void)
{
    QTestState *s = qtest_init("-machine ast2600-evb");

    uint64_t src_addr = 0x80000000;
    uint64_t digest_addr = 0x81000000;
    uint8_t digest[64] = {0};

    /* Check engine is idle, no busy or irq bits set */
    g_assert_cmphex(qtest_readl(s, HACE_STS), ==, 0);

    /* Write test vector into memory */
    qtest_memwrite(s, src_addr, test_vector, sizeof(test_vector));

    write_regs(s, src_addr, sizeof(test_vector), digest_addr, HACE_ALGO_SHA512);

    /* Check hash IRQ status is asserted */
    g_assert_cmphex(qtest_readl(s, HACE_STS), ==, 0x00000200);

    /* Clear IRQ status and check status is deasserted */
    qtest_writel(s, HACE_STS, 0x00000200);
    g_assert_cmphex(qtest_readl(s, HACE_STS), ==, 0);

    /* Read computed digest from memory */
    qtest_memread(s, digest_addr, digest, sizeof(digest));

    /* Check result of computation */
    g_assert_cmpmem(digest, sizeof(digest),
                    test_result_sha512, sizeof(digest));
}

static void test_addresses(void)
{
    QTestState *s = qtest_init("-machine ast2600-evb");

    /*
     * Check command mode is zero, meaning engine is in direct access mode,
     * as this affects the masking behavior of the HASH_SRC register.
     */
    g_assert_cmphex(qtest_readl(s, HACE_CMD), ==, 0);
    g_assert_cmphex(qtest_readl(s, HACE_HASH_SRC), ==, 0);
    g_assert_cmphex(qtest_readl(s, HACE_HASH_DIGEST), ==, 0);
    g_assert_cmphex(qtest_readl(s, HACE_HASH_DATA_LEN), ==, 0);

    /* Check that the address masking is correct */
    qtest_writel(s, HACE_HASH_SRC, 0xffffffff);
    g_assert_cmphex(qtest_readl(s, HACE_HASH_SRC), ==, 0x7fffffff);

    qtest_writel(s, HACE_HASH_DIGEST, 0xffffffff);
    g_assert_cmphex(qtest_readl(s, HACE_HASH_DIGEST), ==, 0x7ffffff8);

    qtest_writel(s, HACE_HASH_DATA_LEN, 0xffffffff);
    g_assert_cmphex(qtest_readl(s, HACE_HASH_DATA_LEN), ==, 0x0fffffff);

    /* Reset to zero */
    qtest_writel(s, HACE_HASH_SRC, 0);
    qtest_writel(s, HACE_HASH_DIGEST, 0);
    qtest_writel(s, HACE_HASH_DATA_LEN, 0);

    /* Check that all bits are now zero */
    g_assert_cmphex(qtest_readl(s, HACE_HASH_SRC), ==, 0);
    g_assert_cmphex(qtest_readl(s, HACE_HASH_DIGEST), ==, 0);
    g_assert_cmphex(qtest_readl(s, HACE_HASH_DATA_LEN), ==, 0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("aspeed/hace/addresses", test_addresses);
    qtest_add_func("aspeed/hace/sha512", test_sha512);
    qtest_add_func("aspeed/hace/sha256", test_sha256);
    qtest_add_func("aspeed/hace/md5", test_md5);

    return g_test_run();
}
