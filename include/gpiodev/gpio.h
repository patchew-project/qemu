// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * QEMU GPIO device.
 *
 * Author: 2025 Nikita Shubin <n.shubin@yadro.com>
 *
 */
#ifndef QEMU_GPIO_H
#define QEMU_GPIO_H

#include "qom/object.h"

/* gpio back-end device */
typedef struct GpioBackend GpioBackend;

struct Gpiodev {
    Object parent_obj;

    GpioBackend *be;

    GMainContext *gcontext;
};

struct GpiodevClass {
    ObjectClass parent_class;
};

#define TYPE_GPIODEV "gpiodev"
OBJECT_DECLARE_TYPE(Gpiodev, GpiodevClass, GPIODEV)

Gpiodev *qemu_gpiodev_add(QemuOpts *opts, GMainContext *context,
                          Error **errp);

#endif /* QEMU_GPIO_H */
