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
    SysBusDevice parent;

    CharBackend chr;
} GoogleGPIOTXState;

void google_gpio_tx_transmit(GoogleGPIOTXState *s, uint8_t controller,
                             uint32_t gpios);

#endif /* GOOGLE_GPIO_TRANSMITTER_H */
