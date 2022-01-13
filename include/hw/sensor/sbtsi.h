/*
 * AMD SBI Temperature Sensor Interface (SB-TSI)
 *
 * Copyright 2021 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */
#ifndef QEMU_TMP_SBTSI_H
#define QEMU_TMP_SBTSI_H

/*
 * SB-TSI registers only support SMBus byte data access. "_INT" registers are
 * the integer part of a temperature value or limit, and "_DEC" registers are
 * corresponding decimal parts.
 */
#define SBTSI_REG_TEMP_INT      0x01 /* RO */
#define SBTSI_REG_STATUS        0x02 /* RO */
#define SBTSI_REG_CONFIG        0x03 /* RO */
#define SBTSI_REG_TEMP_HIGH_INT 0x07 /* RW */
#define SBTSI_REG_TEMP_LOW_INT  0x08 /* RW */
#define SBTSI_REG_CONFIG_WR     0x09 /* RW */
#define SBTSI_REG_TEMP_DEC      0x10 /* RO */
#define SBTSI_REG_TEMP_HIGH_DEC 0x13 /* RW */
#define SBTSI_REG_TEMP_LOW_DEC  0x14 /* RW */
#define SBTSI_REG_ALERT_CONFIG  0xBF /* RW */
#define SBTSI_REG_MAN           0xFE /* RO */
#define SBTSI_REG_REV           0xFF /* RO */

#define SBTSI_STATUS_HIGH_ALERT BIT(4)
#define SBTSI_STATUS_LOW_ALERT  BIT(3)
#define SBTSI_CONFIG_ALERT_MASK BIT(7)
#define SBTSI_ALARM_EN          BIT(0)

/* The temperature we stored are in units of 0.125 degrees. */
#define SBTSI_TEMP_UNIT_IN_MILLIDEGREE 125

#endif
