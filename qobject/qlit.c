/*
 * QLit literal qobject
 *
 * Copyright (C) 2017 Red Hat Inc.
 *
 * Authors:
 *  Marc-André Lureau <marcandre.lureau@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "qapi/qmp/qlit.h"
#include "qapi/qmp/types.h"

QObject *qobject_from_qlit(const QLitObject *qlit)
{
    int i;

    switch (qlit->type) {
    case QTYPE_QNULL:
        return QOBJECT(qnull());
    case QTYPE_QNUM:
        return QOBJECT(qnum_from_int(qlit->value.qnum));
    case QTYPE_QSTRING:
        return QOBJECT(qstring_from_str(qlit->value.qstr));
    case QTYPE_QDICT: {
        QDict *qdict = qdict_new();
        for (i = 0; qlit->value.qdict[i].value.type != QTYPE_NONE; i++) {
            QLitDictEntry *e = &qlit->value.qdict[i];

            qdict_put_obj(qdict, e->key, qobject_from_qlit(&e->value));
        }
        return QOBJECT(qdict);
    }
    case QTYPE_QLIST: {
        QList *qlist = qlist_new();

        for (i = 0; qlit->value.qlist[i].type != QTYPE_NONE; i++) {
            qlist_append_obj(qlist, qobject_from_qlit(&qlit->value.qlist[i]));
        }
        return QOBJECT(qlist);
    }
    case QTYPE_QBOOL:
        return QOBJECT(qbool_from_bool(qlit->value.qbool));
    case QTYPE_NONE:
        assert(0);
    }

    return NULL;
}
