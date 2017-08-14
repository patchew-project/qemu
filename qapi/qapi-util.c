/*
 * QAPI util functions
 *
 * Authors:
 *  Hu Tao       <hutao@cn.fujitsu.com>
 *  Peter Lieven <pl@kamp.de>
 * 
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qmp/qnum.h"
#include "qapi/qmp/qbool.h"
#include "qapi/util.h"

int qapi_enum_parse(const char * const lookup[], const char *buf,
                    int max, int def, Error **errp)
{
    int i;

    if (!buf) {
        return def;
    }

    for (i = 0; i < max; i++) {
        if (!strcmp(buf, lookup[i])) {
            return i;
        }
    }

    error_setg(errp, "invalid parameter value: %s", buf);
    return def;
}

/*
 * Parse a valid QAPI name from @str.
 * A valid name consists of letters, digits, hyphen and underscore.
 * It may be prefixed by __RFQDN_ (downstream extension), where RFQDN
 * may contain only letters, digits, hyphen and period.
 * The special exception for enumeration names is not implemented.
 * See docs/devel/qapi-code-gen.txt for more on QAPI naming rules.
 * Keep this consistent with scripts/qapi.py!
 * If @complete, the parse fails unless it consumes @str completely.
 * Return its length on success, -1 on failure.
 */
int parse_qapi_name(const char *str, bool complete)
{
    const char *p = str;

    if (*p == '_') {            /* Downstream __RFQDN_ */
        p++;
        if (*p != '_') {
            return -1;
        }
        while (*++p) {
            if (!qemu_isalnum(*p) && *p != '-' && *p != '.') {
                break;
            }
        }

        if (*p != '_') {
            return -1;
        }
        p++;
    }

    if (!qemu_isalpha(*p)) {
        return -1;
    }
    while (*++p) {
        if (!qemu_isalnum(*p) && *p != '-' && *p != '_') {
            break;
        }
    }

    if (complete && *p) {
        return -1;
    }
    return p - str;
}

static int qnum_compare(QNum *a, QNum *b)
{
    int64_t ia, ib;
    bool va = qnum_get_try_int(a, &ia);
    bool vb = qnum_get_try_int(b, &ib);

    if (va && vb) {
        return (ia < ib) ? -1 : (ia > ib) ? 1 : 0;
    }

    /*TODO: uint, double */
    return -1;
}

static int qlist_compare(QList *a, QList *b)
{
    const QListEntry *ea, *eb;

    for (ea = qlist_first(a), eb = qlist_first(b);
         ea && eb;
         ea = qlist_next(ea), eb = qlist_next(eb)) {
        QObject *va = qlist_entry_obj(ea);
        QObject *vb = qlist_entry_obj(eb);
        int c = qobject_compare(va, vb);
        if (c) {
            return c;
        }
    }

    if (eb) {
        return -1;
    } else if (ea) {
        return 1;
    } else {
        return 0;
    }
}

int qobject_compare(QObject *a, QObject *b)
{
    QType ta = qobject_type(a);
    QType tb = qobject_type(b);

    if (ta != tb) {
        return -1;
    }

    switch (ta) {
    case QTYPE_QNULL:
        return true;
    case QTYPE_QNUM:
        return qnum_compare(qobject_to_qnum(a), qobject_to_qnum(b));
    case QTYPE_QSTRING:
        return strcmp(qstring_get_str(qobject_to_qstring(a)), qstring_get_str(qobject_to_qstring(b)));
    case QTYPE_QBOOL:
        return (int)qbool_get_bool(qobject_to_qbool(a)) - (int)qbool_get_bool(qobject_to_qbool(b));
    case QTYPE_QLIST:
        return qlist_compare(qobject_to_qlist(a), qobject_to_qlist(b));
    default:
        return -1;
    }
}
