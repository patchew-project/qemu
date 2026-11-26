/*
 * QTest testcase for the DesignWare APB timer through the K230 machine
 *
 * Copyright (c) 2026 raoyi <rao232328@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "libqtest.h"

#define TIMER_BASE 0x91105800
#define K230_TIMER_NUM_CHANNELS 6
#define K230_TIMER_DEFAULT_FREQ 50000000

#define DW_APB_TIMER_STRIDE       0x14
#define DW_APB_TIMER_N_LOAD_COUNT     0x00
#define DW_APB_TIMER_N_CURRENT_VALUE  0x04
#define DW_APB_TIMER_N_CONTROL        0x08
#define DW_APB_TIMER_N_EOI            0x0c
#define DW_APB_TIMER_N_INT_STATUS     0x10
#define DW_APB_TIMER_INT_STATUS        0xa0
#define DW_APB_TIMER_EOI               0xa4
#define DW_APB_TIMER_RAW_INT_STATUS    0xa8
#define DW_APB_TIMER_CONTROL_ENABLE        BIT(0)
#define DW_APB_TIMER_CONTROL_MODE_PERIODIC BIT(1)
#define DW_APB_TIMER_CONTROL_INT_MASK      BIT(2)

#define TIMER_REG(n, reg) (TIMER_BASE + (n) * DW_APB_TIMER_STRIDE + (reg))
#define TIMER_TICK_NS (NANOSECONDS_PER_SECOND / K230_TIMER_DEFAULT_FREQ)

static void timer_load(QTestState *qts, int n, uint32_t value)
{
    qtest_writel(qts, TIMER_REG(n, DW_APB_TIMER_N_LOAD_COUNT), value);
}

static void timer_enable(QTestState *qts, int n, uint32_t ctrl)
{
    qtest_writel(qts, TIMER_REG(n, DW_APB_TIMER_N_CONTROL), ctrl);
}

static void timer_disable(QTestState *qts, int n)
{
    qtest_writel(qts, TIMER_REG(n, DW_APB_TIMER_N_CONTROL), 0);
}

static uint32_t timer_get_and_clear_status(QTestState *qts, int n)
{
    uint32_t int_status = qtest_readl(qts,
                            TIMER_REG(n, DW_APB_TIMER_N_INT_STATUS));

    if (int_status) {
        qtest_readl(qts, TIMER_REG(n, DW_APB_TIMER_N_EOI));
    }

    return int_status;
}

static void test_timer_free_running(void)
{
    QTestState *qts = qtest_init("-machine k230");

    timer_load(qts, 0, 100);
    timer_enable(qts, 0, DW_APB_TIMER_CONTROL_ENABLE);

    qtest_clock_step(qts, 101ULL * TIMER_TICK_NS + 1);
    g_assert_cmphex(timer_get_and_clear_status(qts, 0), ==, 1);

    qtest_clock_step(qts, 10ULL * TIMER_TICK_NS);
    uint32_t cur = qtest_readl(qts,
                      TIMER_REG(0, DW_APB_TIMER_N_CURRENT_VALUE));
    g_assert_cmphex(cur, <, UINT32_MAX);
    g_assert_cmphex(cur, >, 0);

    g_assert_cmphex(qtest_readl(qts,
                      TIMER_REG(0, DW_APB_TIMER_N_INT_STATUS)), ==, 0);

    qtest_quit(qts);
}

static void test_timer_periodic(void)
{
    QTestState *qts = qtest_init("-machine k230");
    int i;

    timer_load(qts, 0, 100);
    timer_enable(qts, 0, DW_APB_TIMER_CONTROL_ENABLE |
                        DW_APB_TIMER_CONTROL_MODE_PERIODIC);

    for (i = 0; i < 3; i++) {
        qtest_clock_step(qts, 101ULL * TIMER_TICK_NS + 1);
        g_assert_cmphex(timer_get_and_clear_status(qts, 0), ==, 1);
        uint32_t cur = qtest_readl(qts,
                          TIMER_REG(0, DW_APB_TIMER_N_CURRENT_VALUE));
        g_assert_cmphex(cur, <=, 100);
    }

    qtest_quit(qts);
}

static void test_timer_int_mask(void)
{
    QTestState *qts = qtest_init("-machine k230");

    timer_load(qts, 0, 100);
    timer_enable(qts, 0, DW_APB_TIMER_CONTROL_ENABLE |
                        DW_APB_TIMER_CONTROL_MODE_PERIODIC |
                        DW_APB_TIMER_CONTROL_INT_MASK);

    qtest_clock_step(qts, 101ULL * TIMER_TICK_NS + 1);

    g_assert_cmphex(qtest_readl(qts,
                      TIMER_REG(0, DW_APB_TIMER_N_INT_STATUS)) & 1, ==, 0);
    g_assert_cmphex(qtest_readl(qts,
                      TIMER_BASE + DW_APB_TIMER_INT_STATUS), ==, 0);
    g_assert_cmphex(qtest_readl(qts,
                      TIMER_BASE + DW_APB_TIMER_RAW_INT_STATUS) & 1, ==, 1);

    /* Unmasking a pending interrupt must assert it immediately. */
    timer_enable(qts, 0, DW_APB_TIMER_CONTROL_ENABLE |
                        DW_APB_TIMER_CONTROL_MODE_PERIODIC);
    g_assert_cmphex(qtest_readl(qts,
                      TIMER_REG(0, DW_APB_TIMER_N_INT_STATUS)), ==, 1);
    g_assert_cmphex(qtest_readl(qts,
                      TIMER_BASE + DW_APB_TIMER_INT_STATUS) & 1, ==, 1);

    qtest_quit(qts);
}

static void test_timer_disable_clears_irq(void)
{
    QTestState *qts = qtest_init("-machine k230");

    timer_load(qts, 0, 100);
    timer_enable(qts, 0, DW_APB_TIMER_CONTROL_ENABLE |
                        DW_APB_TIMER_CONTROL_MODE_PERIODIC);

    qtest_clock_step(qts, 101ULL * TIMER_TICK_NS + 1);
    g_assert_cmphex(qtest_readl(qts,
                      TIMER_REG(0, DW_APB_TIMER_N_INT_STATUS)) & 1, ==, 1);

    timer_disable(qts, 0);
    g_assert_cmphex(qtest_readl(qts,
                      TIMER_REG(0, DW_APB_TIMER_N_INT_STATUS)) & 1, ==, 0);

    qtest_quit(qts);
}

static void test_timer_current_disabled(void)
{
    QTestState *qts = qtest_init("-machine k230");

    /* TRM: "A '0' is always read back when the timer is not enabled" */
    g_assert_cmphex(qtest_readl(qts,
                      TIMER_REG(0, DW_APB_TIMER_N_CURRENT_VALUE)), ==, 0);

    timer_load(qts, 0, 9999);
    g_assert_cmphex(qtest_readl(qts,
                      TIMER_REG(0, DW_APB_TIMER_N_CURRENT_VALUE)), ==, 0);

    timer_enable(qts, 0, DW_APB_TIMER_CONTROL_ENABLE);
    qtest_clock_step(qts, 10ULL * TIMER_TICK_NS);
    uint32_t cur_enabled = qtest_readl(qts,
                              TIMER_REG(0, DW_APB_TIMER_N_CURRENT_VALUE));
    g_assert_cmphex(cur_enabled, >, 0);
    g_assert_cmphex(cur_enabled, <, UINT32_MAX);

    timer_disable(qts, 0);
    g_assert_cmphex(qtest_readl(qts,
                      TIMER_REG(0, DW_APB_TIMER_N_CURRENT_VALUE)), ==, 0);

    qtest_quit(qts);
}

static void test_timer_dynamic_reload(void)
{
    QTestState *qts = qtest_init("-machine k230");
    uint32_t cur;

    timer_load(qts, 0, 1000);
    timer_enable(qts, 0, DW_APB_TIMER_CONTROL_ENABLE |
                        DW_APB_TIMER_CONTROL_MODE_PERIODIC);

    qtest_clock_step(qts, 100ULL * TIMER_TICK_NS);

    timer_load(qts, 0, 200);

    qtest_clock_step(qts, 901ULL * TIMER_TICK_NS + 1);
    g_assert_cmphex(timer_get_and_clear_status(qts, 0), ==, 1);

    qtest_clock_step(qts, 100ULL * TIMER_TICK_NS);
    cur = qtest_readl(qts, TIMER_REG(0, DW_APB_TIMER_N_CURRENT_VALUE));
    g_assert_cmphex(cur, ==, 99);

    qtest_quit(qts);
}

static void test_timer_all_channels(void)
{
    QTestState *qts = qtest_init("-machine k230");
    int i;

    for (i = 0; i < K230_TIMER_NUM_CHANNELS; i++) {
        timer_load(qts, i, 100 + i * 50);
        timer_enable(qts, i, DW_APB_TIMER_CONTROL_ENABLE |
                             DW_APB_TIMER_CONTROL_MODE_PERIODIC);
    }

    /* Step past timer 0 expiry (load=100) - only timer 0 should fire */
    qtest_clock_step(qts, 101ULL * TIMER_TICK_NS + 1);
    uint32_t sts = qtest_readl(qts, TIMER_BASE + DW_APB_TIMER_INT_STATUS);
    g_assert_cmphex(sts & 1, ==, 1);
    g_assert_cmphex(sts & 0x3e, ==, 0);

    /* Step to timer 1 expiry (another 50 ticks) */
    qtest_clock_step(qts, 50ULL * TIMER_TICK_NS + 1);
    sts = qtest_readl(qts, TIMER_BASE + DW_APB_TIMER_INT_STATUS);
    g_assert_cmphex(sts & 0x3, ==, 0x3);
    g_assert_cmphex(sts & 0x3c, ==, 0);

    /* Step to expiry of all remaining timers (another 200 ticks) */
    qtest_clock_step(qts, 200ULL * TIMER_TICK_NS + 1);
    sts = qtest_readl(qts, TIMER_BASE + DW_APB_TIMER_INT_STATUS);
    g_assert_cmphex(sts, ==, 0x3f);

    /* EOI_ALL clears all */
    qtest_readl(qts, TIMER_BASE + DW_APB_TIMER_EOI);
    sts = qtest_readl(qts, TIMER_BASE + DW_APB_TIMER_INT_STATUS);
    g_assert_cmphex(sts, ==, 0);

    qtest_quit(qts);
}

static void test_timer_reset(void)
{
    QTestState *qts = qtest_init("-machine k230");

    timer_load(qts, 0, 100);
    timer_enable(qts, 0, DW_APB_TIMER_CONTROL_ENABLE |
                        DW_APB_TIMER_CONTROL_MODE_PERIODIC);
    qtest_clock_step(qts, 10ULL * TIMER_TICK_NS);

    qtest_system_reset(qts);

    g_assert_cmphex(qtest_readl(qts,
                      TIMER_REG(0, DW_APB_TIMER_N_LOAD_COUNT)), ==, 0);
    g_assert_cmphex(qtest_readl(qts,
                      TIMER_REG(0, DW_APB_TIMER_N_CONTROL)), ==, 0);
    g_assert_cmphex(qtest_readl(qts,
                      TIMER_REG(0, DW_APB_TIMER_N_CURRENT_VALUE)), ==, 0);
    g_assert_cmphex(qtest_readl(qts,
                      TIMER_BASE + DW_APB_TIMER_RAW_INT_STATUS), ==, 0);

    qtest_quit(qts);
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/dw-apb-timer/free_running", test_timer_free_running);
    qtest_add_func("/dw-apb-timer/periodic", test_timer_periodic);
    qtest_add_func("/dw-apb-timer/int_mask", test_timer_int_mask);
    qtest_add_func("/dw-apb-timer/disable_clears_irq",
                   test_timer_disable_clears_irq);
    qtest_add_func("/dw-apb-timer/current_disabled",
                   test_timer_current_disabled);
    qtest_add_func("/dw-apb-timer/dynamic_reload",
                   test_timer_dynamic_reload);
    qtest_add_func("/dw-apb-timer/all_channels", test_timer_all_channels);
    qtest_add_func("/dw-apb-timer/reset", test_timer_reset);

    return g_test_run();
}
