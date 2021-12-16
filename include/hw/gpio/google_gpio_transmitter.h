/*
 * Google GPIO Transmitter.
 *
 * This is a fake hardware model that does not exist on any board or IC.
 * The purpose of this model is to aggregate GPIO state changes from a GPIO
 * controller and transmit them via chardev.
 *
 * Copyright 2021 Google LLC
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#ifndef GOOGLE_GPIO_TRANSMITTER_H
#define GOOGLE_GPIO_TRANSMITTER_H

#include "chardev/char-fe.h"
#include "hw/sysbus.h"

#define TYPE_GOOGLE_GPIO_TRANSMITTER "google.gpio-transmitter"
#define GOOGLE_GPIO_TX(obj) \
    OBJECT_CHECK(GoogleGPIOTXState, (obj), TYPE_GOOGLE_GPIO_TRANSMITTER)

#define GPIO_TX_NUM_CONTROLLERS 8

typedef enum {
    GPIOTXCODE_OK              = 0x00,
    GPIOTXCODE_MALFORMED_PKT   = 0xe0,
    GPIOTXCODE_UNKNOWN_VERSION = 0xe1,
} GoogleGPIOTXCode;

typedef struct {
    uint32_t gpios;
    uint32_t allowed;
} GPIOCtlrState;

typedef struct {
    SysBusDevice parent;

    GHashTable *gpio_state_by_ctlr;
    uint32_t *gpio_allowlist;
    uint32_t gpio_allowlist_sz;

    CharBackend chr;
} GoogleGPIOTXState;

void google_gpio_tx_transmit(GoogleGPIOTXState *s, uint8_t controller,
                             uint32_t gpios);
/*
 * If using an allowlist, this function should be called by the GPIO controller
 * to set an initial state of the controller's GPIO pins.
 * Otherwise all pins will be assumed to have an initial state of 0.
 */
void google_gpio_tx_state_init(GoogleGPIOTXState *s, uint8_t controller,
                               uint32_t gpios);

/* Helper function to be called to initialize the allowlist qdev properties */
void google_gpio_tx_allowlist_qdev_init(GoogleGPIOTXState *s,
                                        const uint32_t *allowed_pins,
                                        size_t num);
#endif /* GOOGLE_GPIO_TRANSMITTER_H */
