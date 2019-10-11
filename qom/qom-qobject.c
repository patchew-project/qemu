/*
 * QEMU Object Model - QObject wrappers
 *
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qom/object.h"
#include "qom/qom-qobject.h"
#include "qapi/visitor.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qobject-output-visitor.h"

void object_property_set_qobject(Object *obj, QObject *value,
                                 const char *name, Error **errp)
{
    Visitor *v;

    v = qobject_input_visitor_new(value);
    object_property_set(obj, v, name, errp);
    visit_free(v);
}

QObject *object_property_get_qobject(Object *obj, const char *name,
                                     Error **errp)
{
    ERRP_AUTO_PROPAGATE();
    QObject *ret = NULL;
    Visitor *v;

    v = qobject_output_visitor_new(&ret);
    object_property_get(obj, v, name, errp);
    if (!*errp) {
        visit_complete(v, &ret);
    }
    visit_free(v);
    return ret;
}
