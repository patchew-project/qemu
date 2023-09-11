/*
 * migration yank functions
 *
 * Copyright (c) Lukas Straub <lukasstraub2@web.de>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "io/channel.h"
#include "yank_functions.h"
#include "qemu/lockable.h"
#include "qemu/yank.h"
#include "qemu-file.h"

static QemuMutex ioc_list_lock;
static QLIST_HEAD(, Yankable) yankable_ioc_list
    = QLIST_HEAD_INITIALIZER(yankable_ioc_list);

static void __attribute__((constructor)) ioc_list_lock_init(void)
{
    qemu_mutex_init(&ioc_list_lock);
}

static void yankable_ref(Yankable *yankable)
{
    assert(yankable->refcnt > 0);
    yankable->refcnt++;
    assert(yankable->refcnt < INT_MAX);
}

static void yankable_unref(Yankable *yankable)
{
    assert(yankable->refcnt > 0);
    yankable->refcnt--;
}

void migration_yank_iochannel(void *opaque)
{
    QIOChannel *ioc = QIO_CHANNEL(opaque);

    qio_channel_shutdown(ioc, QIO_CHANNEL_SHUTDOWN_BOTH, NULL);
}

/* Return whether yank is supported on this ioc */
static bool migration_ioc_yank_supported(QIOChannel *ioc)
{
    return qio_channel_has_feature(ioc, QIO_CHANNEL_FEATURE_SHUTDOWN);
}

void migration_ioc_register_yank(QIOChannel *ioc)
{
    Yankable *new, *entry;

    if (!ioc || !migration_ioc_yank_supported(ioc)) {
        return;
    }

    WITH_QEMU_LOCK_GUARD(&ioc_list_lock) {
        QLIST_FOREACH(entry, &yankable_ioc_list, next) {
            if (entry->opaque == ioc) {
                yankable_ref(entry);
                return;
            }
        }

        new = g_new0(Yankable, 1);
        new->refcnt = 1;
        new->opaque = ioc;
        object_ref(ioc);

        QLIST_INSERT_HEAD(&yankable_ioc_list, new, next);
    }

    yank_register_function(MIGRATION_YANK_INSTANCE,
                           migration_yank_iochannel,
                           ioc);
}

void migration_ioc_unregister_yank(QIOChannel *ioc)
{
    Yankable *entry, *tmp;

    if (!ioc || !migration_ioc_yank_supported(ioc)) {
        return;
    }

    WITH_QEMU_LOCK_GUARD(&ioc_list_lock) {
        QLIST_FOREACH_SAFE(entry, &yankable_ioc_list, next, tmp) {
            if (entry->opaque == ioc) {
                yankable_unref(entry);

                if (!entry->refcnt) {
                    QLIST_REMOVE(entry, next);
                    g_free(entry);
                    goto unreg;
                }
            }
        }
    }

    return;

unreg:
    yank_unregister_function(MIGRATION_YANK_INSTANCE,
                             migration_yank_iochannel,
                             ioc);
    object_unref(ioc);
}

void migration_ioc_unregister_yank_from_file(QEMUFile *file)
{
    QIOChannel *ioc = qemu_file_get_ioc(file);

    if (ioc) {
        /*
         * For migration qemufiles, we'll always reach here.  Though we'll skip
         * calls from e.g. savevm/loadvm as they don't use yank.
         */
        migration_ioc_unregister_yank(ioc);
    }
}
