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
#include "qemu/yank.h"

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

/*
 * This lock protects the yank_instance_list below.
 */
static QemuMutex yank_lock;

static QLIST_HEAD(, YankInstance) yank_instance_list
    = QLIST_HEAD_INITIALIZER(yank_instance_list);

static struct YankInstance *yank_find_instance(const char *name)
{
    struct YankInstance *instance;

    QLIST_FOREACH(instance, &yank_instance_list, next) {
        if (!strcmp(instance->name, name)) {
            return instance;
        }
    }
    return NULL;
}

void yank_register_instance(const char *instance_name, Error **errp)
{
    struct YankInstance *instance;

    qemu_mutex_lock(&yank_lock);

    if (yank_find_instance(instance_name)) {
        error_setg(errp, "duplicate yank instance name: '%s'",
                   instance_name);
        qemu_mutex_unlock(&yank_lock);
        return;
    }

    instance = g_slice_new(struct YankInstance);
    instance->name = g_strdup(instance_name);
    QLIST_INIT(&instance->yankfns);
    QLIST_INSERT_HEAD(&yank_instance_list, instance, next);

    qemu_mutex_unlock(&yank_lock);
}

void yank_unregister_instance(const char *instance_name)
{
    struct YankInstance *instance;

    qemu_mutex_lock(&yank_lock);
    instance = yank_find_instance(instance_name);
    assert(instance);

    assert(QLIST_EMPTY(&instance->yankfns));
    QLIST_REMOVE(instance, next);
    g_free(instance->name);
    g_slice_free(struct YankInstance, instance);

    qemu_mutex_unlock(&yank_lock);
}

void yank_register_function(const char *instance_name,
                            YankFn *func,
                            void *opaque)
{
    struct YankInstance *instance;
    struct YankFuncAndParam *entry;

    qemu_mutex_lock(&yank_lock);
    instance = yank_find_instance(instance_name);
    assert(instance);

    entry = g_slice_new(struct YankFuncAndParam);
    entry->func = func;
    entry->opaque = opaque;

    QLIST_INSERT_HEAD(&instance->yankfns, entry, next);
    qemu_mutex_unlock(&yank_lock);
}

void yank_unregister_function(const char *instance_name,
                              YankFn *func,
                              void *opaque)
{
    struct YankInstance *instance;
    struct YankFuncAndParam *entry;

    qemu_mutex_lock(&yank_lock);
    instance = yank_find_instance(instance_name);
    assert(instance);

    QLIST_FOREACH(entry, &instance->yankfns, next) {
        if (entry->func == func && entry->opaque == opaque) {
            QLIST_REMOVE(entry, next);
            g_slice_free(struct YankFuncAndParam, entry);
            qemu_mutex_unlock(&yank_lock);
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

void qmp_yank(strList *instances,
              Error **errp)
{
    strList *tail;
    struct YankInstance *instance;
    struct YankFuncAndParam *entry;

    qemu_mutex_lock(&yank_lock);
    for (tail = instances; tail; tail = tail->next) {
        instance = yank_find_instance(tail->value);
        if (!instance) {
            error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                      "Instance '%s' not found", tail->value);
            qemu_mutex_unlock(&yank_lock);
            return;
        }
    }
    for (tail = instances; tail; tail = tail->next) {
        instance = yank_find_instance(tail->value);
        assert(instance);
        QLIST_FOREACH(entry, &instance->yankfns, next) {
            entry->func(entry->opaque);
        }
    }
    qemu_mutex_unlock(&yank_lock);
}

YankInstances *qmp_query_yank(Error **errp)
{
    struct YankInstance *instance;
    YankInstances *ret;

    ret = g_new0(YankInstances, 1);
    ret->instances = NULL;

    qemu_mutex_lock(&yank_lock);
    QLIST_FOREACH(instance, &yank_instance_list, next) {
        strList *entry;
        entry = g_new0(strList, 1);
        entry->value = g_strdup(instance->name);
        entry->next = ret->instances;
        ret->instances = entry;
    }
    qemu_mutex_unlock(&yank_lock);

    return ret;
}

static void __attribute__((__constructor__)) yank_init(void)
{
    qemu_mutex_init(&yank_lock);
}
