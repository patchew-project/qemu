/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * QTest testcase for the QCT QTimer
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"
#include "hw/hexagon/hexagon.h"
#include "qemu/bitops.h"

/* Pull in the per-machine addresses instead of hardcoding them here. */
#include "hw/hexagon/machine_cfg_v68n_1024.h.inc"
#include "hw/hexagon/machine_cfg_v66g_1024.h.inc"

#define QTIMER_DEFAULT_FREQ_HZ 19200000ULL

/* View-region register offsets exercised by this test. */
#define QCT_QTIMER_CNTPCT_LO (0x000)
#define QCT_QTIMER_CNT_FREQ (0x010)
#define QCT_QTIMER_CNTP_CVAL_LO (0x020)
#define QCT_QTIMER_CNTP_TVAL (0x028)
#define QCT_QTIMER_CNTP_CTL (0x02c)

/* View region base of the machine under test, set by test_qtimer_on_machine. */
static uint64_t qtimer_view_base;

#define TIMER_TEST_OFFSET 1000
/* TIMER_TEST_OFFSET ticks expressed in nanoseconds of QEMU_CLOCK_VIRTUAL */
#define TIMER_TEST_NS \
    ((TIMER_TEST_OFFSET * 1000000000ULL) / QTIMER_DEFAULT_FREQ_HZ)

static uint32_t qtimer_read32(uint64_t base, uint32_t offset)
{
    return readl(base + offset);
}

static void qtimer_write32(uint64_t base, uint32_t offset, uint32_t value)
{
    writel(base + offset, value);
}

static uint64_t qtimer_read64(uint64_t base, uint32_t offset)
{
    uint32_t lo = qtimer_read32(base, offset);
    uint32_t hi = qtimer_read32(base, offset + 4);

    return ((uint64_t)hi << 32) | lo;
}

static void qtimer_write64(uint64_t base, uint32_t offset, uint64_t value)
{
    qtimer_write32(base, offset, extract64(value, 0, 32));
    qtimer_write32(base, offset + 4, extract64(value, 32, 32));
}

/* Test basic device presence and register access */
static void test_qtimer_basic_access(void)
{
    uint32_t val;

    val = qtimer_read32(qtimer_view_base, QCT_QTIMER_CNT_FREQ);
    g_assert_cmpuint(val, ==, QTIMER_DEFAULT_FREQ_HZ);
}

/* Test multiple timer frames */
static void test_qtimer_multiple_frames(void)
{
    uint32_t val;
    uint64_t frame0_base = qtimer_view_base;
    uint64_t frame1_base = qtimer_view_base + 0x1000;

    val = qtimer_read32(frame0_base, QCT_QTIMER_CNT_FREQ);
    g_assert_cmpuint(val, ==, QTIMER_DEFAULT_FREQ_HZ);

    val = qtimer_read32(frame1_base, QCT_QTIMER_CNT_FREQ);
    g_assert_cmpuint(val, ==, QTIMER_DEFAULT_FREQ_HZ);
}

/* Test that registers exist and can be accessed */
static void test_qtimer_register_reads(void)
{
    qtimer_read32(qtimer_view_base, QCT_QTIMER_CNT_FREQ);
    qtimer_read64(qtimer_view_base, QCT_QTIMER_CNTPCT_LO);
    qtimer_read64(qtimer_view_base, QCT_QTIMER_CNTP_CVAL_LO);
    qtimer_read32(qtimer_view_base, QCT_QTIMER_CNTP_CTL);
    qtimer_read32(qtimer_view_base, QCT_QTIMER_CNTP_TVAL);
}

/* Test timer control and value registers */
static void test_qtimer_control_registers(void)
{
    uint32_t ctl_val;
    uint64_t cval_before, cval_after;

    cval_before = qtimer_read64(qtimer_view_base, QCT_QTIMER_CNTPCT_LO);

    qtimer_write32(qtimer_view_base, QCT_QTIMER_CNTP_TVAL, 1000);

    qtimer_write32(qtimer_view_base, QCT_QTIMER_CNTP_CTL, 1);
    ctl_val = qtimer_read32(qtimer_view_base, QCT_QTIMER_CNTP_CTL);
    g_assert_cmpuint(ctl_val & 1, ==, 1);

    /* CVAL should be greater than before since we set TVAL */
    cval_after = qtimer_read64(qtimer_view_base, QCT_QTIMER_CNTP_CVAL_LO);
    g_assert_cmpuint(cval_after, >, cval_before);

    qtimer_write32(qtimer_view_base, QCT_QTIMER_CNTP_CTL, 0);
    ctl_val = qtimer_read32(qtimer_view_base, QCT_QTIMER_CNTP_CTL);
    g_assert_cmpuint(ctl_val & 1, ==, 0);
}

/* Test CVAL register direct access */
static void test_qtimer_cval_access(void)
{
    uint64_t current_time, test_cval, read_cval;

    current_time = qtimer_read64(qtimer_view_base, QCT_QTIMER_CNTPCT_LO);
    test_cval = current_time + 10000;

    qtimer_write64(qtimer_view_base, QCT_QTIMER_CNTP_CVAL_LO, test_cval);
    read_cval = qtimer_read64(qtimer_view_base, QCT_QTIMER_CNTP_CVAL_LO);
    g_assert_cmpuint(read_cval, ==, test_cval);
}

/* Test counter progression */
static void test_qtimer_counter_progression(void)
{
    uint32_t freq;
    uint64_t count1, count2;

    /*
     * In qtest mode the virtual clock does not advance on its own, so
     * reading the counter twice must give the same value.
     */
    count1 = qtimer_read64(qtimer_view_base, QCT_QTIMER_CNTPCT_LO);
    count2 = qtimer_read64(qtimer_view_base, QCT_QTIMER_CNTPCT_LO);
    g_assert_cmpuint(count2, ==, count1);

    freq = qtimer_read32(qtimer_view_base, QCT_QTIMER_CNT_FREQ);
    g_assert_cmpuint(freq, ==, QTIMER_DEFAULT_FREQ_HZ);
}

/* Test timer behavior with clock control */
static void test_qtimer_timer_behavior(void)
{
    uint64_t current_count, target_count, read_cval, new_count;
    uint64_t ctl_val, count_after_disable;

    current_count = qtimer_read64(qtimer_view_base, QCT_QTIMER_CNTPCT_LO);

    target_count = current_count + TIMER_TEST_OFFSET;
    qtimer_write64(qtimer_view_base, QCT_QTIMER_CNTP_CVAL_LO, target_count);

    read_cval = qtimer_read64(qtimer_view_base, QCT_QTIMER_CNTP_CVAL_LO);
    g_assert_cmpuint(read_cval, ==, target_count);

    qtimer_write32(qtimer_view_base, QCT_QTIMER_CNTP_CTL, 1);

    ctl_val = qtimer_read64(qtimer_view_base, QCT_QTIMER_CNTP_CTL);
    /* EN set, IMASK clear, ISTAT not yet pending */
    g_assert_cmpuint(ctl_val, ==, 0x1);

    /* Step forward but not past the target */
    qtest_clock_step(global_qtest, TIMER_TEST_NS / 2);
    new_count = qtimer_read64(qtimer_view_base, QCT_QTIMER_CNTPCT_LO);
    g_assert_cmpuint(new_count, >=, current_count);

    /* Step past the target */
    qtest_clock_step(global_qtest, TIMER_TEST_NS);
    new_count = qtimer_read64(qtimer_view_base, QCT_QTIMER_CNTPCT_LO);
    g_assert_cmpuint(new_count, >=, target_count);

    qtimer_write32(qtimer_view_base, QCT_QTIMER_CNTP_CTL, 0);

    ctl_val = qtimer_read64(qtimer_view_base, QCT_QTIMER_CNTP_CTL);
    /* EN cleared, ISTAT set since new_count >= target_count */
    g_assert_cmpuint(ctl_val, ==, 0x4);

    /*
     * CNTPCT runs independently of CNTP_CTL.EN: only the compare/IRQ
     * logic is gated by EN, so the counter must keep advancing.
     */
    qtest_clock_step(global_qtest, TIMER_TEST_NS / 2);
    count_after_disable = qtimer_read64(qtimer_view_base,
                                        QCT_QTIMER_CNTPCT_LO);
    g_assert_cmpuint(count_after_disable, >, new_count);

    /* ISTAT remains set while CNTPCT >= CVAL, even with EN=0 */
    ctl_val = qtimer_read64(qtimer_view_base, QCT_QTIMER_CNTP_CTL);
    g_assert_cmpuint(ctl_val, ==, 0x4);
}

typedef struct {
    const char *machine;
    const struct hexagon_machine_config *cfg;
} QtimerMachineCfg;

static const QtimerMachineCfg qtimer_machines[] = {
    { "virt",      &v68n_1024 },
    { "V66G_1024", &v66g_1024 },
};

static void test_qtimer_on_machine(gconstpointer data)
{
    const QtimerMachineCfg *mc = data;
    g_autofree char *args = g_strdup_printf(
        "-machine %s -global qct-qtimer.freq-scale=1", mc->machine);

    qtimer_view_base = mc->cfg->qtmr_region;

    qtest_start(args);

    test_qtimer_basic_access();
    test_qtimer_multiple_frames();
    test_qtimer_register_reads();
    test_qtimer_control_registers();
    test_qtimer_cval_access();
    test_qtimer_counter_progression();
    test_qtimer_timer_behavior();

    qtest_end();
}

int main(int argc, char **argv)
{
    size_t i;

    g_test_init(&argc, &argv, NULL);

    for (i = 0; i < ARRAY_SIZE(qtimer_machines); i++) {
        g_autofree char *path = g_strdup_printf("/qct-qtimer/%s/all-tests",
                                                qtimer_machines[i].machine);
        qtest_add_data_func(path, &qtimer_machines[i], test_qtimer_on_machine);
    }

    return g_test_run();
}
