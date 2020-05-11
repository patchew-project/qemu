/*
 * QEMU yank feature
 *
 * Copyright (c) Lukas Straub <lukasstraub2@web.de>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/thread.h"
#include "qemu/queue.h"
#include "yank.h"

struct YankFuncAndParam {
    YankFn *func;
    void *opaque;
    QLIST_ENTRY(YankFuncAndParam) next;
};

static QemuMutex lock;
static QLIST_HEAD(qlisthead, YankFuncAndParam) head
    = QLIST_HEAD_INITIALIZER(head);

void yank_register_function(YankFn *func, void *opaque)
{
    struct YankFuncAndParam *tmp = g_malloc(sizeof(struct YankFuncAndParam));
    tmp->func = func;
    tmp->opaque = opaque;

    qemu_mutex_lock(&lock);
    QLIST_INSERT_HEAD(&head, tmp, next);
    qemu_mutex_unlock(&lock);
}

void yank_unregister_function(YankFn *func, void *opaque)
{
    qemu_mutex_lock(&lock);

    struct YankFuncAndParam *tmp;
    QLIST_FOREACH(tmp, &head, next) {
        if (tmp->func == func && tmp->opaque == opaque) {
            QLIST_REMOVE(tmp, next);
            g_free(tmp);
            qemu_mutex_unlock(&lock);
            return;
        }
    }

    abort();
}

void yank_call_functions(void)
{
    qemu_mutex_lock(&lock);

    struct YankFuncAndParam *tmp;
    QLIST_FOREACH(tmp, &head, next) {
        tmp->func(tmp->opaque);
    }

    qemu_mutex_unlock(&lock);
}

void qmp_yank(Error **errp)
{
    yank_call_functions();
}

void yank_init(void)
{
    qemu_mutex_init(&lock);
    QLIST_INIT(&head);
}
