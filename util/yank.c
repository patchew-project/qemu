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
#include "qapi/qapi-commands-yank.h"
#include "qapi/qapi-visit-yank.h"
#include "qapi/clone-visitor.h"
#include "io/channel.h"
#include "qemu/yank.h"

struct YankFuncAndParam {
    YankFn *func;
    void *opaque;
    QLIST_ENTRY(YankFuncAndParam) next;
};

struct YankInstanceEntry {
    YankInstance *instance;
    QLIST_HEAD(, YankFuncAndParam) yankfns;
    QLIST_ENTRY(YankInstanceEntry) next;
};

typedef struct YankFuncAndParam YankFuncAndParam;
typedef struct YankInstanceEntry YankInstanceEntry;

/*
 * This lock protects the yank_instance_list below. Because it's taken by
 * OOB-capable commands, it must be "fast", i.e. it may only be held for a
 * bounded, short time. See docs/devel/qapi-code-gen.txt for additional
 * information.
 */
static QemuMutex yank_lock;

static QLIST_HEAD(, YankInstanceEntry) yank_instance_list
    = QLIST_HEAD_INITIALIZER(yank_instance_list);

static bool yank_compare_instances(const YankInstance *a, const YankInstance *b)
{
    if (a->type != b->type) {
        return false;
    }

    switch (a->type) {
    case YANK_INSTANCE_TYPE_BLOCK_NODE:
        return !strcmp(a->u.block_node.node_name, b->u.block_node.node_name);
    break;

    case YANK_INSTANCE_TYPE_CHARDEV:
        return !strcmp(a->u.chardev.id, b->u.chardev.id);
    break;

    case YANK_INSTANCE_TYPE_MIGRATION:
        return true;
    break;

    default:
        abort();
    }
}

static YankInstanceEntry *yank_find_entry(const YankInstance *instance)
{
    YankInstanceEntry *entry;

    QLIST_FOREACH(entry, &yank_instance_list, next) {
        if (yank_compare_instances(entry->instance, instance)) {
            return entry;
        }
    }
    return NULL;
}

void yank_register_instance(const YankInstance *instance, Error **errp)
{
    YankInstanceEntry *entry;

    qemu_mutex_lock(&yank_lock);

    if (yank_find_entry(instance)) {
        error_setg(errp, "duplicate yank instance");
        qemu_mutex_unlock(&yank_lock);
        return;
    }

    entry = g_slice_new(YankInstanceEntry);
    entry->instance = QAPI_CLONE(YankInstance, instance);
    QLIST_INIT(&entry->yankfns);
    QLIST_INSERT_HEAD(&yank_instance_list, entry, next);

    qemu_mutex_unlock(&yank_lock);
}

void yank_unregister_instance(const YankInstance *instance)
{
    YankInstanceEntry *entry;

    qemu_mutex_lock(&yank_lock);
    entry = yank_find_entry(instance);
    assert(entry);

    assert(QLIST_EMPTY(&entry->yankfns));
    QLIST_REMOVE(entry, next);
    qapi_free_YankInstance(entry->instance);
    g_slice_free(YankInstanceEntry, entry);

    qemu_mutex_unlock(&yank_lock);
}

void yank_register_function(const YankInstance *instance,
                            YankFn *func,
                            void *opaque)
{
    YankInstanceEntry *entry;
    YankFuncAndParam *func_entry;

    qemu_mutex_lock(&yank_lock);
    entry = yank_find_entry(instance);
    assert(entry);

    func_entry = g_slice_new(YankFuncAndParam);
    func_entry->func = func;
    func_entry->opaque = opaque;

    QLIST_INSERT_HEAD(&entry->yankfns, func_entry, next);
    qemu_mutex_unlock(&yank_lock);
}

void yank_unregister_function(const YankInstance *instance,
                              YankFn *func,
                              void *opaque)
{
    YankInstanceEntry *entry;
    YankFuncAndParam *func_entry;

    qemu_mutex_lock(&yank_lock);
    entry = yank_find_entry(instance);
    assert(entry);

    QLIST_FOREACH(func_entry, &entry->yankfns, next) {
        if (func_entry->func == func && func_entry->opaque == opaque) {
            QLIST_REMOVE(func_entry, next);
            g_slice_free(YankFuncAndParam, func_entry);
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

void qmp_yank(YankInstanceList *instances,
              Error **errp)
{
    YankInstanceList *tail;
    YankInstanceEntry *entry;
    YankFuncAndParam *func_entry;

    qemu_mutex_lock(&yank_lock);
    for (tail = instances; tail; tail = tail->next) {
        entry = yank_find_entry(tail->value);
        if (!entry) {
            error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND, "Instance not found");
            qemu_mutex_unlock(&yank_lock);
            return;
        }
    }
    for (tail = instances; tail; tail = tail->next) {
        entry = yank_find_entry(tail->value);
        assert(entry);
        QLIST_FOREACH(func_entry, &entry->yankfns, next) {
            func_entry->func(func_entry->opaque);
        }
    }
    qemu_mutex_unlock(&yank_lock);
}

YankInstanceList *qmp_query_yank(Error **errp)
{
    YankInstanceEntry *entry;
    YankInstanceList *ret;

    ret = NULL;

    qemu_mutex_lock(&yank_lock);
    QLIST_FOREACH(entry, &yank_instance_list, next) {
        YankInstanceList *new_entry;
        new_entry = g_new0(YankInstanceList, 1);
        new_entry->value = QAPI_CLONE(YankInstance, entry->instance);
        new_entry->next = ret;
        ret = new_entry;
    }
    qemu_mutex_unlock(&yank_lock);

    return ret;
}

static void __attribute__((__constructor__)) yank_init(void)
{
    qemu_mutex_init(&yank_lock);
}
