/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#ifndef EEPROM_AT24C_H
#define EEPROM_AT24C_H

#include "hw/i2c/i2c.h"

I2CSlave *at24c_eeprom_init(I2CBus *bus, uint8_t address, uint32_t rom_size);

#endif
