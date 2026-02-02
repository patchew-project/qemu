/*
 * Dealloc Visitor
 *
 * Copyright (C) 2012-2016 Red Hat, Inc.
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Michael Roth   <mdroth@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/dealloc-visitor.h"
#include "qemu/queue.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"
#include "qobject/qnull.h"
#include "qapi/visitor-impl.h"

typedef struct QStackEntry {
    QObject *obj;               /* QDict or QList being visited */
    void *qapi;
    const QListEntry *entry;    /* If @obj is QList: unvisited tail */
    QSLIST_ENTRY(QStackEntry) node;
} QStackEntry;

struct QapiDeallocVisitor
{
    Visitor visitor;
    QObject *root;
    QSLIST_HEAD(, QStackEntry) stack;
};

static void qapi_dealloc_pop(Visitor *v, void **obj)
{
    QapiDeallocVisitor *qdv = container_of(v, QapiDeallocVisitor, visitor);
    QStackEntry *se = QSLIST_FIRST(&qdv->stack);

    assert(se && se->qapi == obj);
    QSLIST_REMOVE_HEAD(&qdv->stack, node);
    g_free(se);
}

static void qapi_dealloc_push(Visitor *v, QObject *obj, void *qapi)
{
    QapiDeallocVisitor *qdv = container_of(v, QapiDeallocVisitor, visitor);
    QStackEntry *se = g_new0(QStackEntry, 1);

    assert(obj);
    se->obj = obj;
    se->qapi = qapi;

    if (qobject_type(obj) == QTYPE_QLIST) {
        se->entry = qlist_first(qobject_to(QList, obj));
    }

    QSLIST_INSERT_HEAD(&qdv->stack, se, node);
}

static QObject *qapi_dealloc_try_get_object(QapiDeallocVisitor *qdv, const char *name)
{
    QStackEntry *se = QSLIST_FIRST(&qdv->stack);
    QObject *qobj;
    QObject *ret = NULL;

    if (!se) {
        assert(qdv->root);
        return qdv->root;
    }

    qobj = se->obj;
    assert(qobj);

    if (qobject_type(qobj) == QTYPE_QDICT) {
        assert(name);
        ret = qdict_get(qobject_to(QDict, qobj), name);
    } else {
        assert(qobject_type(qobj) == QTYPE_QLIST);
        assert(!name);
        if (se->entry) {
            ret = qlist_entry_obj(se->entry);
        }
    }

    return ret;
}

static bool qapi_dealloc_present_start_struct(Visitor *v, const char *name,
                                              void **obj, size_t size,
                                              Error **errp)
{
    QapiDeallocVisitor *qdv = container_of(v, QapiDeallocVisitor, visitor);
    QObject *qobj = qapi_dealloc_try_get_object(qdv, name);

    if (!qobj) {
        return false;
    }
    assert(qobject_type(qobj) == QTYPE_QDICT);
    qapi_dealloc_push(v, qobj, obj);
    return true;
}

static void qapi_dealloc_present_end_struct(Visitor *v, void **obj)
{
    QapiDeallocVisitor *qdv = container_of(v, QapiDeallocVisitor, visitor);
    QStackEntry *se = QSLIST_FIRST(&qdv->stack);

    assert(qobject_type(se->obj) == QTYPE_QDICT);
    qapi_dealloc_pop(v, obj);

    if (obj) {
        g_free(*obj);
    }
}

static bool qapi_dealloc_present_start_list(Visitor *v, const char *name,
                                            GenericList **list, size_t size,
                                            Error **errp)
{
    QapiDeallocVisitor *qdv = container_of(v, QapiDeallocVisitor, visitor);
    QObject *qobj = qapi_dealloc_try_get_object(qdv, name);

    if (!qobj) {
        return false;
    }
    assert(qobject_type(qobj) == QTYPE_QLIST);
    qapi_dealloc_push(v, qobj, list);
    return true;
}

static void qapi_dealloc_present_end_list(Visitor *v, void **obj)
{
    QapiDeallocVisitor *qdv = container_of(v, QapiDeallocVisitor, visitor);
    QStackEntry *se = QSLIST_FIRST(&qdv->stack);

    assert(qobject_type(se->obj) == QTYPE_QLIST);
    qapi_dealloc_pop(v, obj);
}

static void qapi_dealloc_present_free(Visitor *v)
{
    QapiDeallocVisitor *qdv = container_of(v, QapiDeallocVisitor, visitor);

    while (!QSLIST_EMPTY(&qdv->stack)) {
        QStackEntry *se = QSLIST_FIRST(&qdv->stack);

        QSLIST_REMOVE_HEAD(&qdv->stack, node);
        g_free(se);
    }
    qobject_unref(qdv->root);
    g_free(qdv);
}

static bool qapi_dealloc_start_struct(Visitor *v, const char *name, void **obj,
                                      size_t unused, Error **errp)
{
    return true;
}

static void qapi_dealloc_end_struct(Visitor *v, void **obj)
{
    if (obj) {
        g_free(*obj);
    }
}

static bool qapi_dealloc_start_alternate(Visitor *v, const char *name,
                                         GenericAlternate **obj, size_t size,
                                         Error **errp)
{
    QapiDeallocVisitor *qdv = container_of(v, QapiDeallocVisitor, visitor);
    QObject *qobj = qapi_dealloc_try_get_object(qdv, name);

    if (!qobj) {
        return false;
    }
    assert(*obj);
    (*obj)->type = qobject_type(qobj);
    return true;
}

static void qapi_dealloc_end_alternate(Visitor *v, void **obj)
{
    if (obj) {
        g_free(*obj);
    }
}

static bool qapi_dealloc_start_list(Visitor *v, const char *name,
                                    GenericList **list, size_t size,
                                    Error **errp)
{
    return true;
}

static GenericList *qapi_dealloc_next_list(Visitor *v, GenericList *tail,
                                           size_t size)
{
    GenericList *next = tail->next;
    g_free(tail);
    return next;
}

static void qapi_dealloc_end_list(Visitor *v, void **obj)
{
}

static bool qapi_dealloc_type_str(Visitor *v, const char *name, char **obj,
                                  Error **errp)
{
    if (obj) {
        g_free(*obj);
    }
    return true;
}

static bool qapi_dealloc_type_int64(Visitor *v, const char *name, int64_t *obj,
                                    Error **errp)
{
    return true;
}

static bool qapi_dealloc_type_uint64(Visitor *v, const char *name,
                                     uint64_t *obj, Error **errp)
{
    return true;
}

static bool qapi_dealloc_type_bool(Visitor *v, const char *name, bool *obj,
                                   Error **errp)
{
    return true;
}

static bool qapi_dealloc_type_number(Visitor *v, const char *name, double *obj,
                                     Error **errp)
{
    return true;
}

static bool qapi_dealloc_type_anything(Visitor *v, const char *name,
                                       QObject **obj, Error **errp)
{
    if (obj) {
        qobject_unref(*obj);
    }
    return true;
}

static bool qapi_dealloc_type_null(Visitor *v, const char *name,
                                   QNull **obj, Error **errp)
{
    if (obj) {
        qobject_unref(*obj);
    }
    return true;
}

static void qapi_dealloc_free(Visitor *v)
{
    g_free(container_of(v, QapiDeallocVisitor, visitor));
}

static QapiDeallocVisitor *qapi_dealloc_visitor_new_base(void)
{
    QapiDeallocVisitor *v;

    v = g_malloc0(sizeof(*v));

    v->visitor.type = VISITOR_DEALLOC;

    v->visitor.start_struct = qapi_dealloc_start_struct;
    v->visitor.end_struct = qapi_dealloc_end_struct;
    v->visitor.end_alternate = qapi_dealloc_end_alternate;
    v->visitor.start_list = qapi_dealloc_start_list;
    v->visitor.next_list = qapi_dealloc_next_list;
    v->visitor.end_list = qapi_dealloc_end_list;
    v->visitor.type_int64 = qapi_dealloc_type_int64;
    v->visitor.type_uint64 = qapi_dealloc_type_uint64;
    v->visitor.type_bool = qapi_dealloc_type_bool;
    v->visitor.type_str = qapi_dealloc_type_str;
    v->visitor.type_number = qapi_dealloc_type_number;
    v->visitor.type_any = qapi_dealloc_type_anything;
    v->visitor.type_null = qapi_dealloc_type_null;
    v->visitor.free = qapi_dealloc_free;

    return v;
}

Visitor *qapi_dealloc_visitor_new(void)
{
    QapiDeallocVisitor *v = qapi_dealloc_visitor_new_base();

    return &v->visitor;
}

Visitor *qapi_dealloc_present_visitor_new(QObject *obj)
{
    QapiDeallocVisitor *v = qapi_dealloc_visitor_new_base();

    v->visitor.start_alternate = qapi_dealloc_start_alternate;
    v->visitor.start_list = qapi_dealloc_present_start_list;
    v->visitor.end_list = qapi_dealloc_present_end_list;
    v->visitor.start_struct = qapi_dealloc_present_start_struct;
    v->visitor.end_struct = qapi_dealloc_present_end_struct;
    v->visitor.free = qapi_dealloc_present_free;

    v->root = qobject_ref(obj);

    return &v->visitor;
}
