/*
 * QTest testcase for K230 GZIP decompression engine
 *
 * Copyright (c) 2026 Tao Ding <dingtao0430@163.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "libqtest.h"
#include "hw/dma/k230_gsdma.h"
#include "hw/misc/k230_decomp_gzip.h"

#define K230_DECOMP_GZIP_BASE 0x80808000
#define K230_GSDMA_BASE       0x80800000
#define K230_SRAM_BASE        0x80200000
#define TEST_LLT_ADDR0        0x01000000
#define TEST_LLT_ADDR1        0x01000040
#define TEST_SRC_ADDR         0x01100000
#define TEST_DST_ADDR         0x01200000

#define TEST_PAYLOAD_LEN          (sizeof(test_payload) - 1)

static inline uint64_t gzip_reg(hwaddr off)
{
    return K230_DECOMP_GZIP_BASE + off;
}

static inline uint64_t gsdma_reg(hwaddr off)
{
    return K230_GSDMA_BASE + off;
}

static inline uint64_t gsdma_ch_reg(unsigned int ch, hwaddr off)
{
    return K230_GSDMA_BASE + K230_GSDMA_CH_BASE +
           ch * K230_GSDMA_CH_STRIDE + off;
}

static inline hwaddr k230_sram_addr(hwaddr off)
{
    return K230_SRAM_BASE + off;
}

static inline hwaddr k230_sram_input_addr(unsigned int slot)
{
    return k230_sram_addr(K230_DECOMP_GZIP_SRAM_IN_BASE +
                          slot * K230_DECOMP_GZIP_BLOCK_SIZE);
}

static const uint8_t test_gzip_data[] = {
    0x1f, 0x8b, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0xff, 0xf3, 0x36, 0x32, 0x36, 0x50, 0x48,
    0xaf, 0xca, 0x2c, 0x50, 0x28, 0x2e, 0x29, 0x4a,
    0x4d, 0xcc, 0xcd, 0xcc, 0x4b, 0x57, 0x28, 0x49,
    0x2d, 0x2e, 0xe1, 0xf2, 0x1e, 0x5a, 0x12, 0x00,
    0xdb, 0x9d, 0xbc, 0xdd, 0xc8, 0x00, 0x00, 0x00,
};

static const uint8_t test_payload[] =
    "K230 gzip streaming test\n"
    "K230 gzip streaming test\n"
    "K230 gzip streaming test\n"
    "K230 gzip streaming test\n"
    "K230 gzip streaming test\n"
    "K230 gzip streaming test\n"
    "K230 gzip streaming test\n"
    "K230 gzip streaming test\n";

static void write_sdma_llt_node(QTestState *qts, hwaddr addr, uint32_t src,
                                uint32_t dst, uint32_t len, uint32_t next)
{
    qtest_writel(qts, addr + offsetof(K230GSDMALLT, cfg), 0);
    qtest_writel(qts, addr + offsetof(K230GSDMALLT, src_addr), src);
    qtest_writel(qts, addr + offsetof(K230GSDMALLT, line_size), len);
    qtest_writel(qts, addr + offsetof(K230GSDMALLT, line_cfg), 0x1);
    qtest_writel(qts, addr + offsetof(K230GSDMALLT, dst_addr), dst);
    qtest_writel(qts, addr + offsetof(K230GSDMALLT, next_llt_addr), next);
}

static void test_reset_and_rw(void)
{
    QTestState *qts = qtest_init("-machine k230");

    g_assert_cmphex(qtest_readl(qts, gzip_reg(K230_DECOMP_GZIP_DECOMP_START)),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, gzip_reg(K230_DECOMP_GZIP_GZIP_SRC_SIZE)),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, gzip_reg(K230_DECOMP_GZIP_GZIP_OUT_SIZE)),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, gzip_reg(K230_DECOMP_GZIP_DECOMP_STAT)),
                    ==, 0);

    qtest_writel(qts, gzip_reg(K230_DECOMP_GZIP_DECOMP_START),
                 K230_DECOMP_GZIP_START);

    qtest_writel(qts, gzip_reg(K230_DECOMP_GZIP_GZIP_SRC_SIZE), 0xffffffff);
    g_assert_cmphex(qtest_readl(qts, gzip_reg(K230_DECOMP_GZIP_GZIP_SRC_SIZE)),
                    ==, 0xffffffff);

    qtest_writel(qts, gzip_reg(K230_DECOMP_GZIP_GZIP_OUT_SIZE), 0x12345678);
    g_assert_cmphex(qtest_readl(qts, gzip_reg(K230_DECOMP_GZIP_GZIP_OUT_SIZE)),
                    ==, 0x12345678);

    qtest_quit(qts);
}

static void test_gsdma_handshake_flow(void)
{
    QTestState *qts = qtest_init("-machine k230");
    uint32_t stat;
    uint8_t output[TEST_PAYLOAD_LEN];

    qtest_memwrite(qts, TEST_SRC_ADDR, test_gzip_data, sizeof(test_gzip_data));
    write_sdma_llt_node(qts, TEST_LLT_ADDR0, TEST_SRC_ADDR,
                        k230_sram_input_addr(0),
                        sizeof(test_gzip_data), 0);
    write_sdma_llt_node(qts, TEST_LLT_ADDR1,
                        k230_sram_addr(K230_DECOMP_GZIP_SRAM_OUT_BASE),
                        TEST_DST_ADDR, TEST_PAYLOAD_LEN, 0);

    qtest_writel(qts, gsdma_reg(K230_GSDMA_DMA_CH_EN), BIT(0) | BIT(1));
    qtest_writel(qts, gsdma_ch_reg(0, K230_GSDMA_CH_CFG),
                 K230_GSDMA_CH0_CFG_DECOMP_CTRL_EN);
    qtest_writel(qts, gsdma_ch_reg(1, K230_GSDMA_CH_CFG), 0);
    qtest_writel(qts, gsdma_ch_reg(0, K230_GSDMA_CH_LLT_SADDR), TEST_LLT_ADDR0);
    qtest_writel(qts, gsdma_ch_reg(1, K230_GSDMA_CH_LLT_SADDR), TEST_LLT_ADDR1);
    qtest_writel(qts, gsdma_ch_reg(0, K230_GSDMA_CH_CTL), K230_GSDMA_CTL_START);
    qtest_writel(qts, gsdma_ch_reg(1, K230_GSDMA_CH_CTL), K230_GSDMA_CTL_START);

    g_assert_cmphex(qtest_readl(qts, gsdma_reg(K230_GSDMA_DMA_CH_EN)),
                    ==, BIT(0) | BIT(1));
    g_assert_cmphex(qtest_readl(qts, gsdma_ch_reg(0, K230_GSDMA_CH_CFG)) &
                    K230_GSDMA_CH0_CFG_DECOMP_CTRL_EN,
                    ==, K230_GSDMA_CH0_CFG_DECOMP_CTRL_EN);
    g_assert_cmphex(qtest_readl(qts, gsdma_ch_reg(0, K230_GSDMA_CH_LLT_SADDR)),
                    ==, TEST_LLT_ADDR0);
    g_assert_cmphex(qtest_readl(qts, gsdma_ch_reg(1, K230_GSDMA_CH_LLT_SADDR)),
                    ==, TEST_LLT_ADDR1);

    qtest_writel(qts, gzip_reg(K230_DECOMP_GZIP_GZIP_SRC_SIZE),
                 K230_DECOMP_GZIP_CTRL_EN | sizeof(test_gzip_data));
    qtest_writel(qts, gzip_reg(K230_DECOMP_GZIP_GZIP_OUT_SIZE),
                 TEST_PAYLOAD_LEN);
    qtest_writel(qts, gzip_reg(K230_DECOMP_GZIP_DECOMP_START),
                 K230_DECOMP_GZIP_START);

    stat = qtest_readl(qts, gzip_reg(K230_DECOMP_GZIP_DECOMP_STAT));
    g_assert_cmphex(stat & K230_DECOMP_GZIP_STAT_CRC_OK, ==,
                    K230_DECOMP_GZIP_STAT_CRC_OK);

    g_assert_cmphex(qtest_readl(qts,
                                gsdma_ch_reg(0, K230_GSDMA_CH_CURRENT_LLT)),
                    ==, TEST_LLT_ADDR0);
    g_assert_cmphex(qtest_readl(qts,
                                gsdma_ch_reg(1, K230_GSDMA_CH_CURRENT_LLT)),
                    ==, TEST_LLT_ADDR1);

    stat = qtest_readl(qts, gsdma_reg(K230_GSDMA_DMA_INT_STAT));
    g_assert_cmphex(stat & K230_GSDMA_SDMA_DONE_INT(0), ==,
                    K230_GSDMA_SDMA_DONE_INT(0));
    g_assert_cmphex(stat & K230_GSDMA_SDMA_DONE_INT(1), ==,
                    K230_GSDMA_SDMA_DONE_INT(1));

    qtest_memread(qts, TEST_DST_ADDR, output, sizeof(output));
    g_assert_cmpmem(output, sizeof(output), test_payload, TEST_PAYLOAD_LEN);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/k230-decomp-gzip/reset-and-rw", test_reset_and_rw);
    qtest_add_func("/k230-decomp-gzip/gsdma-handshake-flow",
                   test_gsdma_handshake_flow);

    return g_test_run();
}
