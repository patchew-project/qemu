/*
 * QEMU NVM Express Virtual Dynamic Namespace Management
 *
 *
 * Copyright (c) 2022 Solidigm
 *
 * Authors:
 *  Michael Kropaczek      <michael.kropaczek@solidigm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See the
 * COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/qmp/qnum.h"
#include "qapi/qmp/qbool.h"
#include "qapi/error.h"
#include "block/qdict.h"

#include "nvme.h"

/* Here is a need for wrapping of original Qemu dictionary retrieval
 * APIs. In rare cases, when nvme cfg files were tampered with or the
 * Qemu version was upgraded and a new key is expected to be existent,
 * but is missing, it will cause segfault crash.
 * Builtin assert statements are not covering sufficiently such cases
 * and additionally a possibility of error handling is lacking */
#define NVME_KEY_CHECK_ERROR_FMT "key[%s] is expected to be existent"
int64_t qdict_get_int_chkd(const QDict *qdict, const char *key, Error **errp)
{
    QObject *qobject = qdict_get(qdict, key);
    if (qobject) {
        return qnum_get_int(qobject_to(QNum, qobject));
    }

    error_setg(errp, NVME_KEY_CHECK_ERROR_FMT, key);
    return 0;
}

QList *qdict_get_qlist_chkd(const QDict *qdict, const char *key, Error **errp)
{
    QObject *qobject = qdict_get(qdict, key);
    if (qobject) {
        return qobject_to(QList, qobject);
    }

    error_setg(errp, NVME_KEY_CHECK_ERROR_FMT, key);
    return NULL;
}
