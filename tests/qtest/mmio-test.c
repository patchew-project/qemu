/*
 * QTest testcases for generic MMIO accesses
 *
 * Copyright (C) 2020 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "libqtest.h"
#include "qemu/bswap.h"

/* Must fit in arch address space */
static const uint64_t base = 0x20000000ul;

static bool is_cross_endian(QTestState *qts)
{
    bool te = qtest_big_endian(qts);
#ifdef HOST_WORDS_BIGENDIAN
    te = !te;
#endif
    return te;
}

static QTestState *create_interleaver_qtest(void)
{
    QTestState *qts;

    qts = qtest_initf("-M none -device mmio-testdev,address=0x%" PRIx64, base);
    if (is_cross_endian(qts)) {
        g_test_skip("Skipping on cross-endian targets");
        qtest_quit(qts);
        return NULL;
    }
    return qts;
}

static void test_interleaver_rd32x8a(void)
{
    QTestState *qts = create_interleaver_qtest();

    if (!qts) {
        return;
    }

    /* write sram directly */
    qtest_writeb(qts, base + 0x000, 0x10);
    qtest_writeb(qts, base + 0x100, 0x32);
    qtest_writeb(qts, base + 0x200, 0x54);
    qtest_writeb(qts, base + 0x300, 0x76);
    /* read via interleaver */
    g_assert_cmphex(qtest_readl(qts, base + 0x13208000 + 0x00), ==, 0x76543210);
    qtest_quit(qts);
}

static void test_interleaver_rd32x8b(void)
{
    QTestState *qts = create_interleaver_qtest();

    if (!qts) {
        return;
    }

    /* write sram directly */
    qtest_writeb(qts, base + 0x003, 0x10);
    qtest_writeb(qts, base + 0x103, 0x32);
    qtest_writeb(qts, base + 0x203, 0x54);
    qtest_writeb(qts, base + 0x303, 0x76);
    /* read via interleaver */
    g_assert_cmphex(qtest_readl(qts, base + 0x13208000 + 0x0c), ==, 0x76543210);
    qtest_quit(qts);
}

static void test_interleaver_rd32x16(void)
{
    QTestState *qts = create_interleaver_qtest();

    if (!qts) {
        return;
    }

    /* write sram directly */
    qtest_writew(qts, base + 0x002, 0x3210);
    qtest_writew(qts, base + 0x102, 0x7654);
    /* read via interleaver */
    g_assert_cmphex(qtest_readl(qts, base + 0x13216000 + 0x04), ==, 0x76543210);
    qtest_quit(qts);
}

static void test_interleaver_wr32x16(void)
{
    QTestState *qts = create_interleaver_qtest();

    if (!qts) {
        return;
    }

    /* write via interleaver */
    qtest_writel(qts, base + 0x13216000 + 0x04, 0x76543210);
    /* read sram directly */
    g_assert_cmphex(qtest_readw(qts, base + 0x002), ==, 0x3210);
    g_assert_cmphex(qtest_readw(qts, base + 0x102), ==, 0x7654);
    qtest_quit(qts);
}

static void test_interleaver_wr64x8(void)
{
    QTestState *qts = create_interleaver_qtest();

    if (!qts) {
        return;
    }

    /* write via interleaver */
    qtest_writeq(qts, base + 0x16408000 + 0x08, 0x9876543210);
    /* read sram directly */
    g_assert_cmphex(qtest_readb(qts, base + 0x001), ==, 0x10);
    g_assert_cmphex(qtest_readb(qts, base + 0x101), ==, 0x32);
    g_assert_cmphex(qtest_readb(qts, base + 0x401), ==, 0x98);
    qtest_quit(qts);
}

static struct {
    const char *name;
    void (*test)(void);
} tests[] = {
    {"interleaver/rd32x8a", test_interleaver_rd32x8a},
    {"interleaver/rd32x8b", test_interleaver_rd32x8b},
    {"interleaver/rd32x16", test_interleaver_rd32x16},
    {"interleaver/wr32x16", test_interleaver_wr32x16},
    {"interleaver/wr64x8",  test_interleaver_wr64x8},
};

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    for (size_t i = 0; i < ARRAY_SIZE(tests); i++) {
        g_autofree gchar *path = g_strdup_printf("mmio/%s",
                                                 tests[i].name);
        qtest_add_func(path, tests[i].test);
    }

    return g_test_run();
}
