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
#define K230_IC_CLR_RX_OVER            0x48
#define K230_IC_CLR_TX_ABRT            0x54
#define K230_IC_CLR_ACTIVITY           0x5c
#define K230_IC_CLR_STOP_DET           0x60
#define K230_IC_CLR_START_DET          0x64
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
#define IC_DATA_CMD_RESTART            BIT(10)

/* IC_INTR_STAT and IC_RAW_INTR_STAT */
#define IC_INTR_RX_UNDER               BIT(0)
#define IC_INTR_RX_OVER                BIT(1)
#define IC_INTR_RX_FULL                BIT(2)
#define IC_INTR_TX_EMPTY               BIT(4)
#define IC_INTR_TX_ABRT                BIT(6)
#define IC_INTR_ACTIVITY               BIT(8)
#define IC_INTR_STOP_DET               BIT(9)
#define IC_INTR_START_DET              BIT(10)

/* IC_STATUS */
#define IC_STATUS_ACTIVITY             BIT(0)
#define IC_STATUS_TFNF                 BIT(1)
#define IC_STATUS_TFE                  BIT(2)
#define IC_STATUS_RFNE                 BIT(3)
#define IC_STATUS_RFF                  BIT(4)
#define IC_STATUS_MST_ACTIVITY         BIT(5)

/* IC_TX_ABRT_SOURCE */
#define IC_ABRT_7B_ADDR_NOACK          BIT(0)
#define IC_ABRT_TXDATA_NOACK           BIT(3)
#define IC_ABRT_GCALL_NOACK            BIT(4)
#define IC_ABRT_GCALL_READ             BIT(5)
#define IC_ABRT_SBYTE_NORSTRT          BIT(9)
#define IC_ABRT_MASTER_DIS             BIT(11)

#define K230_IC_COMP_PARAM_1_VALUE     0x001f3fae
#define K230_IC_COMP_VERSION_VALUE     0x3132302a
#define K230_IC_COMP_TYPE_VALUE        0x44570140

#define TMP105_ADDRESS                  0x49
#define I2C_ECHO_ADDRESS                0x50

#define TMP105_REG_TEMPERATURE          0x00
#define TMP105_REG_CONFIG               0x01
#define TMP105_REG_T_LOW                0x02
#define TMP105_REG_T_HIGH               0x03

#define K230_I2C_SLAVE_MACHINE_ARGS                             \
    "-machine k230 "                                            \
    "-device tmp105,id=tmp105-test,address=0x49 "                \
    "-device i2c-echo,id=i2c-echo-test,address=0x50"

static const uint64_t k230_i2c_base[K230_I2C_COUNT] = {
    K230_I2C0_BASE,
    K230_I2C1_BASE,
    K230_I2C2_BASE,
    K230_I2C3_BASE,
    K230_I2C4_BASE,
};

static QTestState *k230_i2c_init_with_slaves(void)
{
    return qtest_init(K230_I2C_SLAVE_MACHINE_ARGS);
}

static void k230_i2c_configure_master(QTestState *qts, uint64_t base,
                                      uint8_t target, bool restart_enable)
{
    uint32_t con;

    qtest_writel(qts, base + K230_IC_ENABLE, 0);

    con = qtest_readl(qts, base + K230_IC_CON);
    con |= IC_CON_MASTER_MODE;
    con &= ~IC_CON_10BITADDR_MASTER;

    if (restart_enable) {
        con |= IC_CON_RESTART_EN;
    } else {
        con &= ~IC_CON_RESTART_EN;
    }

    qtest_writel(qts, base + K230_IC_CON, con);
    qtest_writel(qts, base + K230_IC_TAR, target);
    qtest_writel(qts, base + K230_IC_ENABLE, 1);
}

static void k230_i2c_assert_no_abort(QTestState *qts, uint64_t base)
{
    uint32_t raw;

    raw = qtest_readl(qts, base + K230_IC_RAW_INTR_STAT);

    g_assert_cmphex(raw & IC_INTR_TX_ABRT, ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + K230_IC_TX_ABRT_SOURCE),
                    ==, 0);
}

static uint64_t k230_i2c_find_tmp105_bus(QTestState *qts)
{
    for (int i = 0; i < K230_I2C_COUNT; i++) {
        uint64_t base = k230_i2c_base[i];
        uint32_t raw;

        k230_i2c_configure_master(qts, base, TMP105_ADDRESS, true);

        /*
         * A one-byte write selects the temperature register.  The
         * controller whose bus contains TMP105 completes without aborting.
         */
        qtest_writel(qts, base + K230_IC_DATA_CMD,
                     IC_DATA_CMD_STOP | TMP105_REG_TEMPERATURE);

        raw = qtest_readl(qts, base + K230_IC_RAW_INTR_STAT);

        if (!(raw & IC_INTR_TX_ABRT)) {
            qtest_writel(qts, base + K230_IC_ENABLE, 0);
            return base;
        }

        qtest_readl(qts, base + K230_IC_CLR_TX_ABRT);
        qtest_writel(qts, base + K230_IC_ENABLE, 0);
    }

    g_assert_not_reached();
}

static void k230_i2c_write_bytes(QTestState *qts, uint64_t base,
                                 const uint8_t *data, size_t len)
{
    g_assert_cmpuint(len, >, 0);

    for (size_t i = 0; i < len; i++) {
        uint32_t command = data[i];

        if (i == len - 1) {
            command |= IC_DATA_CMD_STOP;
        }

        qtest_writel(qts, base + K230_IC_DATA_CMD, command);
    }

    k230_i2c_assert_no_abort(qts, base);
}

static void k230_i2c_select_register(QTestState *qts, uint64_t base,
                                     uint8_t reg)
{
    qtest_writel(qts, base + K230_IC_DATA_CMD,
                 IC_DATA_CMD_STOP | reg);
    k230_i2c_assert_no_abort(qts, base);
}

static void k230_i2c_queue_reads(QTestState *qts, uint64_t base,
                                 size_t len, bool restart_first,
                                 bool stop_last)
{
    g_assert_cmpuint(len, >, 0);

    for (size_t i = 0; i < len; i++) {
        uint32_t command = IC_DATA_CMD_READ;

        if (i == 0 && restart_first) {
            command |= IC_DATA_CMD_RESTART;
        }

        if (i == len - 1 && stop_last) {
            command |= IC_DATA_CMD_STOP;
        }

        qtest_writel(qts, base + K230_IC_DATA_CMD, command);
    }

    k230_i2c_assert_no_abort(qts, base);
}

static void k230_i2c_tmp105_combined_read(QTestState *qts, uint64_t base,
                                          uint8_t reg, size_t len)
{
    qtest_writel(qts, base + K230_IC_DATA_CMD, reg);
    k230_i2c_queue_reads(qts, base, len, true, true);
}

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

static void test_tmp105_config_read_write(void)
{
    static const uint8_t write_data[] = {
        TMP105_REG_CONFIG,
        0x60,
    };
    QTestState *qts = k230_i2c_init_with_slaves();
    uint64_t base = k230_i2c_find_tmp105_bus(qts);

    k230_i2c_configure_master(qts, base, TMP105_ADDRESS, true);

    k230_i2c_write_bytes(qts, base, write_data,
                         G_N_ELEMENTS(write_data));

    k230_i2c_tmp105_combined_read(qts, base,
                                  TMP105_REG_CONFIG, 1);

    g_assert_cmphex(qtest_readl(qts, base + K230_IC_RXFLR),
                    ==, 1);
    g_assert_cmphex(qtest_readl(qts, base + K230_IC_DATA_CMD),
                    ==, 0x60);
    g_assert_cmphex(qtest_readl(qts, base + K230_IC_RXFLR),
                    ==, 0);

    k230_i2c_assert_no_abort(qts, base);
    qtest_quit(qts);
}

static void test_tmp105_limit_read_write(void)
{
    static const uint8_t write_data[] = {
        TMP105_REG_T_LOW,
        0x12,
        0x34,
    };
    QTestState *qts = k230_i2c_init_with_slaves();
    uint64_t base = k230_i2c_find_tmp105_bus(qts);

    k230_i2c_configure_master(qts, base, TMP105_ADDRESS, true);

    k230_i2c_write_bytes(qts, base, write_data,
                         G_N_ELEMENTS(write_data));

    k230_i2c_tmp105_combined_read(qts, base,
                                  TMP105_REG_T_LOW, 2);

    g_assert_cmphex(qtest_readl(qts, base + K230_IC_RXFLR),
                    ==, 2);
    g_assert_cmphex(qtest_readl(qts, base + K230_IC_DATA_CMD),
                    ==, 0x12);
    g_assert_cmphex(qtest_readl(qts, base + K230_IC_DATA_CMD),
                    ==, 0x30);
    g_assert_cmphex(qtest_readl(qts, base + K230_IC_RXFLR),
                    ==, 0);

    k230_i2c_assert_no_abort(qts, base);
    qtest_quit(qts);
}

static void test_tmp105_direct_read(void)
{
    static const uint8_t write_data[] = {
        TMP105_REG_CONFIG,
        0x20,
    };
    QTestState *qts = k230_i2c_init_with_slaves();
    uint64_t base = k230_i2c_find_tmp105_bus(qts);

    k230_i2c_configure_master(qts, base, TMP105_ADDRESS, true);

    k230_i2c_write_bytes(qts, base, write_data,
                         G_N_ELEMENTS(write_data));
    k230_i2c_select_register(qts, base, TMP105_REG_CONFIG);

    /*
     * Start a read while the controller is idle.  This covers the
     * IDLE-to-RECEIVING path without a preceding repeated START.
     */
    k230_i2c_queue_reads(qts, base, 1, false, true);

    g_assert_cmphex(qtest_readl(qts, base + K230_IC_RXFLR),
                    ==, 1);
    g_assert_cmphex(qtest_readl(qts, base + K230_IC_DATA_CMD),
                    ==, 0x20);

    k230_i2c_assert_no_abort(qts, base);
    qtest_quit(qts);
}

static void test_tmp105_read_write_restart(void)
{
    static const uint8_t initial_data[] = {
        TMP105_REG_CONFIG,
        0x20,
    };
    QTestState *qts = k230_i2c_init_with_slaves();
    uint64_t base = k230_i2c_find_tmp105_bus(qts);

    k230_i2c_configure_master(qts, base, TMP105_ADDRESS, true);

    k230_i2c_write_bytes(qts, base, initial_data,
                         G_N_ELEMENTS(initial_data));
    k230_i2c_select_register(qts, base, TMP105_REG_CONFIG);

    /*
     * Leave the read transfer active, then switch from receiving to
     * sending with a repeated START.
     */
    k230_i2c_queue_reads(qts, base, 1, false, false);

    qtest_writel(qts, base + K230_IC_DATA_CMD,
                 IC_DATA_CMD_RESTART | TMP105_REG_CONFIG);
    qtest_writel(qts, base + K230_IC_DATA_CMD,
                 IC_DATA_CMD_STOP | 0x60);

    g_assert_cmphex(qtest_readl(qts, base + K230_IC_RXFLR),
                    ==, 1);
    g_assert_cmphex(qtest_readl(qts, base + K230_IC_DATA_CMD),
                    ==, 0x20);

    k230_i2c_tmp105_combined_read(qts, base,
                                  TMP105_REG_CONFIG, 1);

    g_assert_cmphex(qtest_readl(qts, base + K230_IC_DATA_CMD),
                    ==, 0x60);

    k230_i2c_assert_no_abort(qts, base);
    qtest_quit(qts);
}

static void test_tmp105_same_direction_restart(void)
{
    QTestState *qts = k230_i2c_init_with_slaves();
    uint64_t base = k230_i2c_find_tmp105_bus(qts);

    k230_i2c_configure_master(qts, base, TMP105_ADDRESS, true);

    /*
     * Start in the sending state, issue an explicit repeated START
     * without changing direction, and then write the config register.
     */
    qtest_writel(qts, base + K230_IC_DATA_CMD,
                 TMP105_REG_T_LOW);
    qtest_writel(qts, base + K230_IC_DATA_CMD,
                 IC_DATA_CMD_RESTART | TMP105_REG_CONFIG);
    qtest_writel(qts, base + K230_IC_DATA_CMD,
                 IC_DATA_CMD_STOP | 0x40);

    k230_i2c_assert_no_abort(qts, base);

    k230_i2c_tmp105_combined_read(qts, base,
                                  TMP105_REG_CONFIG, 1);

    g_assert_cmphex(qtest_readl(qts, base + K230_IC_DATA_CMD),
                    ==, 0x40);

    k230_i2c_assert_no_abort(qts, base);
    qtest_quit(qts);
}

static void test_transfer_status_with_slave(void)
{
    QTestState *qts = k230_i2c_init_with_slaves();
    uint64_t base = k230_i2c_find_tmp105_bus(qts);
    uint32_t raw;
    uint32_t status;

    k230_i2c_configure_master(qts, base, TMP105_ADDRESS, true);
    qtest_readl(qts, base + K230_IC_CLR_INTR);

    qtest_writel(qts, base + K230_IC_DATA_CMD,
                 TMP105_REG_CONFIG);

    status = qtest_readl(qts, base + K230_IC_STATUS);
    g_assert_cmphex(status & IC_STATUS_ACTIVITY,
                    ==, IC_STATUS_ACTIVITY);
    g_assert_cmphex(status & IC_STATUS_MST_ACTIVITY,
                    ==, IC_STATUS_MST_ACTIVITY);
    g_assert_cmphex(status & IC_STATUS_TFNF,
                    ==, IC_STATUS_TFNF);
    g_assert_cmphex(status & IC_STATUS_TFE,
                    ==, IC_STATUS_TFE);

    raw = qtest_readl(qts, base + K230_IC_RAW_INTR_STAT);
    g_assert_cmphex(raw & IC_INTR_START_DET,
                    ==, IC_INTR_START_DET);
    g_assert_cmphex(raw & IC_INTR_ACTIVITY,
                    ==, IC_INTR_ACTIVITY);

    qtest_writel(qts, base + K230_IC_DATA_CMD,
                 IC_DATA_CMD_STOP | 0x20);

    status = qtest_readl(qts, base + K230_IC_STATUS);
    g_assert_cmphex(status & IC_STATUS_ACTIVITY, ==, 0);
    g_assert_cmphex(status & IC_STATUS_MST_ACTIVITY, ==, 0);
    g_assert_cmphex(status & IC_STATUS_TFNF,
                    ==, IC_STATUS_TFNF);
    g_assert_cmphex(status & IC_STATUS_TFE,
                    ==, IC_STATUS_TFE);

    raw = qtest_readl(qts, base + K230_IC_RAW_INTR_STAT);
    g_assert_cmphex(raw & IC_INTR_STOP_DET,
                    ==, IC_INTR_STOP_DET);
    g_assert_cmphex(raw & IC_INTR_ACTIVITY,
                    ==, IC_INTR_ACTIVITY);

    qtest_readl(qts, base + K230_IC_CLR_START_DET);
    qtest_readl(qts, base + K230_IC_CLR_STOP_DET);
    qtest_readl(qts, base + K230_IC_CLR_ACTIVITY);

    raw = qtest_readl(qts, base + K230_IC_RAW_INTR_STAT);
    g_assert_cmphex(raw & IC_INTR_START_DET, ==, 0);
    g_assert_cmphex(raw & IC_INTR_STOP_DET, ==, 0);
    g_assert_cmphex(raw & IC_INTR_ACTIVITY, ==, 0);

    k230_i2c_assert_no_abort(qts, base);
    qtest_quit(qts);
}

static void test_rx_threshold_with_slave(void)
{
    QTestState *qts = k230_i2c_init_with_slaves();
    uint64_t base = k230_i2c_find_tmp105_bus(qts);
    uint32_t raw;
    uint32_t stat;

    k230_i2c_configure_master(qts, base, TMP105_ADDRESS, true);
    qtest_writel(qts, base + K230_IC_RX_TL, 1);
    qtest_writel(qts, base + K230_IC_INTR_MASK,
                 IC_INTR_RX_FULL);

    k230_i2c_select_register(qts, base, TMP105_REG_T_LOW);
    qtest_readl(qts, base + K230_IC_CLR_INTR);

    k230_i2c_queue_reads(qts, base, 1, false, false);

    raw = qtest_readl(qts, base + K230_IC_RAW_INTR_STAT);
    g_assert_cmphex(raw & IC_INTR_RX_FULL, ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + K230_IC_RXFLR),
                    ==, 1);

    k230_i2c_queue_reads(qts, base, 1, false, true);

    raw = qtest_readl(qts, base + K230_IC_RAW_INTR_STAT);
    stat = qtest_readl(qts, base + K230_IC_INTR_STAT);

    g_assert_cmphex(raw & IC_INTR_RX_FULL,
                    ==, IC_INTR_RX_FULL);
    g_assert_cmphex(stat & IC_INTR_RX_FULL,
                    ==, IC_INTR_RX_FULL);
    g_assert_cmphex(qtest_readl(qts, base + K230_IC_RXFLR),
                    ==, 2);

    g_assert_cmphex(qtest_readl(qts, base + K230_IC_DATA_CMD),
                    ==, 0x4b);

    raw = qtest_readl(qts, base + K230_IC_RAW_INTR_STAT);
    g_assert_cmphex(raw & IC_INTR_RX_FULL, ==, 0);

    g_assert_cmphex(qtest_readl(qts, base + K230_IC_DATA_CMD),
                    ==, 0x00);

    k230_i2c_assert_no_abort(qts, base);
    qtest_quit(qts);
}

static void test_rx_overflow_with_slave(void)
{
    QTestState *qts = k230_i2c_init_with_slaves();
    uint64_t base = k230_i2c_find_tmp105_bus(qts);
    uint32_t raw;
    uint32_t status;

    k230_i2c_configure_master(qts, base, TMP105_ADDRESS, true);
    qtest_writel(qts, base + K230_IC_RX_TL, 63);
    qtest_writel(qts, base + K230_IC_INTR_MASK,
                 IC_INTR_RX_FULL | IC_INTR_RX_OVER);

    k230_i2c_select_register(qts, base, TMP105_REG_T_LOW);
    qtest_readl(qts, base + K230_IC_CLR_INTR);

    /*
     * TMP105 returns two register bytes and then 0xff.  Sixty-five
     * read commands therefore fill the 64-byte RX FIFO and overflow
     * on the final command.
     */
    k230_i2c_queue_reads(qts, base, 65, false, true);

    g_assert_cmphex(qtest_readl(qts, base + K230_IC_RXFLR),
                    ==, 64);

    status = qtest_readl(qts, base + K230_IC_STATUS);
    g_assert_cmphex(status & IC_STATUS_RFNE,
                    ==, IC_STATUS_RFNE);
    g_assert_cmphex(status & IC_STATUS_RFF,
                    ==, IC_STATUS_RFF);

    raw = qtest_readl(qts, base + K230_IC_RAW_INTR_STAT);
    g_assert_cmphex(raw & IC_INTR_RX_FULL,
                    ==, IC_INTR_RX_FULL);
    g_assert_cmphex(raw & IC_INTR_RX_OVER,
                    ==, IC_INTR_RX_OVER);

    g_assert_cmphex(qtest_readl(qts, base + K230_IC_DATA_CMD),
                    ==, 0x4b);
    g_assert_cmphex(qtest_readl(qts, base + K230_IC_DATA_CMD),
                    ==, 0x00);

    for (int i = 0; i < 62; i++) {
        g_assert_cmphex(qtest_readl(qts,
                                   base + K230_IC_DATA_CMD),
                        ==, 0xff);
    }

    g_assert_cmphex(qtest_readl(qts, base + K230_IC_RXFLR),
                    ==, 0);

    status = qtest_readl(qts, base + K230_IC_STATUS);
    g_assert_cmphex(status & IC_STATUS_RFNE, ==, 0);
    g_assert_cmphex(status & IC_STATUS_RFF, ==, 0);

    raw = qtest_readl(qts, base + K230_IC_RAW_INTR_STAT);
    g_assert_cmphex(raw & IC_INTR_RX_FULL, ==, 0);
    g_assert_cmphex(raw & IC_INTR_RX_OVER,
                    ==, IC_INTR_RX_OVER);

    qtest_readl(qts, base + K230_IC_CLR_RX_OVER);

    raw = qtest_readl(qts, base + K230_IC_RAW_INTR_STAT);
    g_assert_cmphex(raw & IC_INTR_RX_OVER, ==, 0);

    k230_i2c_assert_no_abort(qts, base);
    qtest_quit(qts);
}

static void test_restart_disabled_abort(void)
{
    QTestState *qts = k230_i2c_init_with_slaves();
    uint64_t base = k230_i2c_find_tmp105_bus(qts);
    uint32_t raw;
    uint32_t status;

    k230_i2c_configure_master(qts, base, TMP105_ADDRESS, false);

    qtest_writel(qts, base + K230_IC_DATA_CMD,
                 TMP105_REG_CONFIG);
    qtest_writel(qts, base + K230_IC_DATA_CMD,
                 IC_DATA_CMD_READ | IC_DATA_CMD_STOP);

    raw = qtest_readl(qts, base + K230_IC_RAW_INTR_STAT);
    status = qtest_readl(qts, base + K230_IC_STATUS);

    g_assert_cmphex(raw & IC_INTR_TX_ABRT,
                    ==, IC_INTR_TX_ABRT);
    g_assert_cmphex(qtest_readl(qts,
                               base + K230_IC_TX_ABRT_SOURCE),
                    ==, 0);
    g_assert_cmphex(status & IC_STATUS_ACTIVITY, ==, 0);
    g_assert_cmphex(status & IC_STATUS_MST_ACTIVITY, ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + K230_IC_TXFLR),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + K230_IC_RXFLR),
                    ==, 0);

    qtest_readl(qts, base + K230_IC_CLR_TX_ABRT);

    raw = qtest_readl(qts, base + K230_IC_RAW_INTR_STAT);
    g_assert_cmphex(raw & IC_INTR_TX_ABRT, ==, 0);

    qtest_quit(qts);
}

static void test_echo_data_nack(void)
{
    static const uint8_t accepted_data[] = {
        0x7f,
        0x11,
        0x22,
    };
    QTestState *qts = k230_i2c_init_with_slaves();
    uint64_t base = k230_i2c_find_tmp105_bus(qts);
    uint32_t raw;
    uint32_t source;
    uint32_t status;

    k230_i2c_configure_master(qts, base, I2C_ECHO_ADDRESS, true);

    for (size_t i = 0; i < G_N_ELEMENTS(accepted_data); i++) {
        qtest_writel(qts, base + K230_IC_DATA_CMD,
                     accepted_data[i]);
    }

    /*
     * i2c-echo accepts three bytes.  The fourth byte is rejected,
     * allowing the controller's TXDATA_NOACK path to be tested.
     */
    qtest_writel(qts, base + K230_IC_DATA_CMD,
                 IC_DATA_CMD_STOP | 0x33);

    raw = qtest_readl(qts, base + K230_IC_RAW_INTR_STAT);
    source = qtest_readl(qts, base + K230_IC_TX_ABRT_SOURCE);
    status = qtest_readl(qts, base + K230_IC_STATUS);

    g_assert_cmphex(raw & IC_INTR_TX_ABRT,
                    ==, IC_INTR_TX_ABRT);
    g_assert_cmphex(source & IC_ABRT_TXDATA_NOACK,
                    ==, IC_ABRT_TXDATA_NOACK);
    g_assert_cmphex(source & IC_ABRT_7B_ADDR_NOACK, ==, 0);
    g_assert_cmphex(status & IC_STATUS_ACTIVITY, ==, 0);
    g_assert_cmphex(status & IC_STATUS_MST_ACTIVITY, ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + K230_IC_TXFLR),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + K230_IC_RXFLR),
                    ==, 0);

    qtest_readl(qts, base + K230_IC_CLR_TX_ABRT);

    raw = qtest_readl(qts, base + K230_IC_RAW_INTR_STAT);
    source = qtest_readl(qts, base + K230_IC_TX_ABRT_SOURCE);

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

    if (qtest_has_device("tmp105") && qtest_has_device("i2c-echo")) {
        qtest_add_func("/k230-i2c/tmp105-config-read-write",
                       test_tmp105_config_read_write);
        qtest_add_func("/k230-i2c/tmp105-limit-read-write",
                       test_tmp105_limit_read_write);
        qtest_add_func("/k230-i2c/tmp105-direct-read",
                       test_tmp105_direct_read);
        qtest_add_func("/k230-i2c/tmp105-read-write-restart",
                       test_tmp105_read_write_restart);
        qtest_add_func("/k230-i2c/tmp105-same-direction-restart",
                       test_tmp105_same_direction_restart);
        qtest_add_func("/k230-i2c/transfer-status-with-slave",
                       test_transfer_status_with_slave);
        qtest_add_func("/k230-i2c/rx-threshold-with-slave",
                       test_rx_threshold_with_slave);
        qtest_add_func("/k230-i2c/rx-overflow-with-slave",
                       test_rx_overflow_with_slave);
        qtest_add_func("/k230-i2c/restart-disabled-abort",
                       test_restart_disabled_abort);
        qtest_add_func("/k230-i2c/echo-data-nack",
                       test_echo_data_nack);
    }

    return g_test_run();
}
