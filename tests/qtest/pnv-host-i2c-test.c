/*
 * QTest testcase for PowerNV 10 Host I2C Communications
 *
 * Copyright (c) 2023, IBM Corporation.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later. See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "libqtest.h"
#include "hw/misc/pca9554_regs.h"
#include "hw/misc/pca9552_regs.h"

#define PPC_BIT(bit)            (0x8000000000000000ULL >> (bit))
#define PPC_BIT32(bit)          (0x80000000 >> (bit))
#define PPC_BIT8(bit)           (0x80 >> (bit))
#define PPC_BITMASK(bs, be)     ((PPC_BIT(bs) - PPC_BIT(be)) | PPC_BIT(bs))
#define PPC_BITMASK32(bs, be)   ((PPC_BIT32(bs) - PPC_BIT32(be)) | \
                                 PPC_BIT32(bs))

#define MASK_TO_LSH(m)          (__builtin_ffsll(m) - 1)
#define GETFIELD(m, v)          (((v) & (m)) >> MASK_TO_LSH(m))
#define SETFIELD(m, v, val) \
        (((v) & ~(m)) | ((((typeof(v))(val)) << MASK_TO_LSH(m)) & (m)))

#define P10_XSCOM_BASE          0x000603fc00000000ull
#define PNV10_CHIP_MAX_I2C      5
#define PNV10_XSCOM_I2CM_BASE   0xa0000
#define PNV10_XSCOM_I2CM_SIZE   0x1000

/* I2C FIFO register */
#define I2C_FIFO_REG                    0x4
#define I2C_FIFO                        PPC_BITMASK(0, 7)

/* I2C command register */
#define I2C_CMD_REG                     0x5
#define I2C_CMD_WITH_START              PPC_BIT(0)
#define I2C_CMD_WITH_ADDR               PPC_BIT(1)
#define I2C_CMD_READ_CONT               PPC_BIT(2)
#define I2C_CMD_WITH_STOP               PPC_BIT(3)
#define I2C_CMD_INTR_STEERING           PPC_BITMASK(6, 7) /* P9 */
#define   I2C_CMD_INTR_STEER_HOST       1
#define   I2C_CMD_INTR_STEER_OCC        2
#define I2C_CMD_DEV_ADDR                PPC_BITMASK(8, 14)
#define I2C_CMD_READ_NOT_WRITE          PPC_BIT(15)
#define I2C_CMD_LEN_BYTES               PPC_BITMASK(16, 31)
#define I2C_MAX_TFR_LEN                 0xfff0ull

/* I2C mode register */
#define I2C_MODE_REG                    0x6
#define I2C_MODE_BIT_RATE_DIV           PPC_BITMASK(0, 15)
#define I2C_MODE_PORT_NUM               PPC_BITMASK(16, 21)
#define I2C_MODE_ENHANCED               PPC_BIT(28)
#define I2C_MODE_DIAGNOSTIC             PPC_BIT(29)
#define I2C_MODE_PACING_ALLOW           PPC_BIT(30)
#define I2C_MODE_WRAP                   PPC_BIT(31)

/* I2C watermark register */
#define I2C_WATERMARK_REG               0x7
#define I2C_WATERMARK_HIGH              PPC_BITMASK(16, 19)
#define I2C_WATERMARK_LOW               PPC_BITMASK(24, 27)

/*
 * I2C interrupt mask and condition registers
 *
 * NB: The function of 0x9 and 0xa changes depending on whether you're reading
 *     or writing to them. When read they return the interrupt condition bits
 *     and on writes they update the interrupt mask register.
 *
 *  The bit definitions are the same for all the interrupt registers.
 */
#define I2C_INTR_MASK_REG               0x8

#define I2C_INTR_RAW_COND_REG           0x9 /* read */
#define I2C_INTR_MASK_OR_REG            0x9 /* write*/

#define I2C_INTR_COND_REG               0xa /* read */
#define I2C_INTR_MASK_AND_REG           0xa /* write */

#define I2C_INTR_ALL                    PPC_BITMASK(16, 31)
#define I2C_INTR_INVALID_CMD            PPC_BIT(16)
#define I2C_INTR_LBUS_PARITY_ERR        PPC_BIT(17)
#define I2C_INTR_BKEND_OVERRUN_ERR      PPC_BIT(18)
#define I2C_INTR_BKEND_ACCESS_ERR       PPC_BIT(19)
#define I2C_INTR_ARBT_LOST_ERR          PPC_BIT(20)
#define I2C_INTR_NACK_RCVD_ERR          PPC_BIT(21)
#define I2C_INTR_DATA_REQ               PPC_BIT(22)
#define I2C_INTR_CMD_COMP               PPC_BIT(23)
#define I2C_INTR_STOP_ERR               PPC_BIT(24)
#define I2C_INTR_I2C_BUSY               PPC_BIT(25)
#define I2C_INTR_NOT_I2C_BUSY           PPC_BIT(26)
#define I2C_INTR_SCL_EQ_1               PPC_BIT(28)
#define I2C_INTR_SCL_EQ_0               PPC_BIT(29)
#define I2C_INTR_SDA_EQ_1               PPC_BIT(30)
#define I2C_INTR_SDA_EQ_0               PPC_BIT(31)

/* I2C status register */
#define I2C_RESET_I2C_REG               0xb /* write */
#define I2C_RESET_ERRORS                0xc
#define I2C_STAT_REG                    0xb /* read */
#define I2C_STAT_INVALID_CMD            PPC_BIT(0)
#define I2C_STAT_LBUS_PARITY_ERR        PPC_BIT(1)
#define I2C_STAT_BKEND_OVERRUN_ERR      PPC_BIT(2)
#define I2C_STAT_BKEND_ACCESS_ERR       PPC_BIT(3)
#define I2C_STAT_ARBT_LOST_ERR          PPC_BIT(4)
#define I2C_STAT_NACK_RCVD_ERR          PPC_BIT(5)
#define I2C_STAT_DATA_REQ               PPC_BIT(6)
#define I2C_STAT_CMD_COMP               PPC_BIT(7)
#define I2C_STAT_STOP_ERR               PPC_BIT(8)
#define I2C_STAT_UPPER_THRS             PPC_BITMASK(9, 15)
#define I2C_STAT_ANY_I2C_INTR           PPC_BIT(16)
#define I2C_STAT_PORT_HISTORY_BUSY      PPC_BIT(19)
#define I2C_STAT_SCL_INPUT_LEVEL        PPC_BIT(20)
#define I2C_STAT_SDA_INPUT_LEVEL        PPC_BIT(21)
#define I2C_STAT_PORT_BUSY              PPC_BIT(22)
#define I2C_STAT_INTERFACE_BUSY         PPC_BIT(23)
#define I2C_STAT_FIFO_ENTRY_COUNT       PPC_BITMASK(24, 31)

#define I2C_STAT_ANY_ERR (I2C_STAT_INVALID_CMD | I2C_STAT_LBUS_PARITY_ERR | \
                          I2C_STAT_BKEND_OVERRUN_ERR | \
                          I2C_STAT_BKEND_ACCESS_ERR | I2C_STAT_ARBT_LOST_ERR | \
                          I2C_STAT_NACK_RCVD_ERR | I2C_STAT_STOP_ERR)


#define I2C_INTR_ACTIVE \
        ((I2C_STAT_ANY_ERR >> 16) | I2C_INTR_CMD_COMP | I2C_INTR_DATA_REQ)

/* Pseudo-status used for timeouts */
#define I2C_STAT_PSEUDO_TIMEOUT         PPC_BIT(63)

/* I2C extended status register */
#define I2C_EXTD_STAT_REG               0xc
#define I2C_EXTD_STAT_FIFO_SIZE         PPC_BITMASK(0, 7)
#define I2C_EXTD_STAT_MSM_CURSTATE      PPC_BITMASK(11, 15)
#define I2C_EXTD_STAT_SCL_IN_SYNC       PPC_BIT(16)
#define I2C_EXTD_STAT_SDA_IN_SYNC       PPC_BIT(17)
#define I2C_EXTD_STAT_S_SCL             PPC_BIT(18)
#define I2C_EXTD_STAT_S_SDA             PPC_BIT(19)
#define I2C_EXTD_STAT_M_SCL             PPC_BIT(20)
#define I2C_EXTD_STAT_M_SDA             PPC_BIT(21)
#define I2C_EXTD_STAT_HIGH_WATER        PPC_BIT(22)
#define I2C_EXTD_STAT_LOW_WATER         PPC_BIT(23)
#define I2C_EXTD_STAT_I2C_BUSY          PPC_BIT(24)
#define I2C_EXTD_STAT_SELF_BUSY         PPC_BIT(25)
#define I2C_EXTD_STAT_I2C_VERSION       PPC_BITMASK(27, 31)

/* I2C residual front end/back end length */
#define I2C_RESIDUAL_LEN_REG            0xd
#define I2C_RESIDUAL_FRONT_END          PPC_BITMASK(0, 15)
#define I2C_RESIDUAL_BACK_END           PPC_BITMASK(16, 31)

/* Port busy register */
#define I2C_PORT_BUSY_REG               0xe
#define I2C_SET_S_SCL_REG               0xd
#define I2C_RESET_S_SCL_REG             0xf
#define I2C_SET_S_SDA_REG               0x10
#define I2C_RESET_S_SDA_REG             0x11

#define PNV_I2C_FIFO_SIZE 8

#define SMT                     4 /* some tests will break if less than 4 */

typedef enum PnvChipType {
    PNV_CHIP_POWER8E,     /* AKA Murano (default) */
    PNV_CHIP_POWER8,      /* AKA Venice */
    PNV_CHIP_POWER8NVL,   /* AKA Naples */
    PNV_CHIP_POWER9,      /* AKA Nimbus */
    PNV_CHIP_POWER10,
} PnvChipType;

typedef struct PnvChip {
    PnvChipType chip_type;
    const char *cpu_model;
    uint64_t    xscom_base;
    uint64_t    cfam_id;
    uint32_t    first_core;
    uint32_t    num_i2c;
} PnvChip;

static const PnvChip pnv_chips[] = {
    {
        .chip_type  = PNV_CHIP_POWER9,
        .cpu_model  = "POWER9",
        .xscom_base = 0x000603fc00000000ull,
        .cfam_id    = 0x220d104900008000ull,
        .first_core = 0x0,
        .num_i2c    = 4,
    },
    {
        .chip_type  = PNV_CHIP_POWER10,
        .cpu_model  = "POWER10",
        .xscom_base = 0x000603fc00000000ull,
        .cfam_id    = 0x120da04900008000ull,
        .first_core = 0x0,
        .num_i2c    = 4,
    },
};


typedef struct {
    QTestState  *qts;
    int         engine;
    int         port;
    uint8_t     addr;
} pnv_i2c_dev_t;


static uint64_t pnv_xscom_addr(uint32_t pcba)
{
    return P10_XSCOM_BASE | ((uint64_t) pcba << 3);
}

static uint64_t pnv_i2c_xscom_addr(int engine, uint32_t reg)
{
    return pnv_xscom_addr(PNV10_XSCOM_I2CM_BASE +
                          (PNV10_XSCOM_I2CM_SIZE * engine) + reg);
}

static uint64_t pnv_i2c_xscom_read(QTestState *qts, int engine, uint32_t reg)
{
    return qtest_readq(qts, pnv_i2c_xscom_addr(engine, reg));
}

static void pnv_i2c_xscom_write(QTestState *qts, int engine, uint32_t reg,
                                                             uint64_t val)
{
    qtest_writeq(qts, pnv_i2c_xscom_addr(engine, reg), val);
}

/* Write len bytes from buf to i2c device with given addr and port */
static void pnv_i2c_send(pnv_i2c_dev_t *dev, const uint8_t *buf, uint16_t len)
{
    int byte_num;
    uint64_t reg64;

    /* select requested port */
    reg64 = SETFIELD(I2C_MODE_BIT_RATE_DIV, 0ull, 0x2be);
    reg64 = SETFIELD(I2C_MODE_PORT_NUM, reg64, dev->port);
    pnv_i2c_xscom_write(dev->qts, dev->engine, I2C_MODE_REG, reg64);

    /* check status for cmd complete and bus idle */
    reg64 = pnv_i2c_xscom_read(dev->qts, dev->engine, I2C_EXTD_STAT_REG);
    g_assert_cmphex(reg64 & I2C_EXTD_STAT_I2C_BUSY, ==, 0);
    reg64 = pnv_i2c_xscom_read(dev->qts, dev->engine, I2C_STAT_REG);
    g_assert_cmphex(reg64 & (I2C_STAT_ANY_ERR | I2C_STAT_CMD_COMP), ==,
                    I2C_STAT_CMD_COMP);

    /* Send start, with stop, with address and len bytes of data */
    reg64 = I2C_CMD_WITH_START | I2C_CMD_WITH_ADDR | I2C_CMD_WITH_STOP;
    reg64 = SETFIELD(I2C_CMD_DEV_ADDR, reg64, dev->addr);
    reg64 = SETFIELD(I2C_CMD_LEN_BYTES, reg64, len);
    pnv_i2c_xscom_write(dev->qts, dev->engine, I2C_CMD_REG, reg64);

    /* check status for errors */
    reg64 = pnv_i2c_xscom_read(dev->qts, dev->engine, I2C_STAT_REG);
    g_assert_cmphex(reg64 & I2C_STAT_ANY_ERR, ==, 0);

    /* write data bytes to fifo register */
    for (byte_num = 0; byte_num < len; byte_num++) {
        reg64 = SETFIELD(I2C_FIFO, 0ull, buf[byte_num]);
        pnv_i2c_xscom_write(dev->qts, dev->engine, I2C_FIFO_REG, reg64);
    }

    /* check status for cmd complete and bus idle */
    reg64 = pnv_i2c_xscom_read(dev->qts, dev->engine, I2C_EXTD_STAT_REG);
    g_assert_cmphex(reg64 & I2C_EXTD_STAT_I2C_BUSY, ==, 0);
    reg64 = pnv_i2c_xscom_read(dev->qts, dev->engine, I2C_STAT_REG);
    g_assert_cmphex(reg64 & (I2C_STAT_ANY_ERR | I2C_STAT_CMD_COMP), ==,
                    I2C_STAT_CMD_COMP);
}

/* Recieve len bytes into buf from i2c device with given addr and port */
static void pnv_i2c_recv(pnv_i2c_dev_t *dev, uint8_t *buf, uint16_t len)
{
    int byte_num;
    uint64_t reg64;

    /* select requested port */
    reg64 = SETFIELD(I2C_MODE_BIT_RATE_DIV, 0ull, 0x2be);
    reg64 = SETFIELD(I2C_MODE_PORT_NUM, reg64, dev->port);
    pnv_i2c_xscom_write(dev->qts, dev->engine, I2C_MODE_REG, reg64);

    /* check status for cmd complete and bus idle */
    reg64 = pnv_i2c_xscom_read(dev->qts, dev->engine, I2C_EXTD_STAT_REG);
    g_assert_cmphex(reg64 & I2C_EXTD_STAT_I2C_BUSY, ==, 0);
    reg64 = pnv_i2c_xscom_read(dev->qts, dev->engine, I2C_STAT_REG);
    g_assert_cmphex(reg64 & (I2C_STAT_ANY_ERR | I2C_STAT_CMD_COMP), ==,
                    I2C_STAT_CMD_COMP);

    /* Send start, with stop, with address and len bytes of data */
    reg64 = I2C_CMD_WITH_START | I2C_CMD_WITH_ADDR |
            I2C_CMD_WITH_STOP | I2C_CMD_READ_NOT_WRITE;
    reg64 = SETFIELD(I2C_CMD_DEV_ADDR, reg64, dev->addr);
    reg64 = SETFIELD(I2C_CMD_LEN_BYTES, reg64, len);
    pnv_i2c_xscom_write(dev->qts, dev->engine, I2C_CMD_REG, reg64);

    /* check status for errors */
    reg64 = pnv_i2c_xscom_read(dev->qts, dev->engine, I2C_STAT_REG);
    g_assert_cmphex(reg64 & I2C_STAT_ANY_ERR, ==, 0);

    /* Read data bytes from fifo register */
    for (byte_num = 0; byte_num < len; byte_num++) {
        reg64 = pnv_i2c_xscom_read(dev->qts, dev->engine, I2C_FIFO_REG);
        buf[byte_num] = GETFIELD(I2C_FIFO, reg64);
    }

    /* check status for cmd complete and bus idle */
    reg64 = pnv_i2c_xscom_read(dev->qts, dev->engine, I2C_EXTD_STAT_REG);
    g_assert_cmphex(reg64 & I2C_EXTD_STAT_I2C_BUSY, ==, 0);
    reg64 = pnv_i2c_xscom_read(dev->qts, dev->engine, I2C_STAT_REG);
    g_assert_cmphex(reg64 & (I2C_STAT_ANY_ERR | I2C_STAT_CMD_COMP), ==,
                    I2C_STAT_CMD_COMP);
}

static void pnv_i2c_pca9554_default_cfg(pnv_i2c_dev_t *dev)
{
    uint8_t buf[2];

    /* input register bits are not inverted */
    buf[0] = PCA9554_POLARITY;
    buf[1] = 0;
    pnv_i2c_send(dev, buf, 2);

    /* All pins are inputs */
    buf[0] = PCA9554_CONFIG;
    buf[1] = 0xff;
    pnv_i2c_send(dev, buf, 2);

    /* Output value for when pins are outputs */
    buf[0] = PCA9554_OUTPUT;
    buf[1] = 0xff;
    pnv_i2c_send(dev, buf, 2);
}

static void pnv_i2c_pca9554_set_pin(pnv_i2c_dev_t *dev, int pin, bool high)
{
    uint8_t send_buf[2];
    uint8_t recv_buf[2];
    uint8_t mask = 0x1 << pin;
    uint8_t new_value = ((high) ? 1 : 0) << pin;

    /* read current OUTPUT value */
    send_buf[0] = PCA9554_OUTPUT;
    pnv_i2c_send(dev, send_buf, 1);
    pnv_i2c_recv(dev, recv_buf, 1);

    /* write new OUTPUT value */
    send_buf[1] = (recv_buf[0] & ~mask) | new_value;
    pnv_i2c_send(dev, send_buf, 2);

    /* Update config bit for output */
    send_buf[0] = PCA9554_CONFIG;
    pnv_i2c_send(dev, send_buf, 1);
    pnv_i2c_recv(dev, recv_buf, 1);
    send_buf[1] = recv_buf[0] & ~mask;
    pnv_i2c_send(dev, send_buf, 2);
}

static uint8_t pnv_i2c_pca9554_read_pins(pnv_i2c_dev_t *dev)
{
    uint8_t send_buf[1];
    uint8_t recv_buf[1];
    uint8_t inputs;
    send_buf[0] = PCA9554_INPUT;
    pnv_i2c_send(dev, send_buf, 1);
    pnv_i2c_recv(dev, recv_buf, 1);
    inputs = recv_buf[0];
    return inputs;
}

static void pnv_i2c_pca9554_flip_polarity(pnv_i2c_dev_t *dev)
{
    uint8_t recv_buf[1];
    uint8_t send_buf[2];

    send_buf[0] = PCA9554_POLARITY;
    pnv_i2c_send(dev, send_buf, 1);
    pnv_i2c_recv(dev, recv_buf, 1);
    send_buf[1] = recv_buf[0] ^ 0xff;
    pnv_i2c_send(dev, send_buf, 2);
}

static void pnv_i2c_pca9554_default_inputs(pnv_i2c_dev_t *dev)
{
    uint8_t pin_values = pnv_i2c_pca9554_read_pins(dev);
    g_assert_cmphex(pin_values, ==, 0xff);
}

/* Check that setting pin values and polarity changes inputs as expected */
static void pnv_i2c_pca554_set_pins(pnv_i2c_dev_t *dev)
{
    uint8_t pin_values;
    pnv_i2c_pca9554_set_pin(dev, 0, 0);
    pin_values = pnv_i2c_pca9554_read_pins(dev);
    g_assert_cmphex(pin_values, ==, 0xfe);
    pnv_i2c_pca9554_flip_polarity(dev);
    pin_values = pnv_i2c_pca9554_read_pins(dev);
    g_assert_cmphex(pin_values, ==, 0x01);
    pnv_i2c_pca9554_set_pin(dev, 2, 0);
    pin_values = pnv_i2c_pca9554_read_pins(dev);
    g_assert_cmphex(pin_values, ==, 0x05);
    pnv_i2c_pca9554_flip_polarity(dev);
    pin_values = pnv_i2c_pca9554_read_pins(dev);
    g_assert_cmphex(pin_values, ==, 0xfa);
    pnv_i2c_pca9554_default_cfg(dev);
    pin_values = pnv_i2c_pca9554_read_pins(dev);
    g_assert_cmphex(pin_values, ==, 0xff);
}

static void pnv_i2c_pca9552_default_cfg(pnv_i2c_dev_t *dev)
{
    uint8_t buf[2];
    /* configure pwm/psc regs */
    buf[0] = PCA9552_PSC0;
    buf[1] = 0xff;
    pnv_i2c_send(dev, buf, 2);
    buf[0] = PCA9552_PWM0;
    buf[1] = 0x80;
    pnv_i2c_send(dev, buf, 2);
    buf[0] = PCA9552_PSC1;
    buf[1] = 0xff;
    pnv_i2c_send(dev, buf, 2);
    buf[0] = PCA9552_PWM1;
    buf[1] = 0x80;
    pnv_i2c_send(dev, buf, 2);

    /* configure all pins as inputs */
    buf[0] = PCA9552_LS0;
    buf[1] = 0x55;
    pnv_i2c_send(dev, buf, 2);
    buf[0] = PCA9552_LS1;
    buf[1] = 0x55;
    pnv_i2c_send(dev, buf, 2);
    buf[0] = PCA9552_LS2;
    buf[1] = 0x55;
    pnv_i2c_send(dev, buf, 2);
    buf[0] = PCA9552_LS3;
    buf[1] = 0x55;
    pnv_i2c_send(dev, buf, 2);
}

static void pnv_i2c_pca9552_set_pin(pnv_i2c_dev_t *dev, int pin, bool high)
{
    uint8_t send_buf[2];
    uint8_t recv_buf[2];
    uint8_t reg = PCA9552_LS0 + (pin / 4);
    uint8_t shift = (pin % 4) * 2;
    uint8_t mask = ~(0x3 << shift);
    uint8_t new_value = ((high) ? 1 : 0) << shift;

    /* read current LSx value */
    send_buf[0] = reg;
    pnv_i2c_send(dev, send_buf, 1);
    pnv_i2c_recv(dev, recv_buf, 1);

    /* write new value to LSx */
    send_buf[1] = (recv_buf[0] & mask) | new_value;
    pnv_i2c_send(dev, send_buf, 2);
}

static uint16_t pnv_i2c_pca9552_read_pins(pnv_i2c_dev_t *dev)
{
    uint8_t send_buf[2];
    uint8_t recv_buf[2];
    uint16_t inputs;
    send_buf[0] = PCA9552_INPUT0;
    pnv_i2c_send(dev, send_buf, 1);
    pnv_i2c_recv(dev, recv_buf, 1);
    inputs = recv_buf[0];
    send_buf[0] = PCA9552_INPUT1;
    pnv_i2c_send(dev, send_buf, 1);
    pnv_i2c_recv(dev, recv_buf, 1);
    inputs |= recv_buf[0] << 8;
    return inputs;
}

static void pnv_i2c_pca9552_default_inputs(pnv_i2c_dev_t *dev)
{
    uint16_t pin_values = pnv_i2c_pca9552_read_pins(dev);
    g_assert_cmphex(pin_values, ==, 0xffff);
}

/*
 * Set pins 0-4 one at a time and verify that pins 5-9 are
 * set to the same value
 */
static void pnv_i2c_pca552_set_pins(pnv_i2c_dev_t *dev)
{
    uint16_t pin_values;

    /* set pin 0 low */
    pnv_i2c_pca9552_set_pin(dev, 0, 0);
    pin_values = pnv_i2c_pca9552_read_pins(dev);

    /* pins 0 and 5 should be low */
    g_assert_cmphex(pin_values, ==, 0xffde);

    /* set pin 1 low */
    pnv_i2c_pca9552_set_pin(dev, 1, 0);
    pin_values = pnv_i2c_pca9552_read_pins(dev);

    /* pins 0, 1, 5 and 6 should be low */
    g_assert_cmphex(pin_values, ==, 0xff9c);

    /* set pin 2 low */
    pnv_i2c_pca9552_set_pin(dev, 2, 0);
    pin_values = pnv_i2c_pca9552_read_pins(dev);

    /* pins 0, 1, 2, 5, 6 and 7 should be low */
    g_assert_cmphex(pin_values, ==, 0xff18);

    /* set pin 3 low */
    pnv_i2c_pca9552_set_pin(dev, 3, 0);
    pin_values = pnv_i2c_pca9552_read_pins(dev);

    /* pins 0, 1, 2, 3, 5, 6, 7 and 8 should be low */
    g_assert_cmphex(pin_values, ==, 0xfe10);

    /* set pin 4 low */
    pnv_i2c_pca9552_set_pin(dev, 4, 0);
    pin_values = pnv_i2c_pca9552_read_pins(dev);

    /* pins 0, 1, 2, 3, 5, 6, 7, 8 and 9 should be low */
    g_assert_cmphex(pin_values, ==, 0xfc00);

    /* reset all pins to the high state */
    pnv_i2c_pca9552_default_cfg(dev);
    pin_values = pnv_i2c_pca9552_read_pins(dev);

    /* verify all pins went back to the high state */
    g_assert_cmphex(pin_values, ==, 0xffff);
}

static void reset_engine(QTestState *qts, int engine)
{
    pnv_i2c_xscom_write(qts, engine, I2C_RESET_I2C_REG, 0);
}

static void check_i2cm_por_regs(QTestState *qts, const PnvChip *chip)
{
    int engine;
    for (engine = 0; engine < chip->num_i2c; engine++) {

        /* Check version in Extended Status Register */
        uint64_t value = pnv_i2c_xscom_read(qts, engine, I2C_EXTD_STAT_REG);
        g_assert_cmphex(value & I2C_EXTD_STAT_I2C_VERSION, ==, 0x1700000000);

        /* Check for command complete and bus idle in Status Register */
        value = pnv_i2c_xscom_read(qts, engine, I2C_STAT_REG);
        g_assert_cmphex(value & (I2C_STAT_ANY_ERR | I2C_STAT_CMD_COMP),
                        ==,
                        I2C_STAT_CMD_COMP);
    }
}

static void reset_all(QTestState *qts, const PnvChip *chip)
{
    int engine;
    for (engine = 0; engine < chip->num_i2c; engine++) {
        reset_engine(qts, engine);
        pnv_i2c_xscom_write(qts, engine, I2C_MODE_REG, 0x02be040000000000);
    }
}

static void test_host_i2c(const void *data)
{
    const PnvChip *chip = data;
    QTestState *qts;
    const char *machine = "powernv8";
    pnv_i2c_dev_t pca9552;
    pnv_i2c_dev_t pca9554;

    if (chip->chip_type == PNV_CHIP_POWER9) {
        machine = "powernv9";
    } else if (chip->chip_type == PNV_CHIP_POWER10) {
        machine = "powernv10-rainier";
    }

    qts = qtest_initf("-M %s -smp %d,cores=1,threads=%d -nographic "
                      "-nodefaults -serial mon:stdio -S "
                      "-d guest_errors",
                      machine, SMT, SMT);

    /* Check the I2C master status registers after POR */
    check_i2cm_por_regs(qts, chip);

    /* Now do a forced "immediate" reset on all engines */
    reset_all(qts, chip);

    /* Check that the status values are still good */
    check_i2cm_por_regs(qts, chip);

    /* P9 doesn't have any i2c devices attached at this time */
    if (chip->chip_type != PNV_CHIP_POWER10) {
        qtest_quit(qts);
        return;
    }

    /* Initialize for a P10 pca9552 hotplug device */
    pca9552.qts = qts;
    pca9552.engine = 2;
    pca9552.port = 1;
    pca9552.addr = 0x63;

    /* Set all pca9552 pins as inputs */
    pnv_i2c_pca9552_default_cfg(&pca9552);

    /* Check that all pins of the pca9552 are high */
    pnv_i2c_pca9552_default_inputs(&pca9552);

    /* perform individual pin tests */
    pnv_i2c_pca552_set_pins(&pca9552);

    /* Initialize for a P10 pca9554 CableCard Presence detection device */
    pca9554.qts = qts;
    pca9554.engine = 2;
    pca9554.port = 1;
    pca9554.addr = 0x25;

    /* Set all pca9554 pins as inputs */
    pnv_i2c_pca9554_default_cfg(&pca9554);

    /* Check that all pins of the pca9554 are high */
    pnv_i2c_pca9554_default_inputs(&pca9554);

    /* perform individual pin tests */
    pnv_i2c_pca554_set_pins(&pca9554);

    qtest_quit(qts);
}

static void add_test(const char *name, void (*test)(const void *data))
{
    int i;

    for (i = 0; i < ARRAY_SIZE(pnv_chips); i++) {
        char *tname = g_strdup_printf("pnv-xscom/%s/%s", name,
                                      pnv_chips[i].cpu_model);
        qtest_add_data_func(tname, &pnv_chips[i], test);
        g_free(tname);
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    add_test("host-i2c", test_host_i2c);
    return g_test_run();
}
