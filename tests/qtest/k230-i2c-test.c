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
#define K230_IC_SS_SCL_HCNT            0x14
#define K230_IC_SS_SCL_LCNT            0x18
#define K230_IC_FS_SCL_HCNT            0x1c
#define K230_IC_FS_SCL_LCNT            0x20
#define K230_IC_HS_SCL_HCNT            0x24
#define K230_IC_HS_SCL_LCNT            0x28
#define K230_IC_INTR_STAT              0x2c
#define K230_IC_INTR_MASK              0x30
#define K230_IC_RAW_INTR_STAT          0x34
#define K230_IC_RX_TL                  0x38
#define K230_IC_TX_TL                  0x3c
#define K230_IC_CLR_INTR               0x40
#define K230_IC_CLR_RX_UNDER           0x44
#define K230_IC_CLR_TX_ABRT            0x54
#define K230_IC_ENABLE                 0x6c
#define K230_IC_STATUS                 0x70
#define K230_IC_TXFLR                  0x74
#define K230_IC_RXFLR                  0x78
#define K230_IC_TX_ABRT_SOURCE         0x80
#define K230_IC_SDA_SETUP              0x94
#define K230_IC_ACK_GENERAL_CALL       0x98
#define K230_IC_ENABLE_STATUS          0x9c
#define K230_IC_COMP_PARAM_1           0xf4
#define K230_IC_COMP_VERSION           0xf8
#define K230_IC_COMP_TYPE              0xfc

/* IC_CON */
#define IC_CON_MASTER_MODE             BIT(0)
#define IC_CON_10BITADDR_MASTER        BIT(4)
#define IC_CON_RESTART_EN              BIT(5)

/* IC_TAR */
#define IC_TAR_GC_OR_START             BIT(10)
#define IC_TAR_SPECIAL                 BIT(11)

/* IC_DATA_CMD */
#define IC_DATA_CMD_READ               BIT(8)
#define IC_DATA_CMD_STOP               BIT(9)

/* IC_INTR_STAT and IC_RAW_INTR_STAT */
#define IC_INTR_RX_UNDER               BIT(0)
#define IC_INTR_TX_EMPTY               BIT(4)
#define IC_INTR_TX_ABRT                BIT(6)
#define IC_INTR_ACTIVITY               BIT(8)
#define IC_INTR_START_DET              BIT(10)

/* IC_STATUS */
#define IC_STATUS_TFNF                 BIT(1)
#define IC_STATUS_TFE                  BIT(2)

/* IC_TX_ABRT_SOURCE */
#define IC_ABRT_7B_ADDR_NOACK          BIT(0)
#define IC_ABRT_GCALL_NOACK            BIT(4)
#define IC_ABRT_GCALL_READ             BIT(5)
#define IC_ABRT_SBYTE_NORSTRT          BIT(9)
#define IC_ABRT_MASTER_DIS             BIT(11)

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

static void test_instance_isolation(void)
{
    QTestState *qts = qtest_init("-machine k230");

    qtest_writel(qts, K230_I2C0_BASE + K230_IC_TAR, 0x2a);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_RX_TL, 17);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_INTR_MASK, 0x41);

    g_assert_cmphex(qtest_readl(qts, K230_I2C0_BASE + K230_IC_TAR),
                    ==, 0x2a);
    g_assert_cmphex(qtest_readl(qts, K230_I2C0_BASE + K230_IC_RX_TL),
                    ==, 17);
    g_assert_cmphex(qtest_readl(qts,
                               K230_I2C0_BASE + K230_IC_INTR_MASK),
                    ==, 0x41);

    for (int i = 1; i < K230_I2C_COUNT; i++) {
        g_assert_cmphex(qtest_readl(qts,
                                   k230_i2c_base[i] + K230_IC_TAR),
                        ==, 0x55);
        g_assert_cmphex(qtest_readl(qts,
                                   k230_i2c_base[i] + K230_IC_RX_TL),
                        ==, 0);
        g_assert_cmphex(qtest_readl(qts,
                                   k230_i2c_base[i] +
                                   K230_IC_INTR_MASK),
                        ==, 0x8ff);
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

    qtest_writel(qts, K230_I2C0_BASE + K230_IC_INTR_MASK,
                 UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts,
                               K230_I2C0_BASE + K230_IC_INTR_MASK),
                    ==, 0xfff);

    qtest_writel(qts, K230_I2C0_BASE + K230_IC_SDA_SETUP,
                 UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts,
                               K230_I2C0_BASE + K230_IC_SDA_SETUP),
                    ==, 0xff);

    qtest_writel(qts, K230_I2C0_BASE + K230_IC_ACK_GENERAL_CALL,
                 UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts,
                               K230_I2C0_BASE +
                               K230_IC_ACK_GENERAL_CALL),
                    ==, 1);

    qtest_writel(qts, K230_I2C0_BASE + K230_IC_TX_ABRT_SOURCE,
                 UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts,
                               K230_I2C0_BASE +
                               K230_IC_TX_ABRT_SOURCE),
                    ==, 0xffff);

    qtest_writel(qts, K230_I2C0_BASE + K230_IC_SS_SCL_HCNT,
                 65525);
    g_assert_cmphex(qtest_readl(qts,
                               K230_I2C0_BASE +
                               K230_IC_SS_SCL_HCNT),
                    ==, 65525);

    /*
     * Values greater than 65525 are rejected. The previous valid
     * value must be preserved.
     */
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_SS_SCL_HCNT,
                 65526);
    g_assert_cmphex(qtest_readl(qts,
                               K230_I2C0_BASE +
                               K230_IC_SS_SCL_HCNT),
                    ==, 65525);

    qtest_quit(qts);
}

static void test_readonly_registers(void)
{
    static const struct {
        uint32_t offset;
        uint32_t expected;
    } registers[] = {
        { K230_IC_INTR_STAT,     0 },
        { K230_IC_RAW_INTR_STAT, 0 },
        { K230_IC_STATUS,        IC_STATUS_TFNF | IC_STATUS_TFE },
        { K230_IC_TXFLR,         0 },
        { K230_IC_RXFLR,         0 },
        { K230_IC_ENABLE_STATUS, 0 },
        { K230_IC_COMP_PARAM_1,  K230_IC_COMP_PARAM_1_VALUE },
        { K230_IC_COMP_VERSION,  K230_IC_COMP_VERSION_VALUE },
        { K230_IC_COMP_TYPE,     K230_IC_COMP_TYPE_VALUE },
    };
    QTestState *qts = qtest_init("-machine k230");

    for (size_t i = 0; i < G_N_ELEMENTS(registers); i++) {
        qtest_writel(qts,
                     K230_I2C0_BASE + registers[i].offset,
                     UINT32_MAX);

        g_assert_cmphex(qtest_readl(qts,
                                   K230_I2C0_BASE +
                                   registers[i].offset),
                        ==, registers[i].expected);
    }

    qtest_quit(qts);
}

static void test_register_lock_while_enabled(void)
{
    QTestState *qts = qtest_init("-machine k230");

    qtest_writel(qts, K230_I2C0_BASE + K230_IC_TAR, 0x22);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_SAR, 0x155);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_HS_MADDR, 0x3);

    qtest_writel(qts, K230_I2C0_BASE + K230_IC_SS_SCL_HCNT, 20);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_SS_SCL_LCNT, 21);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_FS_SCL_HCNT, 22);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_FS_SCL_LCNT, 23);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_HS_SCL_HCNT, 24);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_HS_SCL_LCNT, 25);

    qtest_writel(qts, K230_I2C0_BASE + K230_IC_ENABLE, 1);

    /*
     * Configuration registers cannot be changed while the
     * controller is enabled.
     */
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_CON, 0);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_TAR, 0x33);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_SAR, 0x222);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_HS_MADDR, 0x7);

    qtest_writel(qts, K230_I2C0_BASE + K230_IC_SS_SCL_HCNT, 30);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_SS_SCL_LCNT, 31);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_FS_SCL_HCNT, 32);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_FS_SCL_LCNT, 33);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_HS_SCL_HCNT, 34);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_HS_SCL_LCNT, 35);

    g_assert_cmphex(qtest_readl(qts, K230_I2C0_BASE + K230_IC_CON),
                    ==, 0x7f);
    g_assert_cmphex(qtest_readl(qts, K230_I2C0_BASE + K230_IC_TAR),
                    ==, 0x22);
    g_assert_cmphex(qtest_readl(qts, K230_I2C0_BASE + K230_IC_SAR),
                    ==, 0x155);
    g_assert_cmphex(qtest_readl(qts,
                               K230_I2C0_BASE + K230_IC_HS_MADDR),
                    ==, 0x3);

    g_assert_cmphex(qtest_readl(qts,
                               K230_I2C0_BASE +
                               K230_IC_SS_SCL_HCNT),
                    ==, 20);
    g_assert_cmphex(qtest_readl(qts,
                               K230_I2C0_BASE +
                               K230_IC_SS_SCL_LCNT),
                    ==, 21);
    g_assert_cmphex(qtest_readl(qts,
                               K230_I2C0_BASE +
                               K230_IC_FS_SCL_HCNT),
                    ==, 22);
    g_assert_cmphex(qtest_readl(qts,
                               K230_I2C0_BASE +
                               K230_IC_FS_SCL_LCNT),
                    ==, 23);
    g_assert_cmphex(qtest_readl(qts,
                               K230_I2C0_BASE +
                               K230_IC_HS_SCL_HCNT),
                    ==, 24);
    g_assert_cmphex(qtest_readl(qts,
                               K230_I2C0_BASE +
                               K230_IC_HS_SCL_LCNT),
                    ==, 25);

    /*
     * Configuration registers become writable again after the
     * controller is disabled.
     */
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_ENABLE, 0);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_TAR, 0x33);

    g_assert_cmphex(qtest_readl(qts, K230_I2C0_BASE + K230_IC_TAR),
                    ==, 0x33);

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

static void test_system_reset(void)
{
    QTestState *qts = qtest_init("-machine k230");
    uint32_t raw;

    qtest_writel(qts, K230_I2C0_BASE + K230_IC_TAR, 0x2a);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_SAR, 0x123);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_RX_TL, 10);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_TX_TL, 12);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_INTR_MASK, 0);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_ENABLE, 1);

    /*
     * Reading an empty RX FIFO produces RX_UNDER.
     */
    qtest_readl(qts, K230_I2C0_BASE + K230_IC_DATA_CMD);

    raw = qtest_readl(qts,
                      K230_I2C0_BASE + K230_IC_RAW_INTR_STAT);
    g_assert_cmphex(raw & IC_INTR_RX_UNDER, ==, IC_INTR_RX_UNDER);

    qtest_system_reset(qts);

    g_assert_cmphex(qtest_readl(qts, K230_I2C0_BASE + K230_IC_CON),
                    ==, 0x7f);
    g_assert_cmphex(qtest_readl(qts, K230_I2C0_BASE + K230_IC_TAR),
                    ==, 0x55);
    g_assert_cmphex(qtest_readl(qts, K230_I2C0_BASE + K230_IC_SAR),
                    ==, 0x55);
    g_assert_cmphex(qtest_readl(qts,
                               K230_I2C0_BASE + K230_IC_HS_MADDR),
                    ==, 0x1);
    g_assert_cmphex(qtest_readl(qts,
                               K230_I2C0_BASE + K230_IC_INTR_MASK),
                    ==, 0x8ff);
    g_assert_cmphex(qtest_readl(qts, K230_I2C0_BASE + K230_IC_RX_TL),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, K230_I2C0_BASE + K230_IC_TX_TL),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, K230_I2C0_BASE + K230_IC_ENABLE),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts,
                               K230_I2C0_BASE + K230_IC_ENABLE_STATUS),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts,
                               K230_I2C0_BASE + K230_IC_RAW_INTR_STAT),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts,
                               K230_I2C0_BASE +
                               K230_IC_TX_ABRT_SOURCE),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, K230_I2C0_BASE + K230_IC_TXFLR),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, K230_I2C0_BASE + K230_IC_RXFLR),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, K230_I2C0_BASE + K230_IC_STATUS),
                    ==, IC_STATUS_TFNF | IC_STATUS_TFE);

    qtest_quit(qts);
}

static void test_clear_on_read(void)
{
    QTestState *qts = qtest_init("-machine k230");
    uint32_t con;
    uint32_t raw;
    uint32_t source;

    /*
     * Produce RX_UNDER.
     */
    qtest_readl(qts, K230_I2C0_BASE + K230_IC_DATA_CMD);

    raw = qtest_readl(qts,
                      K230_I2C0_BASE + K230_IC_RAW_INTR_STAT);
    g_assert_cmphex(raw & IC_INTR_RX_UNDER, ==, IC_INTR_RX_UNDER);

    /*
     * Writing a clear register must not clear the interrupt.
     */
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_CLR_RX_UNDER, 1);

    raw = qtest_readl(qts,
                      K230_I2C0_BASE + K230_IC_RAW_INTR_STAT);
    g_assert_cmphex(raw & IC_INTR_RX_UNDER, ==, IC_INTR_RX_UNDER);

    /*
     * Produce an independent TX_ABRT interrupt.
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

    g_assert_cmphex(raw & IC_INTR_RX_UNDER, ==, IC_INTR_RX_UNDER);
    g_assert_cmphex(raw & IC_INTR_TX_ABRT, ==, IC_INTR_TX_ABRT);
    g_assert_cmphex(source & IC_ABRT_7B_ADDR_NOACK,
                    ==, IC_ABRT_7B_ADDR_NOACK);

    /*
     * Reading CLR_RX_UNDER clears only RX_UNDER.
     */
    qtest_readl(qts, K230_I2C0_BASE + K230_IC_CLR_RX_UNDER);

    raw = qtest_readl(qts,
                      K230_I2C0_BASE + K230_IC_RAW_INTR_STAT);

    g_assert_cmphex(raw & IC_INTR_RX_UNDER, ==, 0);
    g_assert_cmphex(raw & IC_INTR_TX_ABRT, ==, IC_INTR_TX_ABRT);

    /*
     * Writing CLR_TX_ABRT must not clear TX_ABRT.
     */
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_CLR_TX_ABRT, 1);

    raw = qtest_readl(qts,
                      K230_I2C0_BASE + K230_IC_RAW_INTR_STAT);
    g_assert_cmphex(raw & IC_INTR_TX_ABRT, ==, IC_INTR_TX_ABRT);

    /*
     * Reading CLR_TX_ABRT clears TX_ABRT and its source.
     */
    qtest_readl(qts, K230_I2C0_BASE + K230_IC_CLR_TX_ABRT);

    raw = qtest_readl(qts,
                      K230_I2C0_BASE + K230_IC_RAW_INTR_STAT);
    source = qtest_readl(qts,
                         K230_I2C0_BASE + K230_IC_TX_ABRT_SOURCE);

    g_assert_cmphex(raw & IC_INTR_TX_ABRT, ==, 0);
    g_assert_cmphex(source, ==, 0);

    /*
     * CLR_INTR clears latched interrupts. TX_EMPTY remains asserted
     * because it is recalculated from the empty TX FIFO.
     */
    qtest_readl(qts, K230_I2C0_BASE + K230_IC_CLR_INTR);

    raw = qtest_readl(qts,
                      K230_I2C0_BASE + K230_IC_RAW_INTR_STAT);

    g_assert_cmphex(raw & IC_INTR_RX_UNDER, ==, 0);
    g_assert_cmphex(raw & IC_INTR_TX_ABRT, ==, 0);
    g_assert_cmphex(raw & IC_INTR_ACTIVITY, ==, 0);
    g_assert_cmphex(raw & IC_INTR_START_DET, ==, 0);
    g_assert_cmphex(raw & IC_INTR_TX_EMPTY, ==, IC_INTR_TX_EMPTY);

    qtest_quit(qts);
}

static void test_abort_sources(void)
{
    QTestState *qts = qtest_init("-machine k230");
    uint32_t con;
    uint32_t raw;
    uint32_t source;

    /*
     * Master mode disabled.
     */
    con = qtest_readl(qts, K230_I2C0_BASE + K230_IC_CON);
    con &= ~(IC_CON_MASTER_MODE | IC_CON_10BITADDR_MASTER);

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
    g_assert_cmphex(source & IC_ABRT_MASTER_DIS,
                    ==, IC_ABRT_MASTER_DIS);

    qtest_system_reset(qts);

    /*
     * A read command cannot target the General Call address.
     */
    con = qtest_readl(qts, K230_I2C0_BASE + K230_IC_CON);
    con &= ~IC_CON_10BITADDR_MASTER;

    qtest_writel(qts, K230_I2C0_BASE + K230_IC_CON, con);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_TAR,
                 IC_TAR_SPECIAL);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_ENABLE, 1);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_DATA_CMD,
                 IC_DATA_CMD_READ | IC_DATA_CMD_STOP);

    raw = qtest_readl(qts,
                      K230_I2C0_BASE + K230_IC_RAW_INTR_STAT);
    source = qtest_readl(qts,
                         K230_I2C0_BASE + K230_IC_TX_ABRT_SOURCE);

    g_assert_cmphex(raw & IC_INTR_TX_ABRT, ==, IC_INTR_TX_ABRT);
    g_assert_cmphex(source & IC_ABRT_GCALL_READ,
                    ==, IC_ABRT_GCALL_READ);

    qtest_system_reset(qts);

    /*
     * No slave is attached to acknowledge the General Call.
     */
    con = qtest_readl(qts, K230_I2C0_BASE + K230_IC_CON);
    con &= ~IC_CON_10BITADDR_MASTER;

    qtest_writel(qts, K230_I2C0_BASE + K230_IC_CON, con);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_TAR,
                 IC_TAR_SPECIAL);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_ENABLE, 1);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_DATA_CMD,
                 IC_DATA_CMD_STOP | 0x55);

    raw = qtest_readl(qts,
                      K230_I2C0_BASE + K230_IC_RAW_INTR_STAT);
    source = qtest_readl(qts,
                         K230_I2C0_BASE + K230_IC_TX_ABRT_SOURCE);

    g_assert_cmphex(raw & IC_INTR_TX_ABRT, ==, IC_INTR_TX_ABRT);
    g_assert_cmphex(source & IC_ABRT_GCALL_NOACK,
                    ==, IC_ABRT_GCALL_NOACK);

    qtest_system_reset(qts);

    /*
     * 10-bit master addressing is deliberately unsupported.
     */
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_ENABLE, 1);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_DATA_CMD,
                 IC_DATA_CMD_STOP | 0x55);

    raw = qtest_readl(qts,
                      K230_I2C0_BASE + K230_IC_RAW_INTR_STAT);
    source = qtest_readl(qts,
                         K230_I2C0_BASE + K230_IC_TX_ABRT_SOURCE);

    g_assert_cmphex(raw & IC_INTR_TX_ABRT, ==, IC_INTR_TX_ABRT);
    g_assert_cmphex(source, ==, 0);

    qtest_quit(qts);
}

static void test_start_byte_abort(void)
{
    QTestState *qts = qtest_init("-machine k230");
    uint32_t con;
    uint32_t raw;
    uint32_t source;

    /*
     * START BYTE requires RESTART_EN.
     */
    con = qtest_readl(qts, K230_I2C0_BASE + K230_IC_CON);
    con &= ~(IC_CON_10BITADDR_MASTER | IC_CON_RESTART_EN);

    qtest_writel(qts, K230_I2C0_BASE + K230_IC_CON, con);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_TAR,
                 IC_TAR_SPECIAL | IC_TAR_GC_OR_START);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_ENABLE, 1);
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_DATA_CMD,
                 IC_DATA_CMD_STOP | 0x55);

    raw = qtest_readl(qts,
                      K230_I2C0_BASE + K230_IC_RAW_INTR_STAT);
    source = qtest_readl(qts,
                         K230_I2C0_BASE + K230_IC_TX_ABRT_SOURCE);

    g_assert_cmphex(raw & IC_INTR_TX_ABRT, ==, IC_INTR_TX_ABRT);
    g_assert_cmphex(source & IC_ABRT_SBYTE_NORSTRT,
                    ==, IC_ABRT_SBYTE_NORSTRT);

    /*
     * Bit 9 remains set while the invalid configuration remains.
     */
    qtest_readl(qts, K230_I2C0_BASE + K230_IC_CLR_TX_ABRT);

    raw = qtest_readl(qts,
                      K230_I2C0_BASE + K230_IC_RAW_INTR_STAT);
    source = qtest_readl(qts,
                         K230_I2C0_BASE + K230_IC_TX_ABRT_SOURCE);

    g_assert_cmphex(raw & IC_INTR_TX_ABRT, ==, IC_INTR_TX_ABRT);
    g_assert_cmphex(source & IC_ABRT_SBYTE_NORSTRT,
                    ==, IC_ABRT_SBYTE_NORSTRT);

    /*
     * Remove the cause by enabling RESTART_EN, then clear again.
     */
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_ENABLE, 0);

    con |= IC_CON_RESTART_EN;
    qtest_writel(qts, K230_I2C0_BASE + K230_IC_CON, con);

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
    qtest_add_func("/k230-i2c/instance-isolation",
                   test_instance_isolation);
    qtest_add_func("/k230-i2c/register-access",
                   test_register_access);
    qtest_add_func("/k230-i2c/readonly-registers",
                   test_readonly_registers);
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
    qtest_add_func("/k230-i2c/system-reset",
                   test_system_reset);
    qtest_add_func("/k230-i2c/clear-on-read",
                   test_clear_on_read);
    qtest_add_func("/k230-i2c/abort-sources",
                   test_abort_sources);
    qtest_add_func("/k230-i2c/start-byte-abort",
                   test_start_byte_abort);

    return g_test_run();
}
