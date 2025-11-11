/*
 * Testcase for RISC-V Trace framework
 *
 * Copyright (C) 2025 Ventana Micro Systems Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "qemu/timer.h"
#include "qemu/bitops.h"
#include "libqtest.h"
#include "hw/registerfields.h"

/* taken from virt machine memmap */
#define TE_BASE   0x3020000
#define TRAM_BASE 0x6000000

REG32(TR_TE_CONTROL, 0x0)
    FIELD(TR_TE_CONTROL, ACTIVE, 0, 1)
    FIELD(TR_TE_CONTROL, ENABLE, 1, 1)
    FIELD(TR_TE_CONTROL, INST_TRACING, 2, 1)

REG32(TR_RAM_START_LOW, 0x010)
    FIELD(TR_RAM_START_LOW, ADDR, 0, 32)
REG32(TR_RAM_START_HIGH, 0x014)
    FIELD(TR_RAM_START_HIGH, ADDR, 0, 32)

REG32(TR_RAM_LIMIT_LOW, 0x018)
    FIELD(TR_RAM_LIMIT_LOW, ADDR, 0, 32)
REG32(TR_RAM_LIMIT_HIGH, 0x01C)
    FIELD(TR_RAM_LIMIT_HIGH, ADDR, 0, 32)

REG32(TR_RAM_WP_LOW, 0x020)
    FIELD(TR_RAM_WP_LOW, WRAP, 0, 1)
    FIELD(TR_RAM_WP_LOW, ADDR, 0, 32)
REG32(TR_RAM_WP_HIGH, 0x024)
    FIELD(TR_RAM_WP_HIGH, ADDR, 0, 32)

static uint32_t test_read_te_control(QTestState *qts)
{
    return qtest_readl(qts, TE_BASE + A_TR_TE_CONTROL);
}

static void test_write_te_control(QTestState *qts, uint32_t val)
{
    qtest_writel(qts, TE_BASE + A_TR_TE_CONTROL, val);
}

static uint64_t test_read_tram_ramstart(QTestState *qts)
{
    uint64_t reg = qtest_readl(qts, TRAM_BASE + A_TR_RAM_START_HIGH);

    reg <<= 32;
    reg += qtest_readl(qts, TRAM_BASE + A_TR_RAM_START_LOW);
    return reg;
}

static uint64_t test_read_tram_writep(QTestState *qts)
{
    uint64_t reg = qtest_readl(qts, TRAM_BASE + A_TR_RAM_WP_HIGH);

    reg <<= 32;
    reg += qtest_readl(qts, TRAM_BASE + A_TR_RAM_WP_LOW);
    return reg;
}

static void test_trace_simple(void)
{
    QTestState *qts = qtest_init("-machine virt -accel tcg");
    double timeout_sec = 0.5;
    uint64_t reg_tram_start, reg_tram_writep;
    uint32_t reg;

    reg = test_read_te_control(qts);
    reg = FIELD_DP32(reg, TR_TE_CONTROL, ACTIVE, 1);
    test_write_te_control(qts, reg);
    reg = test_read_te_control(qts);
    g_assert(1 == FIELD_EX32(reg, TR_TE_CONTROL, ACTIVE));

    reg = FIELD_DP32(reg, TR_TE_CONTROL, ENABLE, 1);
    test_write_te_control(qts, reg);
    reg = test_read_te_control(qts);
    g_assert(1 == FIELD_EX32(reg, TR_TE_CONTROL, ENABLE));

    /*
     * Verify if RAM Sink write pointer is equal to
     * ramstart before start tracing.
     */
    reg_tram_start = test_read_tram_ramstart(qts);
    g_assert(reg_tram_start > 0);
    reg_tram_writep = test_read_tram_writep(qts);
    g_assert(reg_tram_writep == reg_tram_start);

    reg = FIELD_DP32(reg, TR_TE_CONTROL, INST_TRACING, 1);
    test_write_te_control(qts, reg);
    reg = test_read_te_control(qts);
    g_assert(1 == FIELD_EX32(reg, TR_TE_CONTROL, INST_TRACING));

    g_test_timer_start();
    for (;;) {
        reg_tram_writep = test_read_tram_writep(qts);
        if (reg_tram_writep > reg_tram_start) {
            break;
        }

        g_assert(g_test_timer_elapsed() <= timeout_sec);
    }

    qtest_quit(qts);
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/riscv-trace-test/test-trace-simple",
                   test_trace_simple);
    return g_test_run();
}
