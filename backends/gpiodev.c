/*
 * QEMU GPIO Backend
 *
 * Copyright (C) 2018-2020 Glider bv
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <errno.h>
#include <gpiod.h>

#include "qemu/osdep.h"
#include "qemu/config-file.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qapi/error.h"

#include "sysemu/gpiodev.h"

#include "hw/irq.h"
#include "hw/qdev-core.h"

static void gpiodev_irq_handler(void *opaque, int n, int level)
{
    struct gpiod_line *line = opaque;
    int status;

    status = gpiod_line_set_value(line, level);
    if (status < 0) {
        struct gpiod_chip *chip = gpiod_line_get_chip(line);

        error_report("%s/%s: Cannot set GPIO line %u: %s",
                     gpiod_chip_name(chip), gpiod_chip_label(chip),
                     gpiod_line_offset(line), strerror(errno));
    }
}

static int gpiodev_map_line(DeviceState *dev, struct gpiod_chip *chip,
                            unsigned int gpio, Error **errp)
{
    struct gpiod_line *line;
    qemu_irq irq;
    int status;

    line = gpiod_chip_get_line(chip, gpio);
    if (!line) {
        error_setg(errp, "Cannot obtain GPIO line %u: %s", gpio,
                   strerror(errno));
        return -1;
    }

    status = gpiod_line_request_output(line, "qemu", 0);
    if (status < 0) {
        error_setg(errp, "Cannot request GPIO line %u for output: %s", gpio,
                   strerror(errno));
        return status;
    }

    irq = qemu_allocate_irq(gpiodev_irq_handler, line, 0);
    qdev_connect_gpio_out(dev, gpio, irq);
    return 0;
}

void qemu_gpiodev_add(DeviceState *dev, const char *name, unsigned int maxgpio,
                      Error **errp)
{
    struct gpiod_chip *chip;
    unsigned int i, n;
    int status;

    chip = gpiod_chip_open_lookup(name);
    if (!chip) {
        error_setg(errp, "Cannot open GPIO chip %s: %s", name,
                   strerror(errno));
        return;
    }

    n = gpiod_chip_num_lines(chip);
    if (n > maxgpio) {
        warn_report("Last %u GPIO line(s) will not be mapped", n - maxgpio);
        n = maxgpio;
    }

    for (i = 0; i < n; i++) {
        status = gpiodev_map_line(dev, chip, i, errp);
        if (status < 0) {
            return;
        }
    }

    info_report("Mapped %u GPIO lines", n);
}
