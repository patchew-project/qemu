/*
 * QEMU single LED device
 *
 * Copyright (C) 2020 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_MISC_LED_H
#define HW_MISC_LED_H

#include "hw/qdev-core.h"
#include "hw/sysbus.h" /* FIXME remove */

#define TYPE_LED "led"
#define LED(obj) OBJECT_CHECK(LEDState, (obj), TYPE_LED)

typedef struct LEDState {
    /* Private */
    SysBusDevice parent_obj; /* FIXME DeviceState */
    /* Public */

    qemu_irq irq;
    uint8_t current_state;
    int64_t last_event_ms;

    /* Properties */
    char *name;
    uint8_t reset_state; /* TODO [GPIO_ACTIVE_LOW, GPIO_ACTIVE_HIGH] */
} LEDState;

/**
 * create_led_by_gpio_id: create and LED device
 * @parent: the parent object
 * @gpio_dev: device exporting GPIOs
 * @gpio_id: GPIO ID of this LED
 * @name: name of the LED
 *
 * This utility function creates a LED and connects it to a
 * GPIO exported by another device.
 */
DeviceState *create_led_by_gpio_id(Object *parentobj,
                                   DeviceState *gpio_dev, unsigned gpio_id,
                                   const char *led_name);

#endif /* HW_MISC_LED_H */
