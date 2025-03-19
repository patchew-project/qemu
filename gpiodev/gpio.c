// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * QEMU GPIO device.
 *
 * Author: 2025 Nikita Shubin <n.shubin@yadro.com>
 *
 */
#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qemu/bitmap.h"
#include "qemu/config-file.h"
#include "qemu/option.h"
#include "qemu/qemu-print.h"
#include "qemu/help_option.h"

#include "gpiodev/gpio.h"
#include "gpiodev/gpio-fe.h"

static Object *get_gpiodevs_root(void)
{
    return object_get_container("gpiodevs");
}

void qemu_gpiodev_set_info(Gpiodev *g, uint32_t nlines,
                           const char *name, const char *label)
{
    g->lines = nlines;
    g_strlcpy(g->name, name, sizeof(g->name));
    g_strlcpy(g->label, label, sizeof(g->label));

    g->mask.risen = bitmap_new(nlines);
    g->mask.fallen = bitmap_new(nlines);
    g->mask.config = bitmap_new(nlines);
}

void qemu_gpio_chip_info(Gpiodev *g, uint32_t *nlines,
                         char *name, char *label)
{
    if (!g->be) {
        g_strlcpy(name, "NULL", GPIO_MAX_NAME_SIZE);
        g_strlcpy(label, "NULL", GPIO_MAX_NAME_SIZE);
        *nlines = 0;
        return;
    }

    g_strlcpy(name, g->name, GPIO_MAX_NAME_SIZE);
    g_strlcpy(label, g->label, GPIO_MAX_NAME_SIZE);
    *nlines = g->lines;
}

void qemu_gpio_line_info(Gpiodev *g, gpio_line_info *info)
{
    GpioBackend *be = g->be;

    if (!be || !be->line_info) {
        return;
    }

    be->line_info(be->opaque, info);
}

void qemu_gpio_set_line_value(Gpiodev *g, uint32_t offset, uint8_t value)
{
    GpioBackend *be = g->be;

    if (!be || !be->set_value) {
        return;
    }

    be->set_value(be->opaque, offset, value);
}

uint8_t qemu_gpio_get_line_value(Gpiodev *g, uint32_t offset)
{
    GpioBackend *be = g->be;

    if (!be || !be->get_value) {
        return 0;
    }

    return be->get_value(be->opaque, offset);
}

void qemu_gpio_add_event_watch(Gpiodev *g, uint32_t offset, uint64_t flags)
{
    if (flags & GPIO_EVENT_RISING_EDGE) {
        set_bit(offset, g->mask.risen);
    }

    if (flags & GPIO_EVENT_FALLING_EDGE) {
        set_bit(offset, g->mask.fallen);
    }
}

void qemu_gpio_clear_event_watch(Gpiodev *g, uint32_t offset, uint64_t flags)
{
    if (flags & GPIO_EVENT_RISING_EDGE) {
        clear_bit(offset, g->mask.risen);
    }

    if (flags & GPIO_EVENT_FALLING_EDGE) {
        clear_bit(offset, g->mask.fallen);
    }
}

void qemu_gpio_add_config_watch(Gpiodev *g, uint32_t offset)
{
    set_bit(offset, g->mask.config);
}

void qemu_gpio_clear_config_watch(Gpiodev *g, uint32_t offset)
{
    clear_bit(offset, g->mask.config);
}

void qemu_gpio_clear_watches(Gpiodev *g)
{
    bitmap_zero(g->mask.risen, g->lines);
    bitmap_zero(g->mask.fallen, g->lines);
    bitmap_zero(g->mask.config, g->lines);
}

void qemu_gpio_line_event(Gpiodev *g, uint32_t offset,
                          QEMUGpioLineEvent event)
{
    GpiodevClass *gc = GPIODEV_GET_CLASS(g);
    bool notify = false;

    if (!gc->line_event) {
        return;
    }

    if (event & GPIO_EVENT_RISING_EDGE) {
        if (test_bit(offset, g->mask.risen)) {
            notify = true;
        }
    }

    if (event & GPIO_EVENT_FALLING_EDGE) {
        if (test_bit(offset, g->mask.fallen)) {
            notify = true;
        }
    }

    if (notify) {
        gc->line_event(g, offset, event);
    }
}

void qemu_gpio_config_event(Gpiodev *g, uint32_t offset,
                            QEMUGpioConfigEvent event)
{
    GpiodevClass *gc = GPIODEV_GET_CLASS(g);

    if (!gc->config_event) {
        return;
    }

    if (test_bit(offset, g->mask.config)) {
        gc->config_event(g, offset, event);
    }
}

static void qemu_gpio_finalize(Object *obj)
{
    Gpiodev *d = GPIODEV(obj);

    g_free(d->mask.risen);
    g_free(d->mask.fallen);
    g_free(d->mask.config);
}

static const TypeInfo gpiodev_types_info[] = {
    {
        .name = TYPE_GPIODEV,
        .parent = TYPE_OBJECT,
        .instance_size = sizeof(Gpiodev),
        .instance_finalize = qemu_gpio_finalize,
        .abstract = true,
        .class_size = sizeof(GpiodevClass),
    },
};

DEFINE_TYPES(gpiodev_types_info);

static void qemu_gpio_open(Gpiodev *gpio, GpiodevBackend *backend,
                           Error **errp)
{
    GpiodevClass *gc = GPIODEV_GET_CLASS(gpio);

    if (gc->open) {
        gc->open(gpio, backend, errp);
    }
}

static Gpiodev *gpiodev_new(const char *id, const char *typename,
                            GpiodevBackend *backend,
                            GMainContext *gcontext,
                            Error **errp)
{
    Object *obj;
    Gpiodev *gpio = NULL;
    Error *local_err = NULL;

    assert(g_str_has_prefix(typename, "gpiodev-"));
    assert(id);

    obj = object_new(typename);
    gpio = GPIODEV(obj);
    gpio->gcontext = gcontext;

    qemu_gpio_open(gpio, backend, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        object_unref(obj);
        return NULL;
    }

    return gpio;
}

static Gpiodev *qemu_gpiodev_new(const char *id, const char *typename,
                                 GpiodevBackend *backend,
                                 GMainContext *gcontext,
                                 Error **errp)
{
    Gpiodev *gpio;

    gpio = gpiodev_new(id, typename, backend, gcontext, errp);
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

static const GpiodevClass *gpio_get_class(const char *driver, Error **errp)
{
    ObjectClass *oc;
    char *typename = g_strdup_printf("gpiodev-%s", driver);

    oc = module_object_class_by_name(typename);
    g_free(typename);

    if (!object_class_dynamic_cast(oc, TYPE_GPIODEV)) {
        error_setg(errp, "'%s' is not a valid gpio driver name", driver);
        return NULL;
    }

    if (object_class_is_abstract(oc)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "driver",
                   "a non-abstract device type");
        return NULL;
    }

    return GPIODEV_CLASS(oc);
}

static GpiodevBackend *qemu_gpio_parse_opts(QemuOpts *opts, Error **errp)
{
    Error *local_err = NULL;
    const GpiodevClass *gc;
    GpiodevBackend *backend = NULL;
    const char *name = qemu_opt_get(opts, "backend");

    if (name == NULL) {
        error_setg(errp, "gpiodev: \"%s\" missing backend",
                   qemu_opts_id(opts));
        return NULL;
    }

    gc = gpio_get_class(name, errp);
    if (gc == NULL) {
        return NULL;
    }

    backend = g_new0(GpiodevBackend, 1);
    if (gc->parse) {
        gc->parse(opts, backend, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            qapi_free_GpiodevBackend(backend);
            return NULL;
        }
    }

    return backend;
}

Gpiodev *qemu_gpiodev_add(QemuOpts *opts, GMainContext *context,
                          Error **errp)
{
    const char *id = qemu_opts_id(opts);
    const char *name = qemu_opt_get(opts, "backend");
    const GpiodevClass *gc;
    GpiodevBackend *backend = NULL;
    Gpiodev *gpio = NULL;

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

    backend = qemu_gpio_parse_opts(opts, errp);
    if (backend == NULL) {
        return NULL;
    }

    gc = gpio_get_class(name, errp);
    if (gc == NULL) {
        goto out;
    }

    gpio = qemu_gpiodev_new(id, object_class_get_name(OBJECT_CLASS(gc)),
                            backend, context, errp);

out:
    qapi_free_GpiodevBackend(backend);
    return gpio;
}

static QemuOptsList qemu_gpiodev_opts = {
    .name = "gpiodev",
    .implied_opt_name = "backend",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_gpiodev_opts.head),
    .desc = {
        {
            .name = "backend",
            .type = QEMU_OPT_STRING,
        }, {
            .name = "chardev",
            .type = QEMU_OPT_STRING,
            .help = "Chardev id (for gpiodev-chardev)",
        }, {
            .name = "devname",
            .type = QEMU_OPT_STRING,
            .help = "Device name (for gpiodev-guse)",
        },
        { /* end of list */ }
    },
};

static void gpiodev_register_config(void)
{
    qemu_add_opts(&qemu_gpiodev_opts);
}

opts_init(gpiodev_register_config);
