/*
 * QNull Module
 *
 * Copyright (C) 2009, 2017 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#ifndef QNULL_H
#define QNULL_H

#include "qapi/qmp/qobject.h"

extern QObject qnull_;

static inline QObject *qnull(void)
{
    qobject_incref(&qnull_);
    return &qnull_;
}

bool qnull_is_equal(const QObject *x, const QObject *y);

#endif /* QNULL_H */
