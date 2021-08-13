/*
 * *AT24C* series I2C EEPROM
 *
 * Copyright (c) 2015 Michael Davidsaver
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the LICENSE file in the top-level directory.
 */

/* Init one at24c eeprom device */
void at24c_eeprom_init_one(I2CBus *i2c_bus, int bus, uint8_t addr,
                           uint32_t rsize, int unit_number);

