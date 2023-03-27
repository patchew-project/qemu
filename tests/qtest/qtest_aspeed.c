/*
 * Aspeed i2c bus interface to reading and writing to i2c device registers
 *
 * Copyright (c) 2023 IBM Corporation
 *
 * Authors:
 *   Stefan Berger <stefanb@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "qtest_aspeed.h"

#include "hw/i2c/aspeed_i2c.h"
#include "libqtest-single.h"

#define A_I2CD_M_STOP_CMD       BIT(5)
#define A_I2CD_M_RX_CMD         BIT(3)
#define A_I2CD_M_TX_CMD         BIT(1)
#define A_I2CD_M_START_CMD      BIT(0)

#define A_I2CD_MASTER_EN        BIT(0)

static void aspeed_i2c_startup(uint32_t baseaddr, uint8_t slave_addr,
                               uint8_t reg)
{
    uint32_t v;
    static int once;

    if (!once) {
        /* one time: enable master */
       writel(baseaddr + A_I2CC_FUN_CTRL, 0);
       v = readl(baseaddr + A_I2CC_FUN_CTRL) | A_I2CD_MASTER_EN;
       writel(baseaddr + A_I2CC_FUN_CTRL, v);
       once = 1;
    }

    /* select device */
    writel(baseaddr + A_I2CD_BYTE_BUF, slave_addr << 1);
    writel(baseaddr + A_I2CD_CMD, A_I2CD_M_START_CMD | A_I2CD_M_RX_CMD);

    /* select the register to write to */
    writel(baseaddr + A_I2CD_BYTE_BUF, reg);
    writel(baseaddr + A_I2CD_CMD, A_I2CD_M_TX_CMD);
}

static uint32_t aspeed_i2c_read_n(uint32_t baseaddr, uint8_t slave_addr,
                                  uint8_t reg, size_t nbytes)
{
    uint32_t res = 0;
    uint32_t v;
    size_t i;

    aspeed_i2c_startup(baseaddr, slave_addr, reg);

    for (i = 0; i < nbytes; i++) {
        writel(baseaddr + A_I2CD_CMD, A_I2CD_M_RX_CMD);
        v = readl(baseaddr + A_I2CD_BYTE_BUF) >> 8;
        res |= (v & 0xff) << (i * 8);
    }

    writel(baseaddr + A_I2CD_CMD, A_I2CD_M_STOP_CMD);

    return res;
}

uint32_t aspeed_i2c_readl(uint32_t baseaddr, uint8_t slave_addr, uint8_t reg)
{
    return aspeed_i2c_read_n(baseaddr, slave_addr, reg, sizeof(uint32_t));
}

uint16_t aspeed_i2c_readw(uint32_t baseaddr, uint8_t slave_addr, uint8_t reg)
{
    return aspeed_i2c_read_n(baseaddr, slave_addr, reg, sizeof(uint16_t));
}

uint8_t aspeed_i2c_readb(uint32_t baseaddr, uint8_t slave_addr, uint8_t reg)
{
    return aspeed_i2c_read_n(baseaddr, slave_addr, reg, sizeof(uint8_t));
}

static void aspeed_i2c_write_n(uint32_t baseaddr, uint8_t slave_addr,
                               uint8_t reg, uint32_t v, size_t nbytes)
{
    size_t i;

    aspeed_i2c_startup(baseaddr, slave_addr, reg);

    for (i = 0; i < nbytes; i++) {
        writel(baseaddr + A_I2CD_BYTE_BUF, v & 0xff);
        v >>= 8;
        writel(baseaddr + A_I2CD_CMD, A_I2CD_M_TX_CMD);
    }

    writel(baseaddr + A_I2CD_CMD, A_I2CD_M_STOP_CMD);
}

void aspeed_i2c_writel(uint32_t baseaddr, uint8_t slave_addr,
                       uint8_t reg, uint32_t v)
{
    aspeed_i2c_write_n(baseaddr, slave_addr, reg, v, sizeof(v));
}

void aspeed_i2c_writew(uint32_t baseaddr, uint8_t slave_addr,
                       uint8_t reg, uint16_t v)
{
    aspeed_i2c_write_n(baseaddr, slave_addr, reg, v, sizeof(v));
}

void aspeed_i2c_writeb(uint32_t baseaddr, uint8_t slave_addr,
                       uint8_t reg, uint8_t v)
{
    aspeed_i2c_write_n(baseaddr, slave_addr, reg, v, sizeof(v));
}
