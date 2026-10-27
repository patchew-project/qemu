/*
 * QTest testcase for Aspeed I2C controller
 *
 * Copyright (c) 2026 Timur Maksimov <gessingate@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"
#include "hw/i2c/aspeed_i2c.h"

typedef enum TestAspeedI2CMode {
    TEST_I2C_MODE_OLD,
    TEST_I2C_MODE_NEW,
    TEST_I2C_MODE_PKT,
} TestAspeedI2CMode;

typedef enum TestAspeedMachine {
    TEST_MACHINE_AST2400,
    TEST_MACHINE_AST2500,
    TEST_MACHINE_AST2600,
} TestAspeedMachine;

typedef struct TestData {
    const char *name;
    TestAspeedMachine machine;
    TestAspeedI2CMode mode;
} TestData;

typedef struct TestFixture {
    hwaddr base_addr;
    hwaddr bus_offset;
    int irq_no;
    uint32_t pool_base;
} TestFixture;

static const uint8_t tx_data[] = {0x10, 0x11, 0x12};
static const uint8_t rx_data[] = {0x30, 0x31, 0x32};

#define DRAM_BASE      0x80000000
#define DRAM_TX_OFFSET 0x1000
#define DRAM_RX_OFFSET 0x2000

static void aspeed_i2c_write(TestFixture *f, uint32_t offset, uint32_t val)
{
    qtest_writel(global_qtest, f->base_addr + f->bus_offset + offset, val);
}

static void aspeed_i2c_write_pool(TestFixture *f, uint32_t offset, uint8_t val)
{
    qtest_writeb(global_qtest, f->base_addr + f->pool_base + offset, val);
}

static void aspeed_i2c_write_dma(TestFixture *f, uint32_t offset, uint8_t val)
{
    qtest_writeb(global_qtest, DRAM_BASE + offset, val);
}

static uint32_t aspeed_i2c_read(TestFixture *f, uint32_t offset)
{
    return qtest_readl(global_qtest, f->base_addr + f->bus_offset + offset);
}

static uint8_t aspeed_i2c_read_pool(TestFixture *f, uint32_t offset)
{
    return qtest_readb(global_qtest, f->base_addr + f->pool_base + offset);
}

static uint8_t aspeed_i2c_read_dma(TestFixture *f, uint32_t offset)
{
    return qtest_readb(global_qtest, DRAM_BASE + offset);
}

static bool aspeed_i2c_irq(TestFixture *f)
{
    return qtest_get_irq(global_qtest, f->irq_no);
}

static uint32_t aspeed_i2c_ctrl_offset(const TestData *td)
{
    if (td->mode == TEST_I2C_MODE_OLD) {
        return A_I2CD_FUN_CTRL;
    } else {
        return A_I2CC_FUN_CTRL;
    }
}

static uint32_t aspeed_i2c_cmd_offset(const TestData *td)
{
    if (td->mode == TEST_I2C_MODE_OLD) {
        return A_I2CD_CMD;
    } else {
        return A_I2CM_CMD;
    }
}


static uint32_t aspeed_i2c_intr_ctrl_offset(const TestData *td)
{
    if (td->mode == TEST_I2C_MODE_OLD) {
        return A_I2CD_INTR_CTRL;
    } else {
        return A_I2CM_INTR_CTRL;
    }
}

static uint32_t aspeed_i2c_intr_sts_offset(const TestData *td)
{
    if (td->mode == TEST_I2C_MODE_OLD) {
        return A_I2CD_INTR_STS;
    } else {
        return A_I2CM_INTR_STS;
    }
}

static uint32_t aspeed_i2c_pool_ctrl_offset(const TestData *td)
{
    if (td->mode == TEST_I2C_MODE_OLD) {
        return A_I2CD_POOL_CTRL;
    } else {
        return A_I2CC_POOL_CTRL;
    }
}

static uint32_t aspeed_i2c_byte_buf_offset(const TestData *td)
{
    if (td->mode == TEST_I2C_MODE_OLD) {
        return A_I2CD_BYTE_BUF;
    } else {
        return A_I2CC_MS_TXRX_BYTE_BUF;
    }
}

static void aspeed_i2c_write_slave_addr(TestFixture *f, const TestData *td,
                                        uint8_t addr)
{
    if (td->mode == TEST_I2C_MODE_OLD) {
        aspeed_i2c_write(f, A_I2CD_DEV_ADDR, addr);
    } else {
        aspeed_i2c_write(f, A_I2CS_DEV_ADDR, addr);
    }
}

static void aspeed_i2c_write_dma_addr(TestFixture *f, const TestData *td,
                                     uint32_t val, bool rx)
{
    uint32_t offset;

    if (td->mode == TEST_I2C_MODE_OLD) {
        offset = A_I2CD_DMA_ADDR;
    } else {
        offset = rx ? A_I2CM_DMA_RX_ADDR : A_I2CM_DMA_TX_ADDR;
    }
    aspeed_i2c_write(f, offset, val);
}

static void aspeed_i2c_write_dma_len(TestFixture *f, const TestData *td,
                                     uint16_t val, bool rx)
{
    uint32_t reg;

    if (td->mode == TEST_I2C_MODE_OLD) {
        aspeed_i2c_write(f, A_I2CD_DMA_LEN, val + 1);
        return;
    }

    if (rx) {
        reg = FIELD_DP32(0, I2CM_DMA_LEN, RX_BUF_LEN_W1T, 1);
        reg = FIELD_DP32(reg, I2CM_DMA_LEN, RX_BUF_LEN, val);
    } else {
        reg = FIELD_DP32(0, I2CM_DMA_LEN, TX_BUF_LEN_W1T, 1);
        reg = FIELD_DP32(reg, I2CM_DMA_LEN, TX_BUF_LEN, val);
    }
    aspeed_i2c_write(f, A_I2CM_DMA_LEN, reg);
}

static uint8_t aspeed_i2c_get_txstate(TestFixture *f, const TestData *td)
{
    uint32_t offset;

    if (td->mode == TEST_I2C_MODE_OLD) {
        offset = A_I2CD_CMD;
    } else {
        offset = A_I2CC_MS_TXRX_BYTE_BUF;
    }
    return SHARED_FIELD_EX32(aspeed_i2c_read(f, offset), TX_STATE);
}

static bool aspeed_i2c_get_busy_sts(TestFixture *f, const TestData *td)
{
    uint32_t offset;

    if (td->mode == TEST_I2C_MODE_OLD) {
        offset = A_I2CD_CMD;
    } else {
        offset = A_I2CC_MS_TXRX_BYTE_BUF;
    }
    return SHARED_FIELD_EX32(aspeed_i2c_read(f, offset), BUS_BUSY_STS);
}

/* Write a data byte in a byte mode transfer */
static
void aspeed_i2c_write_byte(TestFixture *f, const TestData *td, uint8_t val)
{
    aspeed_i2c_write(f, aspeed_i2c_byte_buf_offset(td),
                     SHARED_FIELD_DP32(0, TX_BUF, val));
}

/* Read a data byte in a byte mode transfer */
static uint8_t aspeed_i2c_read_byte(TestFixture *f, const TestData *td)
{
    uint32_t reg;

    reg = aspeed_i2c_read(f, aspeed_i2c_byte_buf_offset(td));
    return SHARED_FIELD_EX32(reg, RX_BUF);
}

static void aspeed_i2c_send_start(TestFixture *f, const TestData *td,
                                  uint32_t cmd)
{
    cmd = SHARED_FIELD_DP32(cmd, M_START_CMD, 1);
    aspeed_i2c_write(f, aspeed_i2c_cmd_offset(td), cmd);
}

static uint8_t qom_i2c_dummy_get_uint8(const char *prop)
{
    QDict *r;
    uint8_t u;

    r = qtest_qmp(global_qtest,
                  "{ 'execute': 'qom-get', 'arguments': "
                  "{ 'path': 'i2c-dummy', 'property': %s } }",
                  prop);
    u = qdict_get_uint(r, "return");
    qobject_unref(r);
    return u;
}

static void qom_i2c_dummy_set_uint8(const char *prop, uint8_t data)
{
    QDict *r;

    r = qtest_qmp(global_qtest,
                  "{ 'execute': 'qom-set', 'arguments': "
                  "{ 'path': 'i2c-dummy', 'property': %s, 'value': %d } }",
                  prop, data);
    qobject_unref(r);
}

static void qom_i2c_dummy_set_rx(uint8_t idx, uint8_t data)
{
    g_autofree char *prop = g_strdup_printf("rx[%d]", idx);

    qom_i2c_dummy_set_uint8(prop, data);
}

static uint8_t qom_i2c_dummy_get_tx(int8_t idx)
{
    g_autofree char *prop = g_strdup_printf("tx[%d]", idx);

    return qom_i2c_dummy_get_uint8(prop);
}

#define I2C_ADDR_NACK  0x10
#define I2C_ADDR_DUMMY 0x20
#define I2C_ADDR_ECHO  0x30
#define I2C_ADDR_SLAVE 0x40

#define CHECK_I2C_IDLE(f, td) do {                                          \
    g_assert_cmphex(aspeed_i2c_get_txstate(f, td), ==, I2CD_IDLE);          \
    g_assert_false(aspeed_i2c_get_busy_sts(f, td));                         \
    g_assert_cmphex(aspeed_i2c_read(f, aspeed_i2c_intr_sts_offset(td)), ==, \
                    0);                                                     \
    g_assert_false(aspeed_i2c_irq(f));                                      \
} while (0)

#define CHECK_I2C_INTR(f, td, reg) do {                                     \
    g_assert_true(aspeed_i2c_irq(f));                                       \
    g_assert_cmphex(aspeed_i2c_read(f, aspeed_i2c_intr_sts_offset(td)), ==, \
                    reg);                                                   \
} while (0)

#define CHECK_I2C_BUS_ACTIVE(f, td) do {                                    \
    g_assert_cmphex(aspeed_i2c_get_txstate(f, td), ==, I2CD_MACTIVE);       \
    g_assert_true(aspeed_i2c_get_busy_sts(f, td));                          \
} while (0)

#define CHECK_I2C_ACK(f, td) do {                                           \
    g_assert_cmphex(aspeed_i2c_get_txstate(f, td), ==, I2CD_MACTIVE);       \
    g_assert_true(aspeed_i2c_get_busy_sts(f, td));                          \
    CLEAR_I2C_INTR(f, td, SHARED_FIELD_DP32(0, TX_ACK, 1));                 \
} while (0)

#define CLEAR_I2C_INTR(f, td, reg) do {                                     \
    CHECK_I2C_INTR(f, td, reg);                                             \
    aspeed_i2c_write(f, aspeed_i2c_intr_sts_offset(td), reg);               \
    g_assert_cmphex(aspeed_i2c_read(f, aspeed_i2c_intr_sts_offset(td)), ==, \
                    0);                                                     \
} while (0)

#define SEND_I2C_STOP(f, td) do {                                           \
    aspeed_i2c_write(f, aspeed_i2c_cmd_offset(td),                          \
                     SHARED_FIELD_DP32(0, M_STOP_CMD, 1));                  \
    CLEAR_I2C_INTR(f, td, SHARED_FIELD_DP32(0, NORMAL_STOP, 1));            \
    CHECK_I2C_IDLE(f, td);                                                  \
} while (0)

#define CHECK_I2C_DUMMY_DEVICE() do {                                       \
     for (uint8_t i = 0; i < ARRAY_SIZE(tx_data); i++) {                    \
        g_assert_cmphex(qom_i2c_dummy_get_tx(i), ==, tx_data[i]);           \
    }                                                                       \
    g_assert_cmphex(qom_i2c_dummy_get_uint8("tx-pos"), ==,                  \
                    ARRAY_SIZE(tx_data));                                   \
    g_assert_cmphex(qom_i2c_dummy_get_uint8("rx-pos"), ==,                  \
                    ARRAY_SIZE(rx_data));                                   \
} while (0)

static void aspeed_i2c_init(TestFixture *f, const TestData *td)
{
    uint32_t reg;

    qtest_qmp(global_qtest, "{ 'execute': 'system_reset' }");

    aspeed_i2c_write_slave_addr(f, td, I2C_ADDR_SLAVE);

    reg = FIELD_DP32(0, I2C_CTRL_GLOBAL, SRAM_EN, 1);
    if (td->mode != TEST_I2C_MODE_OLD) {
        reg = FIELD_DP32(reg, I2C_CTRL_GLOBAL, REG_MODE, 1);
    }
    qtest_writel(global_qtest, f->base_addr + A_I2C_CTRL_GLOBAL, reg);

    reg = SHARED_FIELD_DP32(0, MASTER_EN, 1);
    reg = SHARED_FIELD_DP32(reg, SLAVE_EN, 1);
    aspeed_i2c_write(f, aspeed_i2c_ctrl_offset(td), reg);

    /* Enable all interrupts */
    reg = SHARED_FIELD_DP32(0, TX_ACK, 1);
    reg = SHARED_FIELD_DP32(reg, TX_NAK, 1);
    reg = SHARED_FIELD_DP32(reg, RX_DONE, 1);
    reg = SHARED_FIELD_DP32(reg, ARBIT_LOSS, 1);
    reg = SHARED_FIELD_DP32(reg, NORMAL_STOP, 1);
    reg = SHARED_FIELD_DP32(reg, ABNORMAL, 1);
    if (td->mode != TEST_I2C_MODE_OLD) {
        reg = FIELD_DP32(reg, I2CM_INTR_STS, PKT_CMD_DONE, 1);
        reg = FIELD_DP32(reg, I2CM_INTR_STS, PKT_CMD_FAIL, 1);
    }
    aspeed_i2c_write(f, aspeed_i2c_intr_ctrl_offset(td), reg);

    for (uint8_t i = 0; i < ARRAY_SIZE(rx_data); i++) {
        qom_i2c_dummy_set_rx(i, rx_data[i]);
    }
}

static uint32_t aspeed_i2c_pkt_set_addr_cmd(const TestData *td, uint8_t addr)
{
    uint32_t cmd;

    cmd = FIELD_DP32(0, I2CM_CMD, PKT_OP_EN, 1);
    cmd = FIELD_DP32(cmd, I2CM_CMD, PKT_DEV_ADDR, addr);
    return cmd;
}

static
uint32_t aspeed_i2c_byte_set_addr_cmd(TestFixture *f, const TestData *td,
                                      uint8_t addr, bool is_recv)
{
    if (td->mode == TEST_I2C_MODE_PKT) {
        return aspeed_i2c_pkt_set_addr_cmd(td, addr);
    } else {
        aspeed_i2c_write_byte(f, td, addr << 1 | (is_recv ? 1 : 0));
        return SHARED_FIELD_DP32(0, M_TX_CMD, 1);
    }
}

static
uint32_t aspeed_i2c_buf_set_addr_cmd(TestFixture *f, const TestData *td,
                                     uint8_t addr, bool is_recv)
{
    if (td->mode == TEST_I2C_MODE_PKT) {
        return aspeed_i2c_pkt_set_addr_cmd(td, addr);
    } else {
        aspeed_i2c_write_pool(f, 0, addr << 1 | (is_recv ? 1 : 0));
        return SHARED_FIELD_DP32(0, M_TX_CMD, 1);
    }
}

static
uint32_t aspeed_i2c_dma_set_addr_cmd(TestFixture *f, const TestData *td,
                                     uint8_t addr, bool is_recv)
{
    if (td->mode == TEST_I2C_MODE_PKT) {
        return aspeed_i2c_pkt_set_addr_cmd(td, addr);
    } else {
        aspeed_i2c_write_dma(f, DRAM_TX_OFFSET, addr << 1 | (is_recv ? 1 : 0));
        return SHARED_FIELD_DP32(0, M_TX_CMD, 1);
    }
}


static uint32_t aspeed_i2c_send_addr_status(TestFixture *f, const TestData *td)
{
    uint32_t sts;

    if (td->mode == TEST_I2C_MODE_PKT) {
        sts = FIELD_DP32(0, I2CM_INTR_STS, PKT_CMD_DONE, 1);
    } else {
        sts = SHARED_FIELD_DP32(0, TX_ACK, 1);
    }
    return sts;
}

/*
 * Send an address byte to a non existing device.
 * The TX_NAK interrupt should be raised.
 */
static void test_nack(TestFixture *f, gconstpointer p_data)
{
    const TestData *td = (const TestData *)p_data;
    uint32_t sts, cmd;

    cmd = aspeed_i2c_byte_set_addr_cmd(f, td, I2C_ADDR_NACK, false);
    aspeed_i2c_send_start(f, td, cmd);

    g_assert_cmphex(aspeed_i2c_get_txstate(f, td), ==, I2CD_MSTART);
    g_assert_false(aspeed_i2c_get_busy_sts(f, td));
    sts = SHARED_FIELD_DP32(0, TX_NAK, 1);
    if (td->mode == TEST_I2C_MODE_PKT) {
        sts = FIELD_DP32(sts, I2CM_INTR_STS, PKT_CMD_FAIL, 1);
        sts = FIELD_DP32(sts, I2CM_INTR_STS, PKT_CMD_DONE, 1);
    }
    CLEAR_I2C_INTR(f, td, sts);

    aspeed_i2c_write(f, aspeed_i2c_cmd_offset(td),
                     SHARED_FIELD_DP32(0, M_STOP_CMD, 1));
    CLEAR_I2C_INTR(f, td, SHARED_FIELD_DP32(0, NORMAL_STOP, 1));
    CHECK_I2C_IDLE(f, td);
    qtest_hmp(global_qtest, "system_reset");
}


static void test_dummy_byte(TestFixture *f, gconstpointer p_data)
{
    const TestData *td = (const TestData *)p_data;
    uint32_t cmd, sts;
    bool pkt_mode = td->mode == TEST_I2C_MODE_PKT;

    /* Write device address (Write) */
    cmd = aspeed_i2c_byte_set_addr_cmd(f, td, I2C_ADDR_DUMMY, false);
    aspeed_i2c_send_start(f, td, cmd);
    CHECK_I2C_BUS_ACTIVE(f, td);
    CLEAR_I2C_INTR(f, td, aspeed_i2c_send_addr_status(f, td));

    /* Write data */
    sts = SHARED_FIELD_DP32(0, TX_ACK, 1);
    if (pkt_mode) {
        sts = FIELD_DP32(sts, I2CM_INTR_STS, PKT_CMD_DONE, 1);
        cmd = aspeed_i2c_pkt_set_addr_cmd(td, I2C_ADDR_DUMMY);
    } else {
        cmd = 0;
    }
    cmd = SHARED_FIELD_DP32(cmd, M_TX_CMD, 1);
    for (uint8_t i = 0; i < ARRAY_SIZE(tx_data); i++) {
        aspeed_i2c_write_byte(f, td, tx_data[i]);
        aspeed_i2c_write(f, aspeed_i2c_cmd_offset(td), cmd);
        CHECK_I2C_BUS_ACTIVE(f, td);
        CLEAR_I2C_INTR(f, td, sts);
    }

    /* Read data */
    cmd = aspeed_i2c_byte_set_addr_cmd(f, td, I2C_ADDR_DUMMY, true);
    cmd = SHARED_FIELD_DP32(cmd, M_RX_CMD, 1);
    cmd = SHARED_FIELD_DP32(cmd, M_START_CMD, 1);
    sts = aspeed_i2c_send_addr_status(f, td);

    for (uint8_t i = 0; i < ARRAY_SIZE(rx_data); i++) {
        cmd = SHARED_FIELD_DP32(cmd, M_RX_CMD, 1);
        if (i == ARRAY_SIZE(rx_data) - 1) {
            cmd = SHARED_FIELD_DP32(cmd, M_S_RX_CMD_LAST, 1);
        }
        aspeed_i2c_write(f, aspeed_i2c_cmd_offset(td), cmd);
        CHECK_I2C_BUS_ACTIVE(f, td);
        sts = SHARED_FIELD_DP32(sts, RX_DONE, 1);
        if (pkt_mode) {
            sts = FIELD_DP32(sts, I2CM_INTR_STS, PKT_CMD_DONE, 1);
            cmd = aspeed_i2c_pkt_set_addr_cmd(td, I2C_ADDR_DUMMY);
        } else {
            cmd = 0;
        }
        CLEAR_I2C_INTR(f, td, sts);
        g_assert_cmphex(aspeed_i2c_read_byte(f, td), ==, rx_data[i]);
        sts = 0;
    }

    SEND_I2C_STOP(f, td);
    CHECK_I2C_DUMMY_DEVICE();
    qtest_hmp(global_qtest, "system_reset");
}

static void test_dummy_buf(TestFixture *f, gconstpointer p_data)
{
    const TestData *td = (const TestData *)p_data;
    uint32_t cmd, sts, pool_ctrl;
    bool pkt_mode = td->mode == TEST_I2C_MODE_PKT;

    /* Write data with device address */
    for (uint8_t i = 0; i < ARRAY_SIZE(tx_data); i++) {
        aspeed_i2c_write_pool(f, i + (pkt_mode ? 0 : 1), tx_data[i]);
    }
    pool_ctrl = SHARED_FIELD_DP32(0, TX_COUNT,
                                  ARRAY_SIZE(tx_data) - (pkt_mode ? 1 : 0));
    aspeed_i2c_write(f, aspeed_i2c_pool_ctrl_offset(td), pool_ctrl);
    cmd = aspeed_i2c_buf_set_addr_cmd(f, td, I2C_ADDR_DUMMY, false);
    cmd = SHARED_FIELD_DP32(cmd, M_START_CMD, 1);
    cmd = SHARED_FIELD_DP32(cmd, M_TX_CMD, 1);
    cmd = SHARED_FIELD_DP32(cmd, TX_BUFF_EN, 1);
    sts = SHARED_FIELD_DP32(0, TX_ACK, 1);
    if (pkt_mode) {
        sts = FIELD_DP32(sts, I2CM_INTR_STS, PKT_CMD_DONE, 1);
    }
    aspeed_i2c_write(f, aspeed_i2c_cmd_offset(td), cmd);
    CHECK_I2C_BUS_ACTIVE(f, td);
    CLEAR_I2C_INTR(f, td, sts);

    /* Write device address (Read) in non packet mde */
    if (pkt_mode) {
        cmd = aspeed_i2c_pkt_set_addr_cmd(td, I2C_ADDR_DUMMY);
        cmd = SHARED_FIELD_DP32(cmd, M_START_CMD, 1);
    } else {
        aspeed_i2c_write_byte(f, td, (I2C_ADDR_DUMMY << 1) | 1);
        aspeed_i2c_send_start(f, td, SHARED_FIELD_DP32(0, M_TX_CMD, 1));
        CHECK_I2C_BUS_ACTIVE(f, td);
        CLEAR_I2C_INTR(f, td, aspeed_i2c_send_addr_status(f, td));
        cmd = 0;
    }

    /* Read data */
    pool_ctrl = SHARED_FIELD_DP32(0, RX_SIZE,
                                  ARRAY_SIZE(rx_data) - 1);
    aspeed_i2c_write(f, aspeed_i2c_pool_ctrl_offset(td), pool_ctrl);
    cmd = SHARED_FIELD_DP32(cmd, M_RX_CMD, 1);
    cmd = SHARED_FIELD_DP32(cmd, M_S_RX_CMD_LAST, 1);
    cmd = SHARED_FIELD_DP32(cmd, RX_BUFF_EN, 1);
    sts = SHARED_FIELD_DP32(0, RX_DONE, 1);
    if (pkt_mode) {
        sts = FIELD_DP32(sts, I2CM_INTR_STS, PKT_CMD_DONE, 1);
    }
    aspeed_i2c_write(f, aspeed_i2c_cmd_offset(td), cmd);
    CHECK_I2C_BUS_ACTIVE(f, td);
    CLEAR_I2C_INTR(f, td, sts);
    for (uint8_t i = 0; i < ARRAY_SIZE(rx_data); i++) {
        g_assert_cmphex(aspeed_i2c_read_pool(f, i), ==, rx_data[i]);
    }

    SEND_I2C_STOP(f, td);
    CHECK_I2C_DUMMY_DEVICE();
    qtest_hmp(global_qtest, "system_reset");
}

static void test_dummy_dma(TestFixture *f, gconstpointer p_data)
{
    const TestData *td = (const TestData *)p_data;
    uint32_t cmd, sts;
    bool pkt_mode = td->mode == TEST_I2C_MODE_PKT;

    /* Write data with device address*/
    for (uint8_t i = 0; i < ARRAY_SIZE(tx_data); i++) {
        aspeed_i2c_write_dma(f, DRAM_TX_OFFSET + (pkt_mode ? 0 : 1) + i,
                             tx_data[i]);
    }
    aspeed_i2c_write_dma_addr(f, td, DRAM_TX_OFFSET, false);
    aspeed_i2c_write_dma_len(f, td,
                             ARRAY_SIZE(tx_data) - (pkt_mode ? 1 : 0), false);
    cmd = aspeed_i2c_dma_set_addr_cmd(f, td, I2C_ADDR_DUMMY, false);

    cmd = SHARED_FIELD_DP32(cmd, M_START_CMD, 1);
    cmd = SHARED_FIELD_DP32(cmd, M_TX_CMD, 1);
    cmd = SHARED_FIELD_DP32(cmd, TX_DMA_EN, 1);
    sts = SHARED_FIELD_DP32(0, TX_ACK, 1);
    if (pkt_mode) {
        sts = FIELD_DP32(sts, I2CM_INTR_STS, PKT_CMD_DONE, 1);
    }
    aspeed_i2c_write(f, aspeed_i2c_cmd_offset(td), cmd);
    CHECK_I2C_BUS_ACTIVE(f, td);
    CLEAR_I2C_INTR(f, td, sts);

    /* Write device address (Read) in non packet mode */
    if (pkt_mode) {
        cmd = aspeed_i2c_pkt_set_addr_cmd(td, I2C_ADDR_DUMMY);
        cmd = SHARED_FIELD_DP32(cmd, M_START_CMD, 1);
    } else {
        aspeed_i2c_write_byte(f, td, (I2C_ADDR_DUMMY << 1) | 1);
        aspeed_i2c_send_start(f, td, SHARED_FIELD_DP32(0, M_TX_CMD, 1));
        CHECK_I2C_BUS_ACTIVE(f, td);
        CLEAR_I2C_INTR(f, td, aspeed_i2c_send_addr_status(f, td));
        cmd = 0;
    }

    /* Read data */
    aspeed_i2c_write_dma_addr(f, td, DRAM_RX_OFFSET, true);
    aspeed_i2c_write_dma_len(f, td, ARRAY_SIZE(rx_data) - 1, true);
    cmd = SHARED_FIELD_DP32(cmd, M_RX_CMD, 1);
    cmd = SHARED_FIELD_DP32(cmd, M_S_RX_CMD_LAST, 1);
    cmd = SHARED_FIELD_DP32(cmd, RX_DMA_EN, 1);
    sts = SHARED_FIELD_DP32(0, RX_DONE, 1);
    if (pkt_mode) {
        sts = FIELD_DP32(sts, I2CM_INTR_STS, PKT_CMD_DONE, 1);
    }
    aspeed_i2c_write(f, aspeed_i2c_cmd_offset(td), cmd);
    CHECK_I2C_BUS_ACTIVE(f, td);
    CLEAR_I2C_INTR(f, td, sts);
    for (uint8_t i = 0; i < ARRAY_SIZE(rx_data); i++) {
        g_assert_cmphex(aspeed_i2c_read_dma(f, DRAM_RX_OFFSET + i), ==,
                        rx_data[i]);
    }

    SEND_I2C_STOP(f, td);
    CHECK_I2C_DUMMY_DEVICE();
    qtest_hmp(global_qtest, "system_reset");
}

static void test_slave(TestFixture *f, gconstpointer p_data)
{
    const TestData *td = (const TestData *)p_data;
    uint32_t cmd, sts, tmp;
    bool pkt_mode = td->mode == TEST_I2C_MODE_PKT;
    uint8_t offset = pkt_mode ? 0 : 1;

    aspeed_i2c_write_pool(f, offset + 0, I2C_ADDR_SLAVE);
    aspeed_i2c_write_pool(f, offset + 1, tx_data[0]);
    aspeed_i2c_write_pool(f, offset + 2, tx_data[1]);

    tmp = SHARED_FIELD_DP32(0, TX_COUNT, pkt_mode ? 2 : 3);
    aspeed_i2c_write(f, aspeed_i2c_pool_ctrl_offset(td), tmp);

    cmd = aspeed_i2c_buf_set_addr_cmd(f, td, I2C_ADDR_ECHO, false);
    cmd = SHARED_FIELD_DP32(cmd, M_START_CMD, 1);
    cmd = SHARED_FIELD_DP32(cmd, M_TX_CMD, 1);
    cmd = SHARED_FIELD_DP32(cmd, TX_BUFF_EN, 1);
    sts = SHARED_FIELD_DP32(0, TX_ACK, 1);
    if (pkt_mode) {
        sts = FIELD_DP32(sts, I2CM_INTR_STS, PKT_CMD_DONE, 1);
    }
    aspeed_i2c_write(f, aspeed_i2c_cmd_offset(td), cmd);
    CHECK_I2C_BUS_ACTIVE(f, td);
    CLEAR_I2C_INTR(f, td, sts);

    /* Send stop and handle i2c-echo response as a slave */
    if (td->mode == TEST_I2C_MODE_OLD) {
        aspeed_i2c_write(f, aspeed_i2c_cmd_offset(td),
                        SHARED_FIELD_DP32(0, M_STOP_CMD, 1));
        clock_step(1);
        g_assert_cmphex(aspeed_i2c_read_byte(f, td), ==, I2C_ADDR_SLAVE << 1);
        sts = SHARED_FIELD_DP32(0, NORMAL_STOP, 1);
        sts = SHARED_FIELD_DP32(sts, RX_DONE, 1);
        sts = FIELD_DP32(sts, I2CD_INTR_STS, SLAVE_ADDR_RX_MATCH, 1);
        CHECK_I2C_INTR(f, td, sts);
        aspeed_i2c_write(f, aspeed_i2c_intr_sts_offset(td), sts);

        g_assert_cmphex(aspeed_i2c_read_byte(f, td), ==, tx_data[0]);
        sts = SHARED_FIELD_DP32(0, RX_DONE, 1);
        CHECK_I2C_INTR(f, td, sts);
        aspeed_i2c_write(f, aspeed_i2c_intr_sts_offset(td), sts);

        g_assert_cmphex(aspeed_i2c_read_byte(f, td), ==, tx_data[1]);
        sts = SHARED_FIELD_DP32(0, RX_DONE, 1);
        CHECK_I2C_INTR(f, td, sts);
        aspeed_i2c_write(f, aspeed_i2c_intr_sts_offset(td), sts);
    } else {
        aspeed_i2c_write(f, A_I2CS_DMA_RX_ADDR, DRAM_RX_OFFSET);
        tmp = SHARED_FIELD_DP32(0, RX_DMA_EN, 1);
        aspeed_i2c_write(f, A_I2CS_CMD, tmp);

        aspeed_i2c_write(f, aspeed_i2c_cmd_offset(td),
                        SHARED_FIELD_DP32(0, M_STOP_CMD, 1));
        clock_step(1);

        tmp = FIELD_DP32(0, I2CS_DMA_LEN_STS, RX_LEN, 2);
        g_assert_cmphex(aspeed_i2c_read(f, A_I2CS_DMA_LEN_STS), ==, tmp);
        for (uint8_t i = 0; i < 2; i++) {
            g_assert_cmphex(aspeed_i2c_read_dma(f, DRAM_RX_OFFSET + i), ==,
                            tx_data[i]);
        }
    }

    sts = SHARED_FIELD_DP32(0, NORMAL_STOP, 1);
    CLEAR_I2C_INTR(f, td, sts);
    CHECK_I2C_IDLE(f, td);

    qtest_hmp(global_qtest, "system_reset");
}

static void aspeed_i2c_init_fixture(TestFixture *fixture, const TestData *td)
{
    switch (td->machine) {
    case TEST_MACHINE_AST2400:
        fixture->base_addr = 0x1E78A000;
        fixture->bus_offset = 0x40;
        fixture->irq_no = 12;
        fixture->pool_base = 0x800;
        break;
    case TEST_MACHINE_AST2500:
        fixture->base_addr = 0x1E78A000;
        fixture->bus_offset = 0x40;
        fixture->irq_no = 12;
        fixture->pool_base = 0x200;
        break;
    case TEST_MACHINE_AST2600:
        fixture->base_addr = 0x1E78A000;
        fixture->bus_offset = 0x80;
        fixture->irq_no = 110;
        fixture->pool_base = 0xC00;
        break;
    default:
        return;
    }
}

static void aspeed_i2c_init_test(TestFixture *fixture, const TestData *td)
{
    aspeed_i2c_init(fixture, td);
    CHECK_I2C_IDLE(fixture, td);
}

static void aspeed_i2c_init_new_machine(TestFixture *fixture,
                                        gconstpointer p_data)
{
    const TestData *td = (const TestData *)p_data;
    const char *machine;
    const char *irq_dev;

    aspeed_i2c_init_fixture(fixture, td);

    if (global_qtest) {
        qtest_quit(global_qtest);
    }

    switch (td->machine) {
    case TEST_MACHINE_AST2400:
        machine = "palmetto-bmc";
        irq_dev = "/machine/soc/vic";
        break;
    case TEST_MACHINE_AST2500:
        machine = "ast2500-evb";
        irq_dev = "/machine/soc/vic";
        break;
    case TEST_MACHINE_AST2600:
        machine = "ast2600-evb";
        irq_dev = "/machine/soc/a7mpcore/gic";
        break;
    default:
        g_test_fail();
        return;
    }

    global_qtest = qtest_initf(
        "-machine %s -nographic "
        "-device i2c-echo,id=i2c-echo,bus=aspeed.i2c.bus.0,address=%d "
        "-device i2c-dummy-device,id=i2c-dummy,bus=aspeed.i2c.bus.0,address=%d",
        machine, I2C_ADDR_ECHO, I2C_ADDR_DUMMY);
    qtest_irq_intercept_in(global_qtest, irq_dev);

    aspeed_i2c_init_test(fixture, td);
}

static void aspeed_i2c_init_machine(TestFixture *fixture,
                                    gconstpointer p_data)
{
    const TestData *td = (const TestData *)p_data;

    aspeed_i2c_init_fixture(fixture, td);
    aspeed_i2c_init_test(fixture, td);
}

static void add_test(const TestData *td, const char *name,
                     void (*func)(TestFixture *, const void *),
                     bool setup)
{
    g_autofree char *test_name;

    test_name = g_strdup_printf("%s/%s", td->name, name);

    qtest_add(test_name, TestFixture, td,
              setup ? aspeed_i2c_init_new_machine : aspeed_i2c_init_machine,
              func, NULL);
}

#define ADD_TEST(td, test, new) add_test(td, #test, test_##test, new)

static const TestData test_data[] = {
    {
        .name = "ast2400",
        .machine = TEST_MACHINE_AST2400,
    },
    {
        .name = "ast2500",
        .machine = TEST_MACHINE_AST2500,
    },
    {
        .name = "ast2600",
        .machine = TEST_MACHINE_AST2600,
    },
    {
        .name = "ast2600-new",
        .machine = TEST_MACHINE_AST2600,
        .mode = TEST_I2C_MODE_NEW,
    },
    {
        .name = "ast2600-packet",
        .machine = TEST_MACHINE_AST2600,
        .mode = TEST_I2C_MODE_PKT,
    },
};

int main(int argc, char **argv)
{
    int ret;
    bool setup;

    g_test_init(&argc, &argv, NULL);

    for (int i = 0; i < ARRAY_SIZE(test_data); i++) {
        setup = i == 0 || test_data[i].machine != test_data[i - 1].machine;
        ADD_TEST(&test_data[i], nack, setup);
        ADD_TEST(&test_data[i], dummy_byte, false);
        ADD_TEST(&test_data[i], dummy_buf, false);
        if (test_data[i].machine != TEST_MACHINE_AST2400) {
            ADD_TEST(&test_data[i], dummy_dma, false);
        }
        ADD_TEST(&test_data[i], slave, false);
    }

    ret = g_test_run();
    qtest_quit(global_qtest);
    return ret;
}
