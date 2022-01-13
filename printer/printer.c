/*
 * QEMU Printer subsystem
 *
 * Copyright (c) 2022 ByteDance, Inc.
 *
 * Author:
 *   Ruien Zhang <zhangruien@bytedance.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/help_option.h"
#include "qemu/iov.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/qemu-print.h"
#include "qom/object.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qapi-visit-printer.h"
#include "printer/printer.h"
#include "trace.h"

static QLIST_HEAD(, QEMUPrinter) qemu_printers;

const char *qemu_printer_id(QEMUPrinter *printer)
{
    if (printer->dev && printer->dev->id) {
        return printer->dev->id;
    }

    return "";
}

QEMUPrinter *qemu_printer_by_id(const char *id)
{
    QEMUPrinter *printer;

    if (!id) {
        return NULL;
    }

    QLIST_FOREACH(printer, &qemu_printers, list) {
        if (!strcmp(qemu_printer_id(printer), id)) {
            return printer;
        }
    }

    return NULL;
}

static const QEMUPrinterClass *printer_get_class(const char *typename,
                                               Error **errp)
{
    ObjectClass *oc;

    oc = module_object_class_by_name(typename);

    if (!object_class_dynamic_cast(oc, TYPE_PRINTERDEV)) {
        error_setg(errp, "%s: missing %s implementation",
                   TYPE_PRINTERDEV, typename);
        return NULL;
    }

    if (object_class_is_abstract(oc)) {
        error_setg(errp, "%s: %s is abstract type", TYPE_PRINTERDEV, typename);
        return NULL;
    }

    return PRINTERDEV_CLASS(oc);
}

static QEMUPrinter *qemu_printer_new(Printerdev *dev, Error **errp)
{
    Object *obj;
    QEMUPrinter *printer = NULL;
    g_autofree char *typename = NULL;
    const char *driver = PrinterdevDriver_str(dev->driver);

    typename = g_strdup_printf("%s-%s", TYPE_PRINTERDEV, driver);
    if (!printer_get_class(typename, errp)) {
        return NULL;
    }

    obj = object_new(typename);
    if (!obj) {
        return NULL;
    }

    printer = PRINTERDEV(obj);
    printer->dev = dev;

    QLIST_INSERT_HEAD(&qemu_printers, printer, list);
    trace_qemu_printer_new(qemu_printer_id(printer), typename);

    return printer;
}

typedef struct PrinterdevClassFE {
    void (*fn)(const char *name, void *opaque);
    void *opaque;
} PrinterdevClassFE;

static void printerdev_class_foreach(ObjectClass *klass, void *opaque)
{
    PrinterdevClassFE *fe = opaque;

    assert(g_str_has_prefix(object_class_get_name(klass), TYPE_PRINTERDEV"-"));
    fe->fn(object_class_get_name(klass) + 11, fe->opaque);
}

static void printerdev_name_foreach(void (*fn)(const char *name, void *opaque),
                                   void *opaque)
{
    PrinterdevClassFE fe = { .fn = fn, .opaque = opaque };

    object_class_foreach(printerdev_class_foreach, TYPE_PRINTERDEV, false, &fe);
}

static void help_string_append(const char *name, void *opaque)
{
    GString *str = opaque;

    g_string_append_printf(str, "\n  %s", name);
}

void qemu_printer_new_from_opts(const char *opt)
{
    Printerdev *dev;

    if (opt && is_help_option(opt)) {
        GString *str = g_string_new("");

        printerdev_name_foreach(help_string_append, str);

        qemu_printf("Available printerdev backend types: %s\n", str->str);
        g_string_free(str, true);
        return;
    }

    Visitor *v = qobject_input_visitor_new_str(opt, "driver", &error_fatal);
    visit_type_Printerdev(v, NULL, &dev, &error_fatal);
    visit_free(v);

    if (qemu_printer_by_id(dev->id)) {
        error_setg(&error_fatal, "%s: id %s already existed",
                   TYPE_PRINTERDEV, dev->id);
    }

    if (!qemu_printer_new(dev, &error_fatal)) {
        qapi_free_Printerdev(dev);
    }
}

void qemu_printer_del(QEMUPrinter *printer)
{
    trace_qemu_printer_del(qemu_printer_id(printer));

    QLIST_REMOVE(printer, list);
    qapi_free_Printerdev(printer->dev);
    object_unref(printer);
}


static void printer_init(Object *obj)
{
}

static void printer_finalize(Object *obj)
{
}

static const TypeInfo printer_type_info = {
    .name = TYPE_PRINTERDEV,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(QEMUPrinter),
    .instance_init = printer_init,
    .instance_finalize = printer_finalize,
    .abstract = true,
    .class_size = sizeof(QEMUPrinterClass),
};

static void register_types(void)
{
    type_register_static(&printer_type_info);
}

type_init(register_types);
