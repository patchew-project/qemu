// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * QEMU GPIO device.
 *
 * Author: 2025 Nikita Shubin <n.shubin@yadro.com>
 *
 */
#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qemu/config-file.h"
#include "qemu/option.h"
#include "qemu/qemu-print.h"
#include "qemu/help_option.h"

#include "gpiodev/gpio.h"

static Object *get_gpiodevs_root(void)
{
    return object_get_container("gpiodevs");
}

static const TypeInfo gpiodev_types_info[] = {
    {
        .name = TYPE_GPIODEV,
        .parent = TYPE_OBJECT,
        .instance_size = sizeof(Gpiodev),
        .abstract = true,
    },
};

DEFINE_TYPES(gpiodev_types_info);

static Gpiodev *gpiodev_new(const char *id,
                            GMainContext *gcontext,
                            Error **errp)
{
    Object *obj;
    Gpiodev *gpio = NULL;

    assert(id);

    obj = object_new(TYPE_GPIODEV);
    gpio = GPIODEV(obj);
    gpio->gcontext = gcontext;

    return gpio;
}

static Gpiodev *qemu_gpiodev_new(const char *id,
                                 GMainContext *gcontext,
                                 Error **errp)
{
    Gpiodev *gpio;

    gpio = gpiodev_new(id, gcontext, errp);
    if (!gpio) {
        return NULL;
    }

    if (!object_property_try_add_child(get_gpiodevs_root(), id, OBJECT(gpio),
                                       errp)) {
        object_unref(OBJECT(gpio));
        return NULL;
    }

    object_unref(OBJECT(gpio));

    return gpio;
}

typedef struct GpiodevClassFE {
    void (*fn)(const char *name, void *opaque);
    void *opaque;
} GpiodevClassFE;

static void
gpiodev_class_foreach(ObjectClass *klass, void *opaque)
{
    GpiodevClassFE *fe = opaque;

    assert(g_str_has_prefix(object_class_get_name(klass), "gpiodev-"));
    fe->fn(object_class_get_name(klass) + 8, fe->opaque);
}

static void
gpiodev_name_foreach(void (*fn)(const char *name, void *opaque),
                     void *opaque)
{
    GpiodevClassFE fe = { .fn = fn, .opaque = opaque };

    object_class_foreach(gpiodev_class_foreach, TYPE_GPIODEV, false, &fe);
}

static void
help_string_append(const char *name, void *opaque)
{
    GString *str = opaque;

    g_string_append_printf(str, "\n  %s", name);
}

Gpiodev *qemu_gpiodev_add(QemuOpts *opts, GMainContext *context,
                          Error **errp)
{
    const char *id = qemu_opts_id(opts);
    const char *name = qemu_opt_get(opts, "backend");

    if (name && is_help_option(name)) {
        GString *str = g_string_new("");

        gpiodev_name_foreach(help_string_append, str);

        qemu_printf("Available chardev backend types: %s\n", str->str);
        g_string_free(str, true);
        return NULL;
    }

    if (id == NULL) {
        error_setg(errp, "gpiodev: no id specified");
        return NULL;
    }

    return qemu_gpiodev_new(id, context, errp);
}

static QemuOptsList qemu_gpiodev_opts = {
    .name = "gpiodev",
    .implied_opt_name = "backend",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_gpiodev_opts.head),
    .desc = {
        {
            .name = "backend",
            .type = QEMU_OPT_STRING,
        },
        { /* end of list */ }
    },
};

static void gpiodev_register_config(void)
{
    qemu_add_opts(&qemu_gpiodev_opts);
}

opts_init(gpiodev_register_config);
