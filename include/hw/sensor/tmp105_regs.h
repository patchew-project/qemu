/*
 * Texas Instruments TMP105 Temperature Sensor I2C messages
 *
 * Browse the data sheet:
 *
 *    http://www.ti.com/lit/gpn/tmp105
 *
 * Copyright (C) 2012 Alex Horn <alex.horn@cs.ox.ac.uk>
 * Copyright (C) 2008-2012 Andrzej Zaborowski <balrogg@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later. See the COPYING file in the top-level directory.
 */

#ifndef TMP105_REGS_H
#define TMP105_REGS_H

/**
 * TMP105Reg:
 * @TMP105_REG_TEMPERATURE: Temperature register
 * @TMP105_REG_CONFIG: Configuration register
 * @TMP105_REG_T_LOW: Low temperature register (also known as T_hyst)
 * @TMP105_REG_T_HIGH: High temperature register (also known as T_OS)
 * @TMP105_REG_DIE_ID: Device ID register (TMP1075 only)
 *
 * The following temperature sensors are
 * compatible with the TMP105 registers:
 * - adt75
 * - ds1775
 * - ds75
 * - lm75
 * - lm75a
 * - max6625
 * - max6626
 * - mcp980x
 * - stds75
 * - tcn75
 * - tmp100
 * - tmp101
 * - tmp105
 * - tmp175
 * - tmp275
 * - tmp75
 **/
typedef enum TMP105Reg {
    TMP105_REG_TEMPERATURE = 0,
    TMP105_REG_CONFIG,
    TMP105_REG_T_LOW,
    TMP105_REG_T_HIGH,
    TMP105_REG_DIE_ID = 0x0f,
} TMP105Reg;

/* Value returned by the TMP1075 Device ID register. */
#define TMP1075_DEVICE_ID 0x7500

#endif
