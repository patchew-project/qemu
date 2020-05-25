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
#include "qapi/qapi-commands-misc.h"
#include "io/channel.h"
#include "yank.h"

struct YankFuncAndParam {
    YankFn *func;
    void *opaque;
    QLIST_ENTRY(YankFuncAndParam) next;
};

struct YankInstance {
    char *name;
    QLIST_HEAD(, YankFuncAndParam) yankfns;
    QLIST_ENTRY(YankInstance) next;
};

static QemuMutex lock;
static QLIST_HEAD(yankinst_list, YankInstance) head
    = QLIST_HEAD_INITIALIZER(head);

static struct YankInstance *yank_find_instance(char *name)
{
    struct YankInstance *tmp, *instance;
    instance = NULL;
    QLIST_FOREACH(tmp, &head, next) {
        if (!strcmp(tmp->name, name)) {
            instance = tmp;
        }
    }
    return instance;
}

void yank_register_instance(char *instance_name)
{
    struct YankInstance *instance;

    qemu_mutex_lock(&lock);
    assert(!yank_find_instance(instance_name));

    instance = g_slice_new(struct YankInstance);
    instance->name = g_strdup(instance_name);
    QLIST_INIT(&instance->yankfns);
    QLIST_INSERT_HEAD(&head, instance, next);

    qemu_mutex_unlock(&lock);
}

void yank_unregister_instance(char *instance_name)
{
    struct YankInstance *instance;

    qemu_mutex_lock(&lock);
    instance = yank_find_instance(instance_name);
    assert(instance);

    assert(QLIST_EMPTY(&instance->yankfns));
    QLIST_REMOVE(instance, next);
    g_free(instance->name);
    g_slice_free(struct YankInstance, instance);

    qemu_mutex_unlock(&lock);
}

void yank_register_function(char *instance_name, YankFn *func, void *opaque)
{
    struct YankInstance *instance;
    struct YankFuncAndParam *entry;

    qemu_mutex_lock(&lock);
    instance = yank_find_instance(instance_name);
    assert(instance);

    entry = g_slice_new(struct YankFuncAndParam);
    entry->func = func;
    entry->opaque = opaque;

    QLIST_INSERT_HEAD(&instance->yankfns, entry, next);
    qemu_mutex_unlock(&lock);
}

void yank_unregister_function(char *instance_name, YankFn *func, void *opaque)
{
    struct YankInstance *instance;
    struct YankFuncAndParam *entry;

    qemu_mutex_lock(&lock);
    instance = yank_find_instance(instance_name);
    assert(instance);

    QLIST_FOREACH(entry, &instance->yankfns, next) {
        if (entry->func == func && entry->opaque == opaque) {
            QLIST_REMOVE(entry, next);
            g_slice_free(struct YankFuncAndParam, entry);
            qemu_mutex_unlock(&lock);
            return;
        }
    }

    abort();
}

void yank_generic_iochannel(void *opaque)
{
    QIOChannel *ioc = QIO_CHANNEL(opaque);

    qio_channel_shutdown(ioc, QIO_CHANNEL_SHUTDOWN_BOTH, NULL);
}

void qmp_yank(strList *instances, Error **errp)
{
    strList *tmp;
    struct YankInstance *instance;
    struct YankFuncAndParam *entry;

    qemu_mutex_lock(&lock);
    tmp = instances;
    for (; tmp; tmp = tmp->next) {
        instance = yank_find_instance(tmp->value);
        if (!instance) {
            error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                      "Instance '%s' not found", tmp->value);
            qemu_mutex_unlock(&lock);
            return;
        }
    }
    tmp = instances;
    for (; tmp; tmp = tmp->next) {
        instance = yank_find_instance(tmp->value);
        assert(instance);
        QLIST_FOREACH(entry, &instance->yankfns, next) {
            entry->func(entry->opaque);
        }
    }
    qemu_mutex_unlock(&lock);
}

YankInstances *qmp_query_yank(Error **errp)
{
    struct YankInstance *instance;
    YankInstances *ret;

    ret = g_new0(YankInstances, 1);
    ret->instances = NULL;

    qemu_mutex_lock(&lock);
    QLIST_FOREACH(instance, &head, next) {
        strList *entry;
        entry = g_new0(strList, 1);
        entry->value = g_strdup(instance->name);
        entry->next = ret->instances;
        ret->instances = entry;
    }
    qemu_mutex_unlock(&lock);

    return ret;
}

static void __attribute__((__constructor__)) yank_init(void)
{
    qemu_mutex_init(&lock);
}
