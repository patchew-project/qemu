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
#ifndef QTEST_ASPEED_H
#define QTEST_ASPEED_H

#include <stdint.h>

uint8_t aspeed_i2c_readb(uint32_t baseaddr, uint8_t slave_addr, uint8_t reg);
uint16_t aspeed_i2c_readw(uint32_t baseaddr, uint8_t slave_addr, uint8_t reg);
uint32_t aspeed_i2c_readl(uint32_t baseaddr, uint8_t slave_addr, uint8_t reg);
void aspeed_i2c_writeb(uint32_t baseaddr, uint8_t slave_addr,
                       uint8_t reg, uint8_t v);
void aspeed_i2c_writew(uint32_t baseaddr, uint8_t slave_addr,
                       uint8_t reg, uint16_t v);
void aspeed_i2c_writel(uint32_t baseaddr, uint8_t slave_addr,
                       uint8_t reg, uint32_t v);

#endif
