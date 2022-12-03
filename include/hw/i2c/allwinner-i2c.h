/*
 *  Allwinner I2C Bus Serial Interface registers definition
 *
 *  Copyright (C) 2022 Strahinja Jankovic. <strahinja.p.jankovic@gmail.com>
 *
 *  This file is derived from IMX I2C controller,
 *  by Jean-Christophe DUBOIS .
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef ALLWINNER_I2C_H
#define ALLWINNER_I2C_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_AW_I2C "allwinner.i2c"
OBJECT_DECLARE_SIMPLE_TYPE(AWI2CState, AW_I2C)

#define AW_I2C_MEM_SIZE         0x24

/* Allwinner I2C memory map */
#define TWI_ADDR_REG            0x00  /* slave address register */
#define TWI_XADDR_REG           0x04  /* extended slave address register */
#define TWI_DATA_REG            0x08  /* data register */
#define TWI_CNTR_REG            0x0c  /* control register */
#define TWI_STAT_REG            0x10  /* status register */
#define TWI_CCR_REG             0x14  /* clock control register */
#define TWI_SRST_REG            0x18  /* software reset register */
#define TWI_EFR_REG             0x1c  /* enhance feature register */
#define TWI_LCR_REG             0x20  /* line control register */

/* Used only in slave mode, do not set */
#define TWI_ADDR_RESET          0
#define TWI_XADDR_RESET         0

/* Data register */
#define TWI_DATA_MASK           0xFF
#define TWI_DATA_RESET          0

/* Control register */
#define TWI_CNTR_INT_EN         (1 << 7)
#define TWI_CNTR_BUS_EN         (1 << 6)
#define TWI_CNTR_M_STA          (1 << 5)
#define TWI_CNTR_M_STP          (1 << 4)
#define TWI_CNTR_INT_FLAG       (1 << 3)
#define TWI_CNTR_A_ACK          (1 << 2)
#define TWI_CNTR_MASK           0xFC
#define TWI_CNTR_RESET          0

/* Status register */
#define TWI_STAT_MASK           0xF8
#define TWI_STAT_RESET          0xF8

/* Clock register */
#define TWI_CCR_CLK_M_MASK      0x78
#define TWI_CCR_CLK_N_MASK      0x07
#define TWI_CCR_MASK            0x7F
#define TWI_CCR_RESET           0

/* Soft reset */
#define TWI_SRST_MASK           0x01
#define TWI_SRST_RESET          0

/* Enhance feature */
#define TWI_EFR_MASK            0x03
#define TWI_EFR_RESET           0

/* Line control */
#define TWI_LCR_SCL_STATE       (1 << 5)
#define TWI_LCR_SDA_STATE       (1 << 4)
#define TWI_LCR_SCL_CTL         (1 << 3)
#define TWI_LCR_SCL_CTL_EN      (1 << 2)
#define TWI_LCR_SDA_CTL         (1 << 1)
#define TWI_LCR_SDA_CTL_EN      (1 << 0)
#define TWI_LCR_MASK            0x3F
#define TWI_LCR_RESET           0x3A

struct AWI2CState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    I2CBus *bus;
    qemu_irq irq;

    uint8_t addr;
    uint8_t xaddr;
    uint8_t data;
    uint8_t cntr;
    uint8_t stat;
    uint8_t ccr;
    uint8_t srst;
    uint8_t efr;
    uint8_t lcr;
};

#endif /* ALLWINNER_I2C_H */
