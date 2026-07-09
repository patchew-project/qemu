/*
 * QTest for the K230 Reset Management Unit.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "libqtest.h"
#include "hw/misc/k230_rmu.h"

#define K230_RMU_BASE 0x91101000ULL

static inline uint32_t rd(QTestState *qts, uint64_t off)
{
    return qtest_readl(qts, K230_RMU_BASE + off);
}
static inline void wr(QTestState *qts, uint64_t off, uint32_t val)
{
    qtest_writel(qts, K230_RMU_BASE + off, val);
}

/* Every known register reads back as 0 after reset. */
static void test_reset_values(void)
{
    QTestState *qts = qtest_init("-machine k230");

    g_assert_cmphex(rd(qts, K230_RMU_CPU0_CTRL),  ==, 0);
    g_assert_cmphex(rd(qts, K230_RMU_AI_CTRL),    ==, 0);
    g_assert_cmphex(rd(qts, K230_RMU_HISYS_CTRL), ==, 0);
    g_assert_cmphex(rd(qts, K230_RMU_PERI0_CTRL), ==, 0);

    qtest_quit(qts);
}

/*
 * CPU0: a low-half write without its strobe is ignored; with the strobe the
 * reset request fires, latches done (bit12), and the request bit auto-clears.
 */
static void test_cpu_write_enable(void)
{
    QTestState *qts = qtest_init("-machine k230");

    /* No strobe: the low-half write is dropped, reads back as 0. */
    wr(qts, K230_RMU_CPU0_CTRL, K230_RMU_CPU_RESET);
    g_assert_cmphex(rd(qts, K230_RMU_CPU0_CTRL), ==, 0);

    /* With strobe (bit16): request bit0 takes effect, done (bit12) latches. */
    wr(qts, K230_RMU_CPU0_CTRL,
       K230_RMU_CPU_RESET | (K230_RMU_CPU_RESET << K230_RMU_WE_SHIFT));
    g_assert_cmphex(rd(qts, K230_RMU_CPU0_CTRL), ==, K230_RMU_CPU_DONE);

    qtest_quit(qts);
}

/*
 * HW_DONE: no write-enable needed. Writing HISYS request bit0 latches its
 * done bit (bit4); writing 1 to the done bit clears it.
 */
static void test_hw_done_and_w1c(void)
{
    QTestState *qts = qtest_init("-machine k230");
    uint32_t v;

    wr(qts, K230_RMU_HISYS_CTRL, BIT(0));      /* no strobe */
    v = rd(qts, K230_RMU_HISYS_CTRL);
    g_assert_cmphex(v & BIT(4), ==, BIT(4));   /* done latched */
    g_assert_cmphex(v & BIT(0), ==, 0);        /* request bit auto-cleared */

    wr(qts, K230_RMU_HISYS_CTRL, BIT(4));      /* write-1-to-clear */
    g_assert_cmphex(rd(qts, K230_RMU_HISYS_CTRL) & BIT(4), ==, 0);

    qtest_quit(qts);
}

/* Repeated resets: one HW_DONE line can be triggered again and again. */
static void test_repeat_reset(void)
{
    QTestState *qts = qtest_init("-machine k230");

    for (int i = 0; i < 3; i++) {
        wr(qts, K230_RMU_AI_CTRL, BIT(0));                 /* request */
        g_assert_cmphex(rd(qts, K230_RMU_AI_CTRL) & BIT(31), ==, BIT(31));
        wr(qts, K230_RMU_AI_CTRL, BIT(31));                /* W1C done */
        g_assert_cmphex(rd(qts, K230_RMU_AI_CTRL) & BIT(31), ==, 0);
    }

    qtest_quit(qts);
}

/* FLUSH: the CPU0 bit4 flush request is auto-cleared, reads back as 0. */
static void test_flush_auto_clear(void)
{
    QTestState *qts = qtest_init("-machine k230");

    wr(qts, K230_RMU_CPU0_CTRL,
       K230_RMU_CPU_FLUSH | (K230_RMU_CPU_FLUSH << K230_RMU_WE_SHIFT));
    g_assert_cmphex(rd(qts, K230_RMU_CPU0_CTRL) & K230_RMU_CPU_FLUSH, ==, 0);

    qtest_quit(qts);
}

/* SW_DONE: the PERI0 low half is plain storage, no strobe and no done bit. */
static void test_sw_done_storage(void)
{
    QTestState *qts = qtest_init("-machine k230");

    wr(qts, K230_RMU_PERI0_CTRL, BIT(12) | BIT(13));   /* WDT0 / WDT1 reset bits */
    g_assert_cmphex(rd(qts, K230_RMU_PERI0_CTRL), ==, (BIT(12) | BIT(13)));

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/k230/rmu/reset_values",   test_reset_values);
    qtest_add_func("/k230/rmu/cpu_we",         test_cpu_write_enable);
    qtest_add_func("/k230/rmu/hw_done_w1c",    test_hw_done_and_w1c);
    qtest_add_func("/k230/rmu/repeat_reset",   test_repeat_reset);
    qtest_add_func("/k230/rmu/flush",          test_flush_auto_clear);
    qtest_add_func("/k230/rmu/sw_done",        test_sw_done_storage);

    return g_test_run();
}
