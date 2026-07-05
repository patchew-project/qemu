/*
 * QTest testcase for the RP2040 XIP/SSI block.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/bitops.h"

#define XIP_BASE     0x10000000
#define XIP_NOCACHE_NOALLOC_BASE 0x13000000
#define XIP_CTRL_BASE 0x14000000
#define XIP_AUX_BASE 0x50400000
#define XIP_STAT     0x08
#define XIP_STREAM_ADDR 0x14
#define XIP_STREAM_CTR  0x18
#define XIP_STREAM_FIFO 0x1c
#define XIP_SSI_BASE 0x18000000
#define SSI_CTRLR1   0x04
#define SSI_SSIENR   0x08
#define SSI_DR0      0x60
#define SSI_SER      0x10
#define SSI_DMACR    0x4c

#define XIP_STAT_FIFO_FULL  BIT(2)
#define XIP_STAT_FIFO_EMPTY BIT(1)
#define XIP_STAT_FLUSH_READY BIT(0)

#define DMA_BASE                0x50000000
#define DMA_CH_READ_ADDR        0x00
#define DMA_CH_WRITE_ADDR       0x04
#define DMA_CH_TRANS_COUNT      0x08
#define DMA_CH_CTRL_TRIG        0x0c
#define DMA_CTRL_BUSY           BIT(24)
#define DMA_CTRL_BSWAP          BIT(22)
#define DMA_CTRL_TREQ_SEL_SHIFT 15
#define DMA_CTRL_INCR_WRITE     BIT(5)
#define DMA_CTRL_DATA_SIZE_32   (2 << 2)
#define DMA_CTRL_EN             BIT(0)
#define DREQ_XIP_STREAM         37
#define DREQ_XIP_SSIRX          39
#define SRAM_BASE               0x20000000

static QTestState *rp2040_start(const char *machine_args)
{
    if (machine_args) {
        return qtest_initf("-machine raspi-pico,%s", machine_args);
    }
    return qtest_init("-machine raspi-pico");
}

static void read_flash_uid(QTestState *qts, uint8_t *uid)
{
    int i;

    qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, 0x4b);
    qtest_readl(qts, XIP_SSI_BASE + SSI_DR0);
    for (i = 0; i < 4; i++) {
        qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, 0);
        qtest_readl(qts, XIP_SSI_BASE + SSI_DR0);
    }
    for (i = 0; i < 8; i++) {
        qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, 0);
        uid[i] = qtest_readl(qts, XIP_SSI_BASE + SSI_DR0);
    }
}

static void program_flash_bytes(QTestState *qts, uint32_t off,
                                const uint8_t *buf, size_t len)
{
    size_t i;

    qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, 0x06);

    qtest_writel(qts, XIP_SSI_BASE + SSI_SER, 1);
    qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, 0x02);
    qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, extract32(off, 16, 8));
    qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, extract32(off, 8, 8));
    qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, extract32(off, 0, 8));
    for (i = 0; i < len; i++) {
        qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, buf[i]);
    }
    qtest_writel(qts, XIP_SSI_BASE + SSI_SER, 0);

    qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, 0x05);
    qtest_readl(qts, XIP_SSI_BASE + SSI_DR0);
}

static void test_flash_uid_default(void)
{
    static const uint8_t expected[] = {
        0x3e, 0xb8, 0xa7, 0x49, 0x3f, 0xcc, 0x06, 0x08,
    };
    QTestState *qts = rp2040_start(NULL);
    uint8_t uid[8];

    read_flash_uid(qts, uid);
    g_assert_cmpmem(uid, sizeof(uid), expected, sizeof(expected));

    qtest_quit(qts);
}

static void test_flash_uid_machine_option(void)
{
    static const uint8_t expected[] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
    };
    QTestState *qts = rp2040_start("flash-uid=0011223344556677");
    uint8_t uid[8];

    read_flash_uid(qts, uid);
    g_assert_cmpmem(uid, sizeof(uid), expected, sizeof(expected));

    qtest_quit(qts);
}

static void test_stream_fifo_and_dma(void)
{
    static const uint32_t expected[] = {
        0x03020100, 0x07060504, 0x0b0a0908, 0x0f0e0d0c,
        0x13121110, 0x17161514,
    };
    QTestState *qts = rp2040_start(NULL);
    uint32_t ctrl;
    uint32_t buf[ARRAY_SIZE(expected)];

    program_flash_bytes(qts, 0, (const uint8_t *)expected, sizeof(expected));

    qtest_writel(qts, XIP_CTRL_BASE + XIP_STREAM_ADDR, XIP_BASE);
    qtest_writel(qts, XIP_CTRL_BASE + XIP_STREAM_CTR, ARRAY_SIZE(expected));

    g_assert_cmphex(qtest_readl(qts, XIP_CTRL_BASE + XIP_STAT) &
                    (XIP_STAT_FLUSH_READY | XIP_STAT_FIFO_FULL), ==,
                    XIP_STAT_FLUSH_READY | XIP_STAT_FIFO_FULL);
    g_assert_cmphex(qtest_readl(qts, XIP_CTRL_BASE + XIP_STREAM_FIFO), ==,
                    expected[0]);
    g_assert_cmphex(qtest_readl(qts, XIP_CTRL_BASE + XIP_STREAM_FIFO), ==,
                    expected[1]);

    ctrl = DMA_CTRL_EN | DMA_CTRL_INCR_WRITE | DMA_CTRL_DATA_SIZE_32 |
           (DREQ_XIP_STREAM << DMA_CTRL_TREQ_SEL_SHIFT);
    qtest_writel(qts, DMA_BASE + DMA_CH_READ_ADDR, XIP_AUX_BASE);
    qtest_writel(qts, DMA_BASE + DMA_CH_WRITE_ADDR, SRAM_BASE);
    qtest_writel(qts, DMA_BASE + DMA_CH_TRANS_COUNT, ARRAY_SIZE(expected) - 2);
    qtest_writel(qts, DMA_BASE + DMA_CH_CTRL_TRIG, ctrl);

    qtest_memread(qts, SRAM_BASE, buf,
                  sizeof(buf[0]) * (ARRAY_SIZE(expected) - 2));
    g_assert_cmpmem(buf, sizeof(buf[0]) * (ARRAY_SIZE(expected) - 2),
                    expected + 2,
                    sizeof(expected[0]) * (ARRAY_SIZE(expected) - 2));
    g_assert_cmphex(qtest_readl(qts, DMA_BASE + DMA_CH_CTRL_TRIG) &
                    DMA_CTRL_BUSY, ==, 0);
    g_assert_cmphex(qtest_readl(qts, XIP_CTRL_BASE + XIP_STREAM_CTR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, XIP_CTRL_BASE + XIP_STAT) &
                    XIP_STAT_FIFO_EMPTY, ==, XIP_STAT_FIFO_EMPTY);

    qtest_quit(qts);
}

static void test_ssi_rx_dma_bulk_read(void)
{
    static const uint32_t expected[] = {
        0x03020100, 0x07060504, 0x0b0a0908, 0x0f0e0d0c,
        0x13121110, 0x17161514,
    };
    QTestState *qts = rp2040_start(NULL);
    uint32_t ctrl;
    uint32_t buf[ARRAY_SIZE(expected)];

    program_flash_bytes(qts, 0, (const uint8_t *)expected, sizeof(expected));
    g_assert_cmphex(qtest_readl(qts, XIP_NOCACHE_NOALLOC_BASE), ==,
                    expected[0]);

    qtest_writel(qts, XIP_SSI_BASE + SSI_SSIENR, 0);
    qtest_writel(qts, XIP_SSI_BASE + SSI_CTRLR1, ARRAY_SIZE(expected) - 1);
    qtest_writel(qts, XIP_SSI_BASE + SSI_DMACR, 3);
    qtest_writel(qts, XIP_SSI_BASE + SSI_SSIENR, 1);

    ctrl = DMA_CTRL_EN | DMA_CTRL_INCR_WRITE | DMA_CTRL_DATA_SIZE_32 |
           DMA_CTRL_BSWAP | (DREQ_XIP_SSIRX << DMA_CTRL_TREQ_SEL_SHIFT);
    qtest_writel(qts, DMA_BASE + DMA_CH_READ_ADDR, XIP_SSI_BASE + SSI_DR0);
    qtest_writel(qts, DMA_BASE + DMA_CH_WRITE_ADDR, SRAM_BASE);
    qtest_writel(qts, DMA_BASE + DMA_CH_TRANS_COUNT, ARRAY_SIZE(expected));
    qtest_writel(qts, DMA_BASE + DMA_CH_CTRL_TRIG, ctrl);

    qtest_writel(qts, XIP_SSI_BASE + SSI_DR0, 0xa0);

    qtest_memread(qts, SRAM_BASE, buf, sizeof(buf));
    g_assert_cmpmem(buf, sizeof(buf), expected, sizeof(expected));
    g_assert_cmphex(qtest_readl(qts, DMA_BASE + DMA_CH_CTRL_TRIG) &
                    DMA_CTRL_BUSY, ==, 0);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/rp2040-xip/flash-uid-default",
                   test_flash_uid_default);
    qtest_add_func("/rp2040-xip/flash-uid-machine-option",
                   test_flash_uid_machine_option);
    qtest_add_func("/rp2040-xip/stream-fifo-and-dma",
                   test_stream_fifo_and_dma);
    qtest_add_func("/rp2040-xip/ssi-rx-dma-bulk-read",
                   test_ssi_rx_dma_bulk_read);

    return g_test_run();
}
