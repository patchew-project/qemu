// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * QEMU GPIO device frontend.
 *
 * Author: 2025 Nikita Shubin <n.shubin@yadro.com>
 *
 */
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"

#include "gpiodev/gpio-fe.h"

bool qemu_gpio_fe_init(GpioBackend *b, Gpiodev *s, uint32_t nlines,
                       const char *name, const char *label,
                       Error **errp)
{
    if (s->be) {
        goto unavailable;
    } else {
        s->be = b;
    }

    qemu_gpiodev_set_info(s, nlines, name, label);
    b->gpio = s;

    return true;

unavailable:
    error_setg(errp, "chardev '%s' is already in use", s->label);
    return false;
}

void qemu_gpio_fe_set_handlers(GpioBackend *b,
                               LineInfoHandler *line_info,
                               LineGetValueHandler *get_value,
                               LineSetValueHandler *set_value,
                               void *opaque)
{
    Gpiodev *s;

    s = b->gpio;
    if (!s) {
        return;
    }

    b->line_info = line_info;
    b->get_value = get_value;
    b->set_value = set_value;
    b->opaque = opaque;
}

bool qemu_gpio_fe_line_event(GpioBackend *b, uint32_t offset,
                             QEMUGpioLineEvent event)
{
    Gpiodev *gpio = b->gpio;

    if (!gpio) {
        return false;
    }

    qemu_gpio_line_event(gpio, offset, event);

    return true;
}

bool qemu_gpio_fe_config_event(GpioBackend *b, uint32_t offset,
                               QEMUGpioConfigEvent event)
{
    Gpiodev *gpio = b->gpio;

    if (!gpio) {
        return false;
    }

    qemu_gpio_config_event(gpio, offset, event);

    return true;
}

void qemu_gpio_fe_deinit(GpioBackend *b, bool del)
{
    assert(b);

    if (b->gpio) {
        qemu_gpio_fe_set_handlers(b, NULL, NULL, NULL, NULL);
        if (b->gpio->be == b) {
            b->gpio->be = NULL;
        }

        if (del) {
            Object *obj = OBJECT(b->gpio);
            if (obj->parent) {
                object_unparent(obj);
            } else {
                object_unref(obj);
            }
        }

        b->gpio = NULL;
    }
}
