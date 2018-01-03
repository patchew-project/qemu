/*
 * QTest SD/MMC Bus QMP driver
 *
 * Copyright (c) 2017 Philippe Mathieu-Daudé
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"

#include "libqos/sdbus.h"
#include "libqtest.h"

typedef struct QMPSDBus {
    SDBusAdapter parent;

    const char *qom_path;
} QMPSDBus;


static const char *qmp_sdbus_getpath(const char *blkname)
{
    QDict *response, *minfo;
    QList *list;
    const QListEntry *le;
    QString *qstr;
    const char *mname;
    QObject *qobj;

    response = qmp("{ 'execute': 'query-block' }");
    g_assert_nonnull(response);
    list = qdict_get_qlist(response, "return");
    g_assert_nonnull(list);

    QLIST_FOREACH_ENTRY(list, le) {
        QDict *response2;

        minfo = qobject_to_qdict(qlist_entry_obj(le));
        g_assert(minfo);
        qobj = qdict_get(minfo, "qdev");
        if (!qobj) {
            continue;
        }
        qstr = qobject_to_qstring(qobj);
        g_assert(qstr);
        mname = qstring_get_str(qstr);

        response2 = qmp("{ 'execute': 'qom-get',"
                        "  'arguments': { 'path': %s,"
                        "   'property': \"parent_bus\"}"
                        "}", mname);
        g_assert(response2);
        g_assert(qdict_haskey(response2, "return"));
        qobj = qdict_get(response2, "return");
        qstr = qobject_to_qstring(qobj);
        g_assert(qstr);
        mname = qstring_get_str(qstr);

        return mname;
    }
    return NULL;
}

static ssize_t qmp_mmc_do_cmd(SDBusAdapter *adapter, enum NCmd cmd, uint32_t arg,
                              uint8_t **response)
{
    QMPSDBus *s = (QMPSDBus *)adapter;
    QDict *response1;
    QObject *qobj;

    response1 = qmp("{ 'execute': 'x-debug-sdbus-command',"
                    "  'arguments': { 'qom-path': %s,"
                    "                 'command': %u, 'arg': %u}"
                    "}",
                    s->qom_path, cmd, arg);
    g_assert(qdict_haskey(response1, "return"));
    qobj = qdict_get(response1, "return");
    //QDECREF(response);

    if (!qobj) {
        return -1;
    }

    {
        QString *qstr;
        const gchar *mname;
        guchar *uc;
        gsize out_len;
        QDict *response2 = qobject_to_qdict(qobj);

        if (!qdict_haskey(response2, "base64")) {
            return 0;
        }
        qobj = qdict_get(response2, "base64");
        qstr = qobject_to_qstring(qobj);
        if (!qstr) {
            puts("!qstr");
            return 0;
        }
        mname = qstring_get_str(qstr);

        uc = g_base64_decode(mname, &out_len);
        if (response) {
            *response = uc;
        } else {
            g_free(uc);
        }
        return out_len;

    }

    return 0;
}

SDBusAdapter *qmp_sdbus_create(const char *bus_name)
{
    QMPSDBus *s;
    SDBusAdapter *mmc;

    s = g_new(QMPSDBus, 1);
    s->qom_path = qmp_sdbus_getpath(bus_name);
    g_assert_nonnull(s->qom_path);

    mmc = (SDBusAdapter *)s;
    mmc->do_command = qmp_mmc_do_cmd;

    return mmc;
}
