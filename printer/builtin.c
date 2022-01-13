/*
 * QEMU Builtin printer backend
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
#include "qemu/module.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qom/object.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qapi/qapi-visit-printer.h"
#include "printer/printer.h"
#include "trace.h"

#define TYPE_PRINTER_BUILTIN TYPE_PRINTERDEV"-builtin"

typedef struct PrinterBuiltin {
    QEMUPrinter parent;

    void *opaque; /* used by driver itself */
} PrinterBuiltin;

DECLARE_INSTANCE_CHECKER(PrinterBuiltin, PRINTER_BUILTIN_DEV,
                         TYPE_PRINTER_BUILTIN)

static void printer_builtin_init(Object *obj)
{
}

static void printer_builtin_finalize(Object *obj)
{
}

static void printer_builtin_class_init(ObjectClass *oc, void *data)
{
}

static const TypeInfo printer_builtin_type_info = {
    .name = TYPE_PRINTER_BUILTIN,
    .parent = TYPE_PRINTERDEV,
    .instance_size = sizeof(PrinterBuiltin),
    .instance_init = printer_builtin_init,
    .instance_finalize = printer_builtin_finalize,
    .class_init = printer_builtin_class_init,
};

static void register_types(void)
{
    type_register_static(&printer_builtin_type_info);
}

type_init(register_types);
