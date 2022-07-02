#ifndef QEMU_I2C_MUX_PCA954X_H
#define QEMU_I2C_MUX_PCA954X_H

#include "hw/i2c/i2c.h"

#define TYPE_PCA9546 "pca9546"
#define TYPE_PCA9548 "pca9548"

/**
 * Retrieves the i2c bus associated with the specified channel on this i2c
 * mux.
 * @mux: an i2c mux device.
 * @channel: the i2c channel requested
 *
 * Returns: a pointer to the associated i2c bus.
 */
I2CBus *pca954x_i2c_get_bus(I2CSlave *mux, uint8_t channel);

/**
 * Creates an i2c mux and retrieves all of the channels associated with it.
 *
 * @bus: the i2c bus where the i2c mux resides.
 * @address: the address of the i2c mux on the aforementioned i2c bus.
 * @type_name: name of the i2c mux type to create.
 * @channels: an output parameter specifying where to return the channels.
 *
 * Returns: None
 */
void pca954x_i2c_get_channels(I2CBus *bus, uint8_t address,
                              const char *type_name, I2CBus **channels);

#endif
