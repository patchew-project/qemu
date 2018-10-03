/*
 * QEMU GPIO Backend
 *
 * Copyright (C) 2018 Glider bvba
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

#include "sysemu/gpiodev.h"

#include "hw/irq.h"
#include "hw/qdev-core.h"

DeviceState *the_pl061_dev;

static void gpio_irq_handler(void *opaque, int n, int level)
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

static int gpio_connect_line(unsigned int vgpio, struct gpiod_chip *chip,
                             unsigned int gpio)
{
    const char *name = gpiod_chip_name(chip);
    const char *label = gpiod_chip_label(chip);
    struct gpiod_line *line;
    qemu_irq irq;
    int status;

    if (!the_pl061_dev) {
        error_report("PL061 GPIO controller not available");
        return -1;
    }

    line = gpiod_chip_get_line(chip, gpio);
    if (!line) {
        error_report("%s/%s: Cannot obtain GPIO line %u: %s", name, label,
                     gpio, strerror(errno));
        return -1;
    }

    status = gpiod_line_request_output(line, "qemu", 0);
    if (status < 0) {
        error_report("%s/%s: Cannot request GPIO line %u for output: %s", name,
                     label, gpio, strerror(errno));
        return -1;
    }

    irq = qemu_allocate_irq(gpio_irq_handler, line, 0);
    qdev_connect_gpio_out(the_pl061_dev, vgpio, irq);

    info_report("%s/%s: Connected PL061 GPIO %u to GPIO line %u", name, label,
                vgpio, gpio);
    return 0;
}

static int gpio_count_gpios(const char *opt)
{
    unsigned int len = 0;
    unsigned int n = 0;

    do {
        switch (*opt) {
        case '0' ... '9':
            len++;
            break;

        case ':':
        case '\0':
            if (!len) {
                return -1;
            }

            n++;
            len = 0;
            break;

        default:
            return -1;
        }
    } while (*opt++);

    return n;
}

int qemu_gpiodev_add(QemuOpts *opts)
{
    const char *name = qemu_opt_get(opts, "name");
    const char *vgpios = qemu_opt_get(opts, "vgpios");
    const char *gpios = qemu_opt_get(opts, "gpios");
    unsigned int vgpio, gpio;
    struct gpiod_chip *chip;
    int n1, n2, i, status;

    if (!name || !vgpios || !gpios) {
        error_report("Missing parameters");
        return -1;
    }

    n1 = gpio_count_gpios(vgpios);
    if (n1 < 0) {
        error_report("Invalid vgpios parameter");
        return n1;
    }

    n2 = gpio_count_gpios(gpios);
    if (n2 < 0) {
        error_report("Invalid gpios parameter");
        return n2;
    }

    if (n1 != n2) {
        error_report("Number of vgpios and gpios do not match");
        return -1;
    }

    chip = gpiod_chip_open_lookup(name);
    if (!chip) {
        error_report("Cannot open GPIO chip %s: %s", name, strerror(errno));
        return -1;
    }

    for (i = 0; i < n1; i++, vgpios++, gpios++) {
        qemu_strtoui(vgpios, &vgpios, 10, &vgpio);
        qemu_strtoui(gpios, &gpios, 10, &gpio);

        status = gpio_connect_line(vgpio, chip, gpio);
        if (status) {
            return status;
        }
    }

    return 0;
}

static QemuOptsList qemu_gpiodev_opts = {
    .name = "gpiodev",
    .implied_opt_name = "name",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_gpiodev_opts.head),
    .desc = {
        {
            .name = "name",
            .type = QEMU_OPT_STRING,
            .help = "Sets the GPIO chip specifier",
        }, {
            .name = "vgpios",
            .type = QEMU_OPT_STRING,
            .help = "Sets the list of virtual GPIO offsets",
        }, {
            .name = "gpios",
            .type = QEMU_OPT_STRING,
            .help = "Sets the list of physical GPIO offsets",
        },
        { /* end of list */ }
    },
};

static void gpiodev_register_config(void)
{
    qemu_add_opts(&qemu_gpiodev_opts);
}

opts_init(gpiodev_register_config);
