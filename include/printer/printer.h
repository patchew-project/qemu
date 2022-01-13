/*
 * QEMU Printer subsystem header
 *
 * Copyright (c) 2022 ByteDance, Inc.
 *
 * Author:
 *   Ruien Zhang <zhangruien@bytedance.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_PRINTER_H
#define QEMU_PRINTER_H

#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "qapi/qapi-types-printer.h"

#define TYPE_PRINTERDEV "printerdev"

struct QEMUPrinter {
    Object  *parent_obj;

    char *model;
    Printerdev *dev;

    QLIST_ENTRY(QEMUPrinter) list;
};

OBJECT_DECLARE_TYPE(QEMUPrinter, QEMUPrinterClass, PRINTERDEV)

struct QEMUPrinterClass {
    ObjectClass parent_class;
};

void qemu_printer_new_from_opts(const char *opt);
void qemu_printer_del(QEMUPrinter *printer);
const char *qemu_printer_id(QEMUPrinter *printer);
QEMUPrinter *qemu_printer_by_id(const char *id);

#endif /* QEMU_PRINTER_H */
