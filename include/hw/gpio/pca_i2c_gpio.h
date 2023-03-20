/*
 * NXP PCA6416A
 * Low-voltage translating 16-bit I2C/SMBus GPIO expander with interrupt output,
 * reset, and configuration registers
 *
 * Datasheet: https://www.nxp.com/docs/en/data-sheet/PCA6416A.pdf
 *
 * Note: Polarity inversion emulation not implemented
 *
 * Copyright 2021 Google LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef PCA_I2C_GPIO_H
#define PCA_I2C_GPIO_H

#include "hw/i2c/i2c.h"
#include "qom/object.h"

#define PCA_I2C_MAX_PINS                     16
#define PCA6416_NUM_PINS                     16
#define PCA9538_NUM_PINS                     8

typedef struct PCAGPIOClass {
    I2CSlaveClass parent;

    uint8_t num_pins;
} PCAGPIOClass;

typedef struct PCAGPIOState {
    I2CSlave parent;

    uint16_t polarity_inv;
    uint16_t config;

    /* the values of the gpio pins are mirrored in these integers */
    uint16_t curr_input;
    uint16_t curr_output;
    uint16_t new_input;
    uint16_t new_output;

    /*
     * Note that these outputs need to be consumed by some other input
     * to be useful, qemu ignores writes to disconnected gpio pins
     */
    qemu_irq output[PCA_I2C_MAX_PINS];

    /* i2c transaction info */
    uint8_t command;
    bool i2c_cmd;

} PCAGPIOState;

#define TYPE_PCA_I2C_GPIO "pca_i2c_gpio"
OBJECT_DECLARE_TYPE(PCAGPIOState, PCAGPIOClass, PCA_I2C_GPIO)

#define PCA953x_INPUT_PORT                   0x00 /* read */
#define PCA953x_OUTPUT_PORT                  0x01 /* read/write */
#define PCA953x_POLARITY_INVERSION_PORT      0x02 /* read/write */
#define PCA953x_CONFIGURATION_PORT           0x03 /* read/write */

#define PCA_I2C_CONFIG_DEFAULT               0

#define TYPE_PCA6416_GPIO "pca6416"
#define TYPE_PCA9538_GPIO "pca9538"

#endif
