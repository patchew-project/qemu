// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef QEMU_MAX11615_H
#define QEMU_MAX11615_H

#include <stdint.h>
#include "qemu/osdep.h"
#include "hw/i2c/i2c.h"

#define TYPE_MAX11615 "max11615"

/*
 * Create and realize a MAX11615 ADC with constant caller-supplied readings
 * @bus: I2C bus to put it on
 * @address: I2C address
 * @init_values: array of readings for each ADC channel
 * @init_values_size: Size of @init_values, can be less than the number of channels
 */
I2CSlave *max11615_init_with_values(I2CBus *bus, uint8_t address,
                                    const uint16_t *init_values, uint32_t init_values_size);

#endif
