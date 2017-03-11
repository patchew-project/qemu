/*
 * QUInt Module
 *
 * Copyright (C) 2017 Red Hat Inc.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/qmp/quint.h"
#include "qapi/qmp/qobject.h"
#include "qemu-common.h"

/**
 * quint_from_uint(): Create a new QUInt from an uint64_t
 *
 * Return strong reference.
 */
QUInt *quint_from_uint(uint64_t value)
{
    QUInt *qi;

    qi = g_malloc(sizeof(*qi));
    qobject_init(QOBJECT(qi), QTYPE_QUINT);
    qi->value = value;

    return qi;
}

/**
 * quint_get_int(): Get the stored integer
 */
uint64_t quint_get_uint(const QUInt *qi)
{
    return qi->value;
}

/**
 * qobject_to_quint(): Convert a QObject into a QUInt
 */
QUInt *qobject_to_quint(const QObject *obj)
{
    if (!obj || qobject_type(obj) != QTYPE_QUINT) {
        return NULL;
    }
    return container_of(obj, QUInt, base);
}

/**
 * quint_destroy_obj(): Free all memory allocated by a
 * QUInt object
 */
void quint_destroy_obj(QObject *obj)
{
    assert(obj != NULL);
    g_free(qobject_to_quint(obj));
}
