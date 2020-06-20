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

#define TYPE_LED "led"
#define LED(obj) OBJECT_CHECK(LEDState, (obj), TYPE_LED)

typedef enum {
    LED_COLOR_UNKNOWN,
    LED_COLOR_RED,
    LED_COLOR_ORANGE,
    LED_COLOR_AMBER,
    LED_COLOR_YELLOW,
    LED_COLOR_GREEN,
    LED_COLOR_BLUE,
    LED_COLOR_VIOLET, /* PURPLE */
    LED_COLOR_WHITE,
    LED_COLOR_COUNT
} LEDColor;

/* Definitions useful when a LED is connected to a GPIO */
#define LED_RESET_INTENSITY_ACTIVE_LOW  UINT16_MAX
#define LED_RESET_INTENSITY_ACTIVE_HIGH 0U

typedef struct LEDState {
    /* Private */
    DeviceState parent_obj;
    /* Public */

    qemu_irq irq;

    /* Properties */
    char *description;
    char *color;
    /*
     * When used with GPIO, the intensity at reset is related to GPIO polarity
     */
    uint16_t reset_intensity;
} LEDState;

/**
 * led_set_intensity: set the intensity of a LED device
 * @s: the LED object
 * @intensity: new intensity
 *
 * This utility is meant for LED connected to PWM.
 */
void led_set_intensity(LEDState *s, uint16_t intensity);

/**
 * led_set_intensity: set the state of a LED device
 * @s: the LED object
 * @is_on: boolean indicating whether the LED is emitting
 *
 * This utility is meant for LED connected to GPIO.
 */
void led_set_state(LEDState *s, bool is_on);

/**
 * create_led: create and LED device
 * @parent: the parent object
 * @color: color of the LED
 * @description: description of the LED (optional)
 * @reset_intensity: LED intensity at reset
 *
 * This utility function creates a LED object.
 */
DeviceState *create_led(Object *parentobj,
                        LEDColor color,
                        const char *description,
                        uint16_t reset_intensity);

/**
 * create_led_by_gpio_id: create and LED device and connect it to a GPIO output
 * @parent: the parent object
 * @gpio_dev: device exporting GPIOs
 * @gpio_id: GPIO ID of this LED
 * @color: color of the LED
 * @description: description of the LED (optional)
 * @reset_intensity: LED intensity at reset
 *
 * This utility function creates a LED and connects it to a
 * GPIO exported by another device.
 */
DeviceState *create_led_by_gpio_id(Object *parentobj,
                                   DeviceState *gpio_dev, unsigned gpio_id,
                                   LEDColor color,
                                   const char *description,
                                   uint16_t reset_intensity);

#endif /* HW_MISC_LED_H */
