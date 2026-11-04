/*
 * QTest testcase for the SiFive L2 cache controller
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
#include "migration/migration-qmp.h"
#include "migration/migration-util.h"

#define L2CC_BASE                       0x02010000
#define L2CC_CONFIG                     0x000
#define L2CC_CONFIG_VALUE               0x06091004
#define L2CC_WAY_ENABLE                 0x008
#define L2CC_ECC_INJECT                 0x040
#define L2CC_FLUSH64                    0x200
#define L2CC_WAY_MASK_BASE              0x800
#define L2CC_WAY_MASK_COUNT             15
#define L2CC_MAX_MASTERS                21
#define L2CC_WAY_MASK(n)                (L2CC_WAY_MASK_BASE + 8 * (n))
#define L2CC_MASTER15                   L2CC_WAY_MASK(L2CC_WAY_MASK_COUNT)
#define L2CC_WAY_ENABLE_RESET           0
#define L2CC_WAY_MASK_RESET             UINT64_MAX

#define L2_LIM_BASE                     0x08000000
#define L2_WAY_SIZE                     (128 * KiB)
#define L2_LIM_RESET_WAYS               15
#define L2_ZERO_BASE                    0x0a000000
#define L2_ZERO_SIZE                    (2 * MiB)

static QTestState *sifive_l2cc_start(const char *firmware)
{
    return qtest_initf("-machine microchip-icicle-kit -smp 5 -m 2G "
                       "-bios \"%s\"", firmware);
}

static char *sifive_l2cc_create_firmware(void)
{
    static const uint8_t firmware[] = {
        0x13, 0x00, 0x00, 0x00,
        0x6f, 0x00, 0x00, 0x00,
    };
    char *path;
    int fd;
    ssize_t written;

    fd = g_file_open_tmp("sifive_l2cc_XXXXXX", &path, NULL);
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
    g_autofree char *firmware = sifive_l2cc_create_firmware();
    QTestState *qts = sifive_l2cc_start(firmware);
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
    g_autofree char *firmware = sifive_l2cc_create_firmware();
    uint32_t original[L2_LIM_RESET_WAYS];
    const uint32_t poison = 0xdeadc0de;
    const uint64_t zero_last = L2_ZERO_BASE + L2_ZERO_SIZE - 4;
    QTestState *qts = sifive_l2cc_start(firmware);
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
    g_assert_cmphex(qtest_readl(qts, zero_last), ==, 0xa55a9669);

    qtest_quit(qts);
}

static void test_l2cc_master_count(void)
{
    g_autofree char *firmware = sifive_l2cc_create_firmware();
    QTestState *qts = qtest_initf(
        "-machine sifive_u -smp 5 -bios \"%s\"", firmware);
    const uint64_t zero_last = L2_ZERO_BASE + L2_ZERO_SIZE - 4;
    unsigned int i;

    unlink(firmware);

    for (i = 0; i < L2CC_MAX_MASTERS; i++) {
        g_assert_cmphex(qtest_readq(qts, L2CC_BASE + L2CC_WAY_MASK(i)), ==,
                        L2CC_WAY_MASK_RESET);
        qtest_writeq(qts, L2CC_BASE + L2CC_WAY_MASK(i),
                     l2cc_way_mask_test_value(i));
    }
    for (i = 0; i < L2CC_MAX_MASTERS; i++) {
        g_assert_cmphex(qtest_readq(qts, L2CC_BASE + L2CC_WAY_MASK(i)), ==,
                        l2cc_way_mask_test_value(i));
    }
    g_assert_cmphex(qtest_readq(qts,
                                L2CC_BASE + L2CC_WAY_MASK(L2CC_MAX_MASTERS)),
                    ==, 0);
    qtest_writel(qts, zero_last, 0x12345678);
    qtest_writel(qts, L2_ZERO_BASE + L2_ZERO_SIZE, 0xdeadc0de);
    g_assert_cmphex(qtest_readl(qts, L2_ZERO_BASE + L2_ZERO_SIZE), ==, 0);
    qtest_writeb(qts, L2CC_BASE + L2CC_WAY_ENABLE, 15);
    g_assert_cmphex(qtest_readl(qts, zero_last), ==, 0x12345678);
    g_assert_cmphex(qtest_readl(qts, L2_LIM_BASE), ==, 0);

    qtest_system_reset(qts);
    for (i = 0; i < L2CC_MAX_MASTERS; i++) {
        g_assert_cmphex(qtest_readq(qts, L2CC_BASE + L2CC_WAY_MASK(i)), ==,
                        L2CC_WAY_MASK_RESET);
    }
    qtest_quit(qts);
}

static void test_l2cc_migration(void)
{
    g_autofree char *firmware = sifive_l2cc_create_firmware();
    QTestState *from = sifive_l2cc_start(firmware);
    QTestState *to = qtest_initf(
        "-machine microchip-icicle-kit -smp 5 -m 2G "
        "-bios \"%s\" -incoming defer", firmware);
    const uint64_t hidden = L2_LIM_BASE + 8 * L2_WAY_SIZE;
    const uint64_t zero_last = L2_ZERO_BASE + L2_ZERO_SIZE - 4;

    unlink(firmware);

    qtest_writel(from, L2_LIM_BASE, 0x11223344);
    qtest_writel(from, hidden, 0x55667788);
    qtest_writel(from, zero_last, 0x99aabbcc);
    qtest_writeb(from, L2CC_BASE + L2CC_WAY_ENABLE, 7);
    qtest_writeq(from, L2CC_BASE + L2CC_WAY_MASK(14), 0x1234);
    g_assert_cmphex(qtest_readl(from, hidden), ==, 0);

    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");
    migrate_qmp(from, to, NULL, NULL, "{}");
    wait_for_migration_complete(from);

    g_assert_cmphex(qtest_readb(to, L2CC_BASE + L2CC_WAY_ENABLE), ==, 7);
    g_assert_cmphex(qtest_readq(to, L2CC_BASE + L2CC_WAY_MASK(14)), ==, 0x1234);
    g_assert_cmphex(qtest_readl(to, L2_LIM_BASE), ==, 0x11223344);
    g_assert_cmphex(qtest_readl(to, hidden), ==, 0);
    qtest_writel(to, hidden, 0xdeadc0de);
    g_assert_cmphex(qtest_readl(to, zero_last), ==, 0x99aabbcc);

    qtest_system_reset(to);
    g_assert_cmphex(qtest_readb(to, L2CC_BASE + L2CC_WAY_ENABLE), ==, 0);
    g_assert_cmphex(qtest_readl(to, hidden), ==, 0x55667788);
    g_assert_cmphex(qtest_readl(to, zero_last), ==, 0x99aabbcc);

    qtest_quit(from);
    qtest_quit(to);
}

static void test_l2cc_unimplemented(void)
{
    g_autofree char *firmware = sifive_l2cc_create_firmware();
    g_autofree char *log_path = NULL;
    g_autofree char *log = NULL;
    QTestState *qts;
    int fd;

    fd = g_file_open_tmp("sifive_l2cc_unimp_XXXXXX", &log_path, NULL);
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

    if (qtest_has_machine("microchip-icicle-kit")) {
        qtest_add_func("/sifive_l2cc/registers", test_l2cc_registers);
        qtest_add_func("/sifive_l2cc/l2lim_topology", test_l2lim_topology);
        qtest_add_func("/sifive_l2cc/unimplemented", test_l2cc_unimplemented);
        qtest_add_func("/sifive_l2cc/migration", test_l2cc_migration);
    }
    if (qtest_has_machine("sifive_u")) {
        qtest_add_func("/sifive_l2cc/master_count", test_l2cc_master_count);
    }

    return g_test_run();
}
