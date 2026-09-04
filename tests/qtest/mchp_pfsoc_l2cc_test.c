/*
 * QTest testcase for the Microchip PolarFire SoC L2 cache controller
 *
 * Copyright (c) 2026 Process Mission
 *
 * Author:
 *   Bin Meng <bin.meng@processmission.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "libqtest.h"

#define L2CC_BASE                       0x02010000
#define L2CC_CONFIG                     0x000
#define L2CC_CONFIG_VALUE               0x06091004
#define L2CC_WAY_ENABLE                 0x008
#define L2CC_ECC_INJECT                 0x040
#define L2CC_FLUSH64                    0x200
#define L2CC_WAY_MASK_BASE              0x800
#define L2CC_WAY_MASK_COUNT             15
#define L2CC_WAY_MASK(n)                (L2CC_WAY_MASK_BASE + 8 * (n))
#define L2CC_MASTER15                   L2CC_WAY_MASK(L2CC_WAY_MASK_COUNT)
#define L2CC_WAY_ENABLE_RESET           0
#define L2CC_WAY_MASK_RESET             UINT64_MAX

#define L2_LIM_BASE                     0x08000000
#define L2_WAY_SIZE                     (128 * KiB)
#define L2_LIM_RESET_WAYS               15
#define L2_ZERO_BASE                    0x0a000000
#define L2_ZERO_SIZE                    (2 * MiB)

static QTestState *mchp_pfsoc_l2cc_start(const char *firmware)
{
    return qtest_initf("-machine microchip-icicle-kit -smp 5 -m 2G "
                       "-bios \"%s\"", firmware);
}

static char *mchp_pfsoc_l2cc_create_firmware(void)
{
    static const uint8_t firmware[] = {
        0x13, 0x00, 0x00, 0x00,
        0x6f, 0x00, 0x00, 0x00,
    };
    char *path;
    int fd;
    ssize_t written;

    fd = g_file_open_tmp("mchp_pfsoc_l2cc_XXXXXX", &path, NULL);
    g_assert_cmpint(fd, >=, 0);

    written = write(fd, firmware, sizeof(firmware));
    g_assert_cmpint(written, ==, sizeof(firmware));
    close(fd);

    return path;
}

static uint64_t l2cc_way_mask_test_value(unsigned int master)
{
    return UINT64_C(0xfedcba9876543201) + master;
}

static void test_l2cc_registers(void)
{
    g_autofree char *firmware = mchp_pfsoc_l2cc_create_firmware();
    QTestState *qts = mchp_pfsoc_l2cc_start(firmware);
    unsigned int i;

    unlink(firmware);

    g_assert_cmphex(qtest_readl(qts, L2CC_BASE + L2CC_CONFIG), ==,
                    L2CC_CONFIG_VALUE);
    qtest_writel(qts, L2CC_BASE + L2CC_CONFIG, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, L2CC_BASE + L2CC_CONFIG), ==,
                    L2CC_CONFIG_VALUE);

    g_assert_cmphex(qtest_readb(qts, L2CC_BASE + L2CC_WAY_ENABLE), ==,
                    L2CC_WAY_ENABLE_RESET);
    qtest_writeb(qts, L2CC_BASE + L2CC_WAY_ENABLE, 0xd);
    g_assert_cmphex(qtest_readb(qts, L2CC_BASE + L2CC_WAY_ENABLE), ==, 0xd);
    qtest_writeb(qts, L2CC_BASE + L2CC_WAY_ENABLE, 3);
    g_assert_cmphex(qtest_readb(qts, L2CC_BASE + L2CC_WAY_ENABLE), ==, 0xd);
    qtest_writeq(qts, L2CC_BASE + L2CC_WAY_ENABLE, 0xf);
    g_assert_cmphex(qtest_readq(qts, L2CC_BASE + L2CC_WAY_ENABLE), ==, 0xf);
    qtest_writeb(qts, L2CC_BASE + L2CC_WAY_ENABLE, 0xe);
    g_assert_cmphex(qtest_readb(qts, L2CC_BASE + L2CC_WAY_ENABLE), ==, 0xf);

    for (i = 0; i < L2CC_WAY_MASK_COUNT; i++) {
        g_assert_cmphex(qtest_readq(qts, L2CC_BASE + L2CC_WAY_MASK(i)), ==,
                        L2CC_WAY_MASK_RESET);
        qtest_writeq(qts, L2CC_BASE + L2CC_WAY_MASK(i),
                     l2cc_way_mask_test_value(i));
    }
    for (i = 0; i < L2CC_WAY_MASK_COUNT; i++) {
        g_assert_cmphex(qtest_readq(qts, L2CC_BASE + L2CC_WAY_MASK(i)), ==,
                        l2cc_way_mask_test_value(i));
    }

    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts, L2CC_BASE + L2CC_CONFIG), ==,
                    L2CC_CONFIG_VALUE);
    g_assert_cmphex(qtest_readb(qts, L2CC_BASE + L2CC_WAY_ENABLE), ==,
                    L2CC_WAY_ENABLE_RESET);
    for (i = 0; i < L2CC_WAY_MASK_COUNT; i++) {
        g_assert_cmphex(qtest_readq(qts, L2CC_BASE + L2CC_WAY_MASK(i)), ==,
                        L2CC_WAY_MASK_RESET);
    }

    qtest_quit(qts);
}

static void test_l2lim_topology(void)
{
    g_autofree char *firmware = mchp_pfsoc_l2cc_create_firmware();
    uint32_t original[L2_LIM_RESET_WAYS];
    const uint32_t poison = 0xdeadc0de;
    const uint64_t zero_last = L2_ZERO_BASE + L2_ZERO_SIZE - 4;
    QTestState *qts = mchp_pfsoc_l2cc_start(firmware);
    unsigned int i;

    unlink(firmware);

    /* Check the fixed aperture, not the RAM stand-in's eviction behavior */
    qtest_writel(qts, zero_last, 0xa55a9669);
    g_assert_cmphex(qtest_readl(qts, zero_last), ==, 0xa55a9669);

    for (i = 1; i <= L2_LIM_RESET_WAYS; i++) {
        uint64_t l2lim_size = (L2_LIM_RESET_WAYS - i) * L2_WAY_SIZE;
        uint64_t hidden = L2_LIM_BASE + l2lim_size;
        uint32_t tail = 0x96000000 | i;

        original[i - 1] = 0x69000000 | i;
        qtest_writel(qts, hidden, original[i - 1]);
        g_assert_cmphex(qtest_readl(qts, hidden), ==, original[i - 1]);

        if (l2lim_size) {
            qtest_writel(qts, hidden - 4, tail);
            g_assert_cmphex(qtest_readl(qts, hidden - 4), ==, tail);
        }

        qtest_writeb(qts, L2CC_BASE + L2CC_WAY_ENABLE, i);
        g_assert_cmphex(qtest_readb(qts,
                                   L2CC_BASE + L2CC_WAY_ENABLE), ==, i);
        if (l2lim_size) {
            g_assert_cmphex(qtest_readl(qts, hidden - 4), ==, tail);
        }
        g_assert_cmphex(qtest_readl(qts, hidden), ==, 0);
        qtest_writel(qts, hidden, poison);
        g_assert_cmphex(qtest_readl(qts, hidden), ==, 0);
        g_assert_cmphex(qtest_readl(qts, zero_last), ==, 0xa55a9669);
    }

    qtest_writeb(qts, L2CC_BASE + L2CC_WAY_ENABLE, UINT8_MAX);
    g_assert_cmphex(qtest_readb(qts, L2CC_BASE + L2CC_WAY_ENABLE), ==,
                    UINT8_MAX);
    g_assert_cmphex(qtest_readl(qts, L2_LIM_BASE), ==, 0);
    qtest_writeb(qts, L2CC_BASE + L2CC_WAY_ENABLE, 3);
    g_assert_cmphex(qtest_readb(qts, L2CC_BASE + L2CC_WAY_ENABLE), ==,
                    UINT8_MAX);
    g_assert_cmphex(qtest_readl(qts, L2_LIM_BASE), ==, 0);

    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readb(qts, L2CC_BASE + L2CC_WAY_ENABLE), ==,
                    L2CC_WAY_ENABLE_RESET);
    for (i = 1; i <= L2_LIM_RESET_WAYS; i++) {
        uint64_t restored = L2_LIM_BASE +
                            (L2_LIM_RESET_WAYS - i) * L2_WAY_SIZE;

        g_assert_cmphex(qtest_readl(qts, restored), ==, original[i - 1]);
    }

    qtest_quit(qts);
}

static void test_l2cc_unimplemented(void)
{
    g_autofree char *firmware = mchp_pfsoc_l2cc_create_firmware();
    g_autofree char *log_path = NULL;
    g_autofree char *log = NULL;
    QTestState *qts;
    int fd;

    fd = g_file_open_tmp("mchp_pfsoc_l2cc_unimp_XXXXXX", &log_path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);

    qts = qtest_initf(
        "-machine microchip-icicle-kit -smp 5 -m 2G -bios \"%s\" "
        "-d unimp -D \"%s\"", firmware, log_path);
    unlink(firmware);

    g_assert_cmphex(qtest_readl(qts, L2CC_BASE + L2CC_ECC_INJECT), ==, 0);
    qtest_writeq(qts, L2CC_BASE + L2CC_FLUSH64, 0x1234);
    g_assert_cmphex(qtest_readq(qts, L2CC_BASE + L2CC_MASTER15), ==, 0);
    qtest_quit(qts);

    g_assert_true(g_file_get_contents(log_path, &log, NULL, NULL));
    g_assert_nonnull(strstr(log,
        "unimplemented device read (size 4, offset 0x40)"));
    g_assert_nonnull(strstr(log,
        "unimplemented device write (size 8, value 0x1234, offset 0x200)"));
    g_assert_nonnull(strstr(log,
        "unimplemented device read (size 8, offset 0x878)"));
    unlink(log_path);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/mchp_pfsoc_l2cc/registers", test_l2cc_registers);
    qtest_add_func("/mchp_pfsoc_l2cc/l2lim_topology",
                   test_l2lim_topology);
    qtest_add_func("/mchp_pfsoc_l2cc/unimplemented",
                   test_l2cc_unimplemented);

    return g_test_run();
}
