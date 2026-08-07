/*
 * QTest testcase for K230 DesignWare I2C controller
 *
 * Copyright (c) 2026 Wang Zhongyu <wzy15515798875@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/bitops.h"

/* K230 I2C controller base addresses */
#define K230_I2C0_BASE                 0x91405000
#define K230_I2C1_BASE                 0x91406000
#define K230_I2C2_BASE                 0x91407000
#define K230_I2C3_BASE                 0x91408000
#define K230_I2C4_BASE                 0x91409000

#define K230_I2C_COUNT                 5

/* Register offsets used by the tests */
#define K230_IC_CON                    0x00
#define K230_IC_TAR                    0x04
#define K230_IC_SAR                    0x08
#define K230_IC_HS_MADDR               0x0c
#define K230_IC_DATA_CMD               0x10
#define K230_IC_INTR_STAT              0x2c
#define K230_IC_INTR_MASK              0x30
#define K230_IC_RAW_INTR_STAT          0x34
#define K230_IC_RX_TL                  0x38
#define K230_IC_TX_TL                  0x3c
#define K230_IC_CLR_RX_UNDER           0x44
#define K230_IC_CLR_TX_ABRT            0x54
#define K230_IC_ENABLE                 0x6c
#define K230_IC_STATUS                 0x70
#define K230_IC_TXFLR                  0x74
#define K230_IC_RXFLR                  0x78
#define K230_IC_TX_ABRT_SOURCE         0x80
#define K230_IC_ENABLE_STATUS          0x9c
#define K230_IC_COMP_PARAM_1           0xf4
#define K230_IC_COMP_VERSION           0xf8
#define K230_IC_COMP_TYPE              0xfc

#define IC_CON_10BITADDR_MASTER        BIT(4)

/* IC_DATA_CMD */
#define IC_DATA_CMD_STOP               BIT(9)

/* IC_INTR_STAT and IC_RAW_INTR_STAT */
#define IC_INTR_RX_UNDER               BIT(0)
#define IC_INTR_TX_EMPTY               BIT(4)
#define IC_INTR_TX_ABRT                BIT(6)

/* IC_STATUS */
#define IC_STATUS_TFNF                 BIT(1)
#define IC_STATUS_TFE                  BIT(2)

/* IC_TX_ABRT_SOURCE */
#define IC_ABRT_7B_ADDR_NOACK          BIT(0)

#define K230_IC_COMP_PARAM_1_VALUE     0x001f3fae
#define K230_IC_COMP_VERSION_VALUE     0x3132302a
#define K230_IC_COMP_TYPE_VALUE        0x44570140

static const uint64_t k230_i2c_base[K230_I2C_COUNT] = {
    K230_I2C0_BASE,
    K230_I2C1_BASE,
    K230_I2C2_BASE,
    K230_I2C3_BASE,
    K230_I2C4_BASE,
};

static void test_reset_values(void)
{
    QTestState *qts = qtest_init("-machine k230");
    uint32_t status;

    g_assert_cmphex(qtest_readl(qts, K230_I2C0_BASE + K230_IC_CON),
                    ==, 0x7f);
    g_assert_cmphex(qtest_readl(qts, K230_I2C0_BASE + K230_IC_TAR),
                    ==, 0x55);
    g_assert_cmphex(qtest_readl(qts, K230_I2C0_BASE + K230_IC_SAR),
                    ==, 0x55);
    g_assert_cmphex(qtest_readl(qts,
                               K230_I2C0_BASE + K230_IC_HS_MADDR),
                    ==, 0x1);

    g_assert_cmphex(qtest_readl(qts, K230_I2C0_BASE + K230_IC_ENABLE),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts,
                               K230_I2C0_BASE + K230_IC_ENABLE_STATUS),
                    ==, 0);

    g_assert_cmphex(qtest_readl(qts, K230_I2C0_BASE + K230_IC_TXFLR),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, K230_I2C0_BASE + K230_IC_RXFLR),
                    ==, 0);

    status = qtest_readl(qts, K230_I2C0_BASE + K230_IC_STATUS);
    g_assert_cmphex(status, ==, IC_STATUS_TFNF | IC_STATUS_TFE);

    g_assert_cmphex(qtest_readl(qts,
                               K230_I2C0_BASE + K230_IC_RAW_INTR_STAT),
                    ==, 0);

    g_assert_cmphex(qtest_readl(qts,
                               K230_I2C0_BASE + K230_IC_COMP_PARAM_1),
                    ==, K230_IC_COMP_PARAM_1_VALUE);
    g_assert_cmphex(qtest_readl(qts,
                               K230_I2C0_BASE + K230_IC_COMP_VERSION),
                    ==, K230_IC_COMP_VERSION_VALUE);
    g_assert_cmphex(qtest_readl(qts,
                               K230_I2C0_BASE + K230_IC_COMP_TYPE),
                    ==, K230_IC_COMP_TYPE_VALUE);

    qtest_quit(qts);
}

static void test_all_instances(void)
{
    QTestState *qts = qtest_init("-machine k230");

    for (int i = 0; i < K230_I2C_COUNT; i++) {
        g_assert_cmphex(qtest_readl(qts,
                                   k230_i2c_base[i] +
                                   K230_IC_COMP_TYPE),
                        ==, K230_IC_COMP_TYPE_VALUE);
    }

    qtest_quit(qts);
}

static void test_register_access(void)
{
    QTestState *qts = qtest_init("-machine k230");

    qtest_writel(qts, K230_I2C0_BASE + K230_IC_CON, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, K230_I2C0_BASE + K230_IC_CON),
                    ==, 0x7f);

    qtest_writel(qts, K230_I2C0_BASE + K230_IC_TAR, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, K230_I2C0_BASE + K230_IC_TAR),
                    ==, 0xfff);

    qtest_writel(qts, K230_I2C0_BASE + K230_IC_SAR, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, K230_I2C0_BASE + K230_IC_SAR),
                    ==, 0x3ff);

    qtest_writel(qts, K230_I2C0_BASE + K230_IC_HS_MADDR,
                 UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts,
                               K230_I2C0_BASE + K230_IC_HS_MADDR),
                    ==, 0x7);

    qtest_writel(qts, K230_I2C0_BASE + K230_IC_RX_TL,
                 UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, K230_I2C0_BASE + K230_IC_RX_TL),
                    ==, 63);

    qtest_writel(qts, K230_I2C0_BASE + K230_IC_TX_TL,
                 UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, K230_I2C0_BASE + K230_IC_TX_TL),
                    ==, 32);

    qtest_quit(qts);
}

static void test_register_lock_while_enabled(void)
{
    QTestState *qts = qtest_init("-machine k230");

    qtest_writel(qts, K230_I2C0_BASE + K230_IC_TAR, 0x22);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_ENABLE, 1);

    qtest_writel(qts, K230_I2C0_BASE + K230_IC_TAR, 0x33);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_CON, 0);

    g_assert_cmphex(qtest_readl(qts, K230_I2C0_BASE + K230_IC_TAR),
                    ==, 0x22);
    g_assert_cmphex(qtest_readl(qts, K230_I2C0_BASE + K230_IC_CON),
                    ==, 0x7f);

    qtest_quit(qts);
}

static void test_enable_disable(void)
{
    QTestState *qts = qtest_init("-machine k230");
    uint32_t raw;

    qtest_writel(qts, K230_I2C0_BASE + K230_IC_ENABLE, 1);

    g_assert_cmphex(qtest_readl(qts, K230_I2C0_BASE + K230_IC_ENABLE),
                    ==, 1);
    g_assert_cmphex(qtest_readl(qts,
                               K230_I2C0_BASE + K230_IC_ENABLE_STATUS),
                    ==, 1);

    raw = qtest_readl(qts,
                      K230_I2C0_BASE + K230_IC_RAW_INTR_STAT);
    g_assert_cmphex(raw & IC_INTR_TX_EMPTY, ==, IC_INTR_TX_EMPTY);

    qtest_writel(qts, K230_I2C0_BASE + K230_IC_ENABLE, 0);

    g_assert_cmphex(qtest_readl(qts, K230_I2C0_BASE + K230_IC_ENABLE),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts,
                               K230_I2C0_BASE + K230_IC_ENABLE_STATUS),
                    ==, 0);

    raw = qtest_readl(qts,
                      K230_I2C0_BASE + K230_IC_RAW_INTR_STAT);
    g_assert_cmphex(raw & IC_INTR_TX_EMPTY, ==, 0);

    qtest_quit(qts);
}

static void test_interrupt_mask(void)
{
    QTestState *qts = qtest_init("-machine k230");
    uint32_t raw;
    uint32_t stat;

    qtest_writel(qts, K230_I2C0_BASE + K230_IC_ENABLE, 1);

    raw = qtest_readl(qts,
                      K230_I2C0_BASE + K230_IC_RAW_INTR_STAT);
    g_assert_cmphex(raw & IC_INTR_TX_EMPTY, ==, IC_INTR_TX_EMPTY);

    qtest_writel(qts, K230_I2C0_BASE + K230_IC_INTR_MASK, 0);

    stat = qtest_readl(qts, K230_I2C0_BASE + K230_IC_INTR_STAT);
    g_assert_cmphex(stat, ==, 0);

    raw = qtest_readl(qts,
                      K230_I2C0_BASE + K230_IC_RAW_INTR_STAT);
    g_assert_cmphex(raw & IC_INTR_TX_EMPTY, ==, IC_INTR_TX_EMPTY);

    qtest_writel(qts, K230_I2C0_BASE + K230_IC_INTR_MASK,
                 IC_INTR_TX_EMPTY);

    stat = qtest_readl(qts, K230_I2C0_BASE + K230_IC_INTR_STAT);
    g_assert_cmphex(stat & IC_INTR_TX_EMPTY, ==, IC_INTR_TX_EMPTY);

    qtest_quit(qts);
}

static void test_rx_underflow(void)
{
    QTestState *qts = qtest_init("-machine k230");
    uint32_t raw;

    g_assert_cmphex(qtest_readl(qts,
                               K230_I2C0_BASE + K230_IC_DATA_CMD),
                    ==, 0);

    raw = qtest_readl(qts,
                      K230_I2C0_BASE + K230_IC_RAW_INTR_STAT);
    g_assert_cmphex(raw & IC_INTR_RX_UNDER, ==, IC_INTR_RX_UNDER);

    qtest_readl(qts, K230_I2C0_BASE + K230_IC_CLR_RX_UNDER);

    raw = qtest_readl(qts,
                      K230_I2C0_BASE + K230_IC_RAW_INTR_STAT);
    g_assert_cmphex(raw & IC_INTR_RX_UNDER, ==, 0);

    qtest_quit(qts);
}

static void test_address_nack(void)
{
    QTestState *qts = qtest_init("-machine k230");
    uint32_t con;
    uint32_t raw;
    uint32_t source;

    /*
     * No slave is attached at address 0x7f, so the address phase
     * must terminate with TX_ABRT.
     */
    con = qtest_readl(qts, K230_I2C0_BASE + K230_IC_CON);
    con &= ~IC_CON_10BITADDR_MASTER;
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_CON, con);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_TAR, 0x7f);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_ENABLE, 1);

    qtest_writel(qts, K230_I2C0_BASE + K230_IC_DATA_CMD,
                 IC_DATA_CMD_STOP | 0x55);

    raw = qtest_readl(qts,
                      K230_I2C0_BASE + K230_IC_RAW_INTR_STAT);
    source = qtest_readl(qts,
                         K230_I2C0_BASE + K230_IC_TX_ABRT_SOURCE);

    g_assert_cmphex(raw & IC_INTR_TX_ABRT, ==, IC_INTR_TX_ABRT);
    g_assert_cmphex(source & IC_ABRT_7B_ADDR_NOACK,
                    ==, IC_ABRT_7B_ADDR_NOACK);

    qtest_readl(qts, K230_I2C0_BASE + K230_IC_CLR_TX_ABRT);

    raw = qtest_readl(qts,
                      K230_I2C0_BASE + K230_IC_RAW_INTR_STAT);
    source = qtest_readl(qts,
                         K230_I2C0_BASE + K230_IC_TX_ABRT_SOURCE);

    g_assert_cmphex(raw & IC_INTR_TX_ABRT, ==, 0);
    g_assert_cmphex(source, ==, 0);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/k230-i2c/reset-values",
                   test_reset_values);
    qtest_add_func("/k230-i2c/all-instances",
                   test_all_instances);
    qtest_add_func("/k230-i2c/register-access",
                   test_register_access);
    qtest_add_func("/k230-i2c/register-lock-while-enabled",
                   test_register_lock_while_enabled);
    qtest_add_func("/k230-i2c/enable-disable",
                   test_enable_disable);
    qtest_add_func("/k230-i2c/interrupt-mask",
                   test_interrupt_mask);
    qtest_add_func("/k230-i2c/rx-underflow",
                   test_rx_underflow);
    qtest_add_func("/k230-i2c/address-nack",
                   test_address_nack);

    return g_test_run();
}
