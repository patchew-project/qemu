// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * QEMU GPIO device.
 *
 * Author: 2025 Nikita Shubin <n.shubin@yadro.com>
 *
 */
#ifndef QEMU_GPIO_H
#define QEMU_GPIO_H

#include "qapi/qapi-types-gpio.h"
#include "qom/object.h"
#include "qemu/bitops.h"

/* gpio back-end device */
typedef struct GpioBackend GpioBackend;

#define GPIO_MAX_NAME_SIZE 32

/* compatible with enum gpio_v2_line_flag */
typedef enum QEMUGpioLineFlags {
	GPIO_LINE_FLAG_INPUT            = BIT_ULL(2),
	GPIO_LINE_FLAG_OUTPUT           = BIT_ULL(3),
} QEMUGpioLineFlags;

typedef enum QEMUGpioLineEvent {
	GPIO_EVENT_RISING_EDGE	= 1,
	GPIO_EVENT_FALLING_EDGE	= 2,
} QEMUGpioLineEvent;

typedef enum QEMUGpioConfigEvent {
	GPIO_LINE_CHANGED_REQUESTED	= 1,
	GPIO_LINE_CHANGED_RELEASED	= 2,
	GPIO_LINE_CHANGED_CONFIG	= 3,
} QEMUGpioConfigEvent;

struct Gpiodev {
    Object parent_obj;

    GpioBackend *be;

    uint32_t lines;
    char name[GPIO_MAX_NAME_SIZE];
	char label[GPIO_MAX_NAME_SIZE];

    struct {
        unsigned long *risen;
        unsigned long *fallen;
        unsigned long *config;
    } mask;

    GMainContext *gcontext;
};

#define TYPE_GPIODEV "gpiodev"
OBJECT_DECLARE_TYPE(Gpiodev, GpiodevClass, GPIODEV)

#define TYPE_GPIODEV_CHARDEV "gpiodev-chardev"
#define TYPE_GPIODEV_GUSEDEV "gpiodev-guse"

struct GpiodevClass {
    ObjectClass parent_class;

    /* parse command line options and populate QAPI @backend */
    void (*parse)(QemuOpts *opts, GpiodevBackend *backend, Error **errp);

    /* called after construction, open/starts the backend */
    void (*open)(Gpiodev *gpio, GpiodevBackend *backend, Error **errp);

    /* notify backend about line event */
    void (*line_event)(Gpiodev *g, uint32_t offset,
                       QEMUGpioLineEvent event);

    /* notify backend about config event */
    void (*config_event)(Gpiodev *g, uint32_t offset,
                         QEMUGpioConfigEvent event);
};

/**
 * qemu_gpiodev_set_info:
 *
 * @g: a Gpiodev
 * @nlines: number of lines in the GPIO Port
 * @name: name of the GPIO Port
 * @label: label of the GPIO Port
 *
 * Set basic info about GPIO Port, used by backends to provide data
 * to client applications.
 *
 * nlines, name and label used for proving information
 * via qemu_gpio_chip_info().
 */
void qemu_gpiodev_set_info(Gpiodev *g, uint32_t nlines,
                           const char *name, const char *label);

/**
 * qemu_gpio_chip_info:
 *
 * @g: a Gpiodev
 * @nlines: lines number of the GPIO Port will be set
 * @name: name of the GPIO Port will be set
 * @label: label of the GPIO Port will be set
 *
 * If GpioBackend is NULL, nlines will be set to zero and
 * both name and label to NULL.
 */
void qemu_gpio_chip_info(Gpiodev *g, uint32_t *nlines,
                         char *name, char *label);

typedef struct gpio_line_info {
	char name[GPIO_MAX_NAME_SIZE];
	char consumer[GPIO_MAX_NAME_SIZE];
	uint32_t offset;
	uint64_t flags;
} gpio_line_info;

/**
 * qemu_gpio_line_info:
 *
 * @g: a Gpiodev
 * @info: info about requested line
 *
 * info->offset should be provided see gpio_line_info.
 */
void qemu_gpio_line_info(Gpiodev *g, gpio_line_info *info);

/**
 * qemu_gpio_set_line_value:
 *
 * @g: a Gpiodev
 * @offset: line offset
 * @value: line value
 */
void qemu_gpio_set_line_value(Gpiodev *g, uint32_t offset, uint8_t value);

/**
 * qemu_gpio_get_line_value:
 *
 * @g: a Gpiodev
 * @offset: line offset
 *
 * returns 0 or 1 line status
 */
uint8_t qemu_gpio_get_line_value(Gpiodev *g, uint32_t offset);

/**
 * qemu_gpio_add_event_watch:
 *
 * @g: a Gpiodev
 * @offset: line offset
 * @flags: event flags
 *
 * See QEMUGpioLineEvent.
 *
 * Add lines specified by mask and flags to watch, called by GpiodevBackend to subscribe
 * desired lines and events.
 */
void qemu_gpio_add_event_watch(Gpiodev *g, uint32_t offset, uint64_t flags);

/**
 * qemu_gpio_clear_event_watch:
 *
 * @g: a Gpiodev
 * @offset: line offset
 * @flags: event flags
 *
 * See QEMUGpioLineEvent.
 *
 * Remove lines specified by mask and flags from watch, called by GpiodevBackend.
 */
void qemu_gpio_clear_event_watch(Gpiodev *g, uint32_t offset, uint64_t flags);

/**
 * qemu_gpio_add_config_watch:
 *
 * @g: a Gpiodev
 * @offset: line offset
 *
 * See QEMUGpioConfigEvent.
 *
 * Add lines specified by mask to watch, called by GpiodevBackend to subscribe
 * about desired lines config change.
 */
void qemu_gpio_add_config_watch(Gpiodev *g, uint32_t offset);

/**
 * qemu_gpio_clear_config_watch:
 *
 * @g: a Gpiodev
 * @mask: lines mask to clear
 *
 * See QEMUGpioConfigEvent.
 *
 * Remove lines specified by mask from watch, called by GpiodevBackend.
 */
void qemu_gpio_clear_config_watch(Gpiodev *g, uint32_t offset);

void qemu_gpio_clear_watches(Gpiodev *g);


/**
 * qemu_gpio_line_event:
 *
 * @g: a Gpiodev
 * @offset: line offset
 * @event: event
 *
 * See QEMUGpioLineEvent.
 *
 * Called by GpioBackend to notify Gpiodev about line event, i.e. line set to 0/1.
 */
void qemu_gpio_line_event(Gpiodev *gpio, uint32_t offset,
                          QEMUGpioLineEvent event);

/**
 * qemu_gpio_config_event:
 *
 * @g: a Gpiodev
 * @offset: line offset
 * @event: event
 *
 * See QEMUGpioConfigEvent.
 *
 * Called by GpioBackend to notify Gpiodev about line config event,
 * i.e. input switched to output.
 */
void qemu_gpio_config_event(Gpiodev *g, uint32_t offset,
                          QEMUGpioConfigEvent event);

Gpiodev *qemu_gpiodev_add(QemuOpts *opts, GMainContext *context,
                          Error **errp);

#endif /* QEMU_GPIO_H */
