// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * QEMU GPIO device frontend.
 *
 * Author: 2025 Nikita Shubin <n.shubin@yadro.com>
 *
 */
#ifndef QEMU_GPIO_FE_H
#define QEMU_GPIO_FE_H

#include "qemu/main-loop.h"

#include "gpiodev/gpio.h"

/**
 * LineInfoHandler: Return the gpio line info specified by offset
 */
typedef void LineInfoHandler(void *opaque, gpio_line_info *info);

/**
 * LineInfoHandler: Return the gpio line value specified by offset
 */
typedef int LineGetValueHandler(void *opaque, uint32_t offset);

/**
 * LineSetValueHandler: Set the gpio line value specified by offset
 */
typedef int LineSetValueHandler(void *opaque, uint32_t offset, uint8_t value);

/**
 * struct GpioBackend - back end as seen by front end
 *
 * The actual backend is Gpiodev
 */
struct GpioBackend {
    Gpiodev *gpio;
    LineInfoHandler *line_info;
    LineGetValueHandler *get_value;
    LineSetValueHandler *set_value;
    void *opaque;
};

/**
 * qemu_gpio_fe_deinit:
 *
 * @b: a GpioBackend
 * @s: a Gpiodev
 * @nlines: number of lines in the GPIO Port
 * @name: name of the GPIO Port
 * @label: label of the GPIO Port
 * @errp: error if any
 *
 * Initializes a front end for the given GpioBackend and
 * Gpiodev. Call qemu_gpio_fe_deinit() to remove the association and
 * release the driver.
 *
 * nlines, name and label used for proving information
 * via qemu_gpiodev_set_info().
 *
 * Returns: false on error.
 */
bool qemu_gpio_fe_init(GpioBackend *b, Gpiodev *s, uint32_t nlines,
                       const char *name, const char *label,
                       Error **errp);

/**
 * qemu_gpio_fe_set_handlers:
 *
 * @b: a GpioBackend
 * @s: a Gpiodev
 * @line_info: Line info handler to provide info about line
 * @get_value: Get line value handler
 * @set_value: Set line value handler
 * @opaque: an opaque pointer for the callbacks
 * @context: a main loop context or NULL for the default
 *
 * Set the front end gpio handlers.
 *
 */
void qemu_gpio_fe_set_handlers(GpioBackend *b,
                               LineInfoHandler *line_info,
                               LineGetValueHandler *get_value,
                               LineSetValueHandler *set_value,
                               void *opaque);

/**
 * qemu_gpio_fe_deinit:
 *
 * @b: a GpioBackend
 * @del: if true, delete the gpiodev backend
 *
 * Dissociate the GpioBackend from the Gpiodev.
 *
 * Safe to call without associated Gpiodev.
 */
void qemu_gpio_fe_deinit(GpioBackend *b, bool del);

/**
 * qemu_gpio_fe_line_event:
 *
 * @b: a GpioBackend
 * @offset: line number offset
 * @event: rising or falling edge event
 *
 * See enum QEMUGpioEvent.
 */
bool qemu_gpio_fe_line_event(GpioBackend *b, uint32_t offset,
                             QEMUGpioLineEvent event);

/**
 * qemu_gpio_fe_config_event:
 *
 * @b: a GpioBackend
 * @offset: line number offset
 * @event: requested, released or input/output toggle
 *
 * See enum QEMUGpioConfigEvent.
 */
bool qemu_gpio_fe_config_event(GpioBackend *b, uint32_t offset,
                               QEMUGpioConfigEvent event);

#endif /* QEMU_GPIO_FE_H */
