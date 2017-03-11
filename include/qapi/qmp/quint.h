/*
 * QUInt Module
 *
 * Copyright (C) 2017 Red Hat Inc.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#ifndef QUINT_H
#define QUINT_H

#include "qapi/qmp/qobject.h"

typedef struct QUInt {
    QObject base;
    uint64_t value;
} QUInt;

QUInt *quint_from_uint(uint64_t value);
uint64_t quint_get_uint(const QUInt *qi);
QUInt *qobject_to_quint(const QObject *obj);
void quint_destroy_obj(QObject *obj);

#endif /* QUINT_H */
