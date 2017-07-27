/*
 * QObject
 *
 * Copyright (C) 2015 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1
 * or later.  See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qapi/qmp/types.h"

static void (*qdestroy[QTYPE__MAX])(QObject *) = {
    [QTYPE_NONE] = NULL,               /* No such object exists */
    [QTYPE_QNULL] = NULL,              /* qnull_ is indestructible */
    [QTYPE_QNUM] = qnum_destroy_obj,
    [QTYPE_QSTRING] = qstring_destroy_obj,
    [QTYPE_QDICT] = qdict_destroy_obj,
    [QTYPE_QLIST] = qlist_destroy_obj,
    [QTYPE_QBOOL] = qbool_destroy_obj,
};

void qobject_destroy(QObject *obj)
{
    assert(!obj->refcnt);
    assert(QTYPE_QNULL < obj->type && obj->type < QTYPE__MAX);
    qdestroy[obj->type](obj);
}

char *qobject_to_string_indent(QObject *obj, int indent)
{
    switch (qobject_type(obj)) {
    case QTYPE_QNULL:
        return g_strdup("null");
    case QTYPE_QNUM:
        return qnum_to_string(qobject_to_qnum(obj));
    case QTYPE_QSTRING:
        return g_strdup(qstring_get_str(qobject_to_qstring(obj)));
    case QTYPE_QDICT:
        return qdict_to_string(qobject_to_qdict(obj), indent);
    case QTYPE_QLIST:
        return qlist_to_string(qobject_to_qlist(obj), indent);
    case QTYPE_QBOOL:
        return g_strdup(qbool_get_bool(qobject_to_qbool(obj)) ?
                        "true" : "false");
    default:
        abort();
    }
}
