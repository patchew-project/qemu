
#ifndef QEMU_SMBUS_EEPROM_H
#define QEMU_SMBUS_EEPROM_H

#include "hw/i2c/i2c.h"

#define SMBUS_EEPROM_MAX 8

void smbus_eeprom_init_one(I2CBus *bus, uint8_t address, uint8_t *eeprom_buf);
void smbus_eeprom_init(I2CBus *bus, unsigned int nb_eeprom,
                       const uint8_t *eeprom_spd, int size);

#endif
