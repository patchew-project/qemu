/*
 * Input Visitor
 *
 * Copyright (C) 2012-2017 Red Hat, Inc.
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include <math.h>
#include "qapi/error.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/visitor-impl.h"
#include "qemu/queue.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qbool.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qmp/qlist.h"
#include "qapi/qmp/qnull.h"
#include "qapi/qmp/qnum.h"
#include "qapi/qmp/qstring.h"
#include "qemu/cutils.h"
#include "qemu/option.h"

/*
 * Describes an alias that is relevant for the current StackObject,
 * either because it aliases a member of the currently visited object
 * or because it aliases a member of a nested object.
 *
 * When processing a nested object, all InputVisitorAlias objects that
 * are relevant for the nested object are propagated, i.e. copied with
 * the name of the nested object removed from @source.
 */
typedef struct InputVisitorAlias {
    /* StackObject in which the alias was defined */
    struct StackObject *alias_so;

    /*
     * Alias name as defined for @alias_so.
     * NULL means that this is a wildcard alias, i.e. all members of
     * @src get an alias in @alias_so with the same name.
     */
    const char *name;

    /*
     * NULL terminated array representing a path to the source member
     * that the alias refers to.
     *
     * Must contain at least one non-NULL element if @alias is not NULL.
     *
     * If it contains no non-NULL element, @alias_so must be different
     * from the StackObject which contains this InputVisitorAlias in
     * its aliases list.  In this case, all elements in the currently
     * visited object have an alias with the same name in @alias_so.
     */
    const char **src;

    /*
     * The alias remains valid as long as the StackObject which
     * contains this InputVisitorAlias in its aliases list has
     * StackObject.alias_scope_nesting >= InputVisitorAlias.scope_nesting
     * or until the whole StackObject is removed.
     */
    int scope_nesting;

    QSLIST_ENTRY(InputVisitorAlias) next;
} InputVisitorAlias;

typedef struct StackObject {
    const char *name;            /* Name of @obj in its parent, if any */
    QObject *obj;                /* QDict or QList being visited */
    void *qapi; /* sanity check that caller uses same pointer */

    GHashTable *h;              /* If @obj is QDict: unvisited keys */
    const QListEntry *entry;    /* If @obj is QList: unvisited tail */
    unsigned index;             /* If @obj is QList: list index of @entry */

    QSLIST_HEAD(, InputVisitorAlias) aliases;
    int alias_scope_nesting;    /* Number of open alias scopes */

    QSLIST_ENTRY(StackObject) node; /* parent */
} StackObject;

struct QObjectInputVisitor {
    Visitor visitor;

    /* Root of visit at visitor creation. */
    QObject *root;
    bool keyval;                /* Assume @root made with keyval_parse() */

    QDict *empty_qdict;         /* Used for implicit objects */

    /* Stack of objects being visited (all entries will be either
     * QDict or QList). */
    QSLIST_HEAD(, StackObject) stack;

    GString *errname;           /* Accumulator for full_name() */
};

static QObjectInputVisitor *to_qiv(Visitor *v)
{
    return container_of(v, QObjectInputVisitor, visitor);
}

/*
 * Find the full name of a member in @so which @qiv is currently
 * visiting.  If the currently visited thing is an object, @name is
 * the (local) name of the member to describe.  If it is a list, @name
 * is ignored and the current index (so->index) is included.
 *
 * If @skip_member is true, find the full name of @so itself instead.
 * @name must be NULL then.
 *
 * The returned string is valid until the next full_name_so(@qiv) or
 * destruction of @qiv.
 */
static const char *full_name_so(QObjectInputVisitor *qiv, const char *name,
                                bool skip_member, StackObject *so)
{
    char buf[32];

    if (qiv->errname) {
        g_string_truncate(qiv->errname, 0);
    } else {
        qiv->errname = g_string_new("");
    }

    if (skip_member && so) {
        assert(name == NULL);
        name = so->name;
        so = QSLIST_NEXT(so, node);
    }

    for (; so; so = QSLIST_NEXT(so, node)) {
        if (qobject_type(so->obj) == QTYPE_QDICT) {
            g_string_prepend(qiv->errname, name ?: "<anonymous>");
            g_string_prepend_c(qiv->errname, '.');
        } else {
            snprintf(buf, sizeof(buf),
                     qiv->keyval ? ".%u" : "[%u]",
                     so->index);
            g_string_prepend(qiv->errname, buf);
        }
        name = so->name;
    }

    if (name) {
        g_string_prepend(qiv->errname, name);
    } else if (qiv->errname->str[0] == '.') {
        g_string_erase(qiv->errname, 0, 1);
    } else if (!qiv->errname->str[0]) {
        return "<anonymous>";
    }

    return qiv->errname->str;
}

static const char *full_name(QObjectInputVisitor *qiv, const char *name)
{
    StackObject *tos = QSLIST_FIRST(&qiv->stack);

    return full_name_so(qiv, name, false, tos);
}

static bool find_object_member(QObjectInputVisitor *qiv,
                               StackObject **so, const char **name,
                               bool *implicit_object, Error **errp);

/*
 * Check whether the alias member defined by @a is present in the
 * input and can be used to obtain the value for the member @name in
 * the currently visited object.
 */
static bool alias_present(QObjectInputVisitor *qiv,
                          InputVisitorAlias *a, const char *name)
{
    StackObject *so = a->alias_so;

    /*
     * The passed source @name is only relevant for wildcard aliases which
     * don't have a separate name, otherwise we use the alias name.
     */
    if (a->name) {
        name = a->name;
    }

    /*
     * Check whether the alias member is present in the input
     * (possibly recursively because aliases are transitive).
     */
    if (!find_object_member(qiv, &so, &name, NULL, NULL)) {
        return false;
    }

    /*
     * Every source can be used only once. If a value in the input would end up
     * being used twice through aliases, we'll fail the second access.
     */
    if (!g_hash_table_contains(so->h, name)) {
        return false;
    }

    return true;
}

/*
 * Check whether the member @name in the object visited by @so can be
 * specified in the input by using the alias described by @a.
 *
 * If @name is only a prefix of the alias source, but doesn't match
 * immediately, false is returned and @implicit_object is set to true
 * if it is non-NULL.  In all other cases, @implicit_object is left
 * unchanged.
 */
static bool alias_source_matches(QObjectInputVisitor *qiv,
                                 StackObject *so, InputVisitorAlias *a,
                                 const char *name, bool *implicit_object)
{
    if (a->src[0] == NULL) {
        assert(a->name == NULL);
        return true;
    }

    if (!strcmp(a->src[0], name)) {
        if (a->name && a->src[1] == NULL) {
            /*
             * We're matching an exact member, the source for this alias is
             * immediately in @so.
             */
            return true;
        } else if (implicit_object) {
            /*
             * We're only looking at a prefix of the source path for the alias.
             * If the input contains no object of the requested name, we will
             * implicitly create an empty one so that the alias can still be
             * used.
             *
             * We want to create the implicit object only if the alias is
             * actually used, but we can't tell here for wildcard aliases (only
             * a later visitor call will determine this). This means that
             * wildcard aliases must never have optional keys in their source
             * path.
             */
            if (!a->name || alias_present(qiv, a, a->name)) {
                *implicit_object = true;
            }
        }
    }

    return false;
}

/*
 * Find the place in the input where the value for the object member
 * @name in @so is specified, considering applicable aliases.
 *
 * If a value could be found, true is returned and @so and @name are
 * updated to identify the key name and StackObject where the value
 * can be found in the input.  (This is either unchanged or the
 * alias_so/name of an alias.)  The value of @implicit_object on
 * return is undefined in this case.
 *
 * If no value could be found in the input, false is returned.  This
 * is not an error and @errp remains unchanged.  If @implicit_object
 * is non-NULL, it is set to true if the given name is a prefix of the
 * source path of an alias for which a value may be present in the
 * input.  It is set to false otherwise.
 *
 * If an error occurs (e.g. two values are specified for the member
 * through different names), false is returned and @errp is set.  The
 * value of @implicit_object on return is undefined in this case.
 */
static bool find_object_member(QObjectInputVisitor *qiv,
                               StackObject **so, const char **name,
                               bool *implicit_object, Error **errp)
{
    StackObject *cur_so = *so;
    QDict *qdict = qobject_to(QDict, cur_so->obj);
    const char *found = NULL;
    bool found_is_wildcard = false;
    InputVisitorAlias *a;

    if (implicit_object) {
        *implicit_object = false;
    }

    /* Directly present in the container */
    if (qdict_haskey(qdict, *name)) {
        found = *name;
    }

    /*
     * Find aliases whose source path matches @name in this StackObject. We can
     * then get the value with the key a->name from a->alias_so.
     */
    QSLIST_FOREACH(a, &cur_so->aliases, next) {
        if (a->name == NULL && found) {
            /*
             * Skip wildcard aliases if we already have a match. This is
             * not a conflict that should result in an error.
             */
            continue;
        }

        if (!alias_source_matches(qiv, cur_so, a, *name, implicit_object)) {
            continue;
        }

        if (!alias_present(qiv, a, *name)) {
            continue;
        }

        if (found && !found_is_wildcard) {
            error_setg(errp, "Value for parameter %s was already given "
                       "through an alias",
                       full_name_so(qiv, *name, false, *so));
            return false;
        } else {
            found = a->name ?: *name;
            *so = a->alias_so;
            found_is_wildcard = !a->name;
        }
    }

    /* Chained aliases: *so/found might be the source of another alias */
    if (found && (*so != cur_so || found != *name)) {
        find_object_member(qiv, so, &found, NULL, errp);
    }

    *name = found;
    return found;
}

static QObject *qobject_input_try_get_object(QObjectInputVisitor *qiv,
                                             const char *name,
                                             bool consume, Error **errp)
{
    StackObject *tos;
    QObject *qobj;
    QObject *ret;

    if (QSLIST_EMPTY(&qiv->stack)) {
        /* Starting at root, name is ignored. */
        assert(qiv->root);
        return qiv->root;
    }

    /* We are in a container; find the next element. */
    tos = QSLIST_FIRST(&qiv->stack);
    qobj = tos->obj;
    assert(qobj);

    if (qobject_type(qobj) == QTYPE_QDICT) {
        StackObject *so = tos;
        const char *key = name;
        bool implicit_object;

        assert(key);
        if (!find_object_member(qiv, &so, &key, &implicit_object, errp)) {
            if (implicit_object) {
                /*
                 * The member is not present in the input, but
                 * something inside of it might still be given through
                 * an alias. Pretend there was an empty object in the
                 * input.
                 */
                if (!qiv->empty_qdict) {
                    qiv->empty_qdict = qdict_new();
                }
                return QOBJECT(qiv->empty_qdict);
            } else {
                return NULL;
            }
        }
        ret = qdict_get(qobject_to(QDict, so->obj), key);
        if (so->h && consume && ret) {
            bool removed = g_hash_table_remove(so->h, key);
            assert(removed);
        }
    } else {
        assert(qobject_type(qobj) == QTYPE_QLIST);
        assert(!name);
        if (tos->entry) {
            ret = qlist_entry_obj(tos->entry);
            if (consume) {
                tos->entry = qlist_next(tos->entry);
            }
        } else {
            ret = NULL;
        }
        if (consume) {
            tos->index++;
        }
    }

    return ret;
}

static QObject *qobject_input_get_object(QObjectInputVisitor *qiv,
                                         const char *name,
                                         bool consume, Error **errp)
{
    ERRP_GUARD();
    QObject *obj = qobject_input_try_get_object(qiv, name, consume, errp);

    if (!obj && !*errp) {
        error_setg(errp, QERR_MISSING_PARAMETER, full_name(qiv, name));
    }
    return obj;
}

static const char *qobject_input_get_keyval(QObjectInputVisitor *qiv,
                                            const char *name,
                                            Error **errp)
{
    QObject *qobj;
    QString *qstr;

    qobj = qobject_input_get_object(qiv, name, true, errp);
    if (!qobj) {
        return NULL;
    }

    qstr = qobject_to(QString, qobj);
    if (!qstr) {
        switch (qobject_type(qobj)) {
        case QTYPE_QDICT:
        case QTYPE_QLIST:
            error_setg(errp, "Parameters '%s.*' are unexpected",
                       full_name(qiv, name));
            return NULL;
        default:
            /* Non-string scalar (should this be an assertion?) */
            error_setg(errp, "Internal error: parameter %s invalid",
                       full_name(qiv, name));
            return NULL;
        }
    }

    return qstring_get_str(qstr);
}

/*
 * Propagate aliases from the parent StackObject @src to its direct
 * child StackObject @dst, which is representing the child struct @name.
 *
 * Every alias whose source path begins with @dst->name and which still
 * applies in @dst (i.e. it is either a wildcard alias or has at least
 * one more source path element) is propagated to @dst with the first
 * element (i.e. @dst->name) removed from the source path.
 */
static void propagate_aliases(StackObject *dst, StackObject *src)
{
    InputVisitorAlias *a;

    QSLIST_FOREACH(a, &src->aliases, next) {
        if (!a->src[0] || strcmp(a->src[0], dst->name)) {
            continue;
        }

        /*
         * If this is not a wildcard alias, but a->src[1] is NULL,
         * then it referred to dst->name in src and doesn't apply
         * inside dst any more.
         */
        if (a->src[1] || !a->name) {
            InputVisitorAlias *alias = g_new(InputVisitorAlias, 1);

            *alias = (InputVisitorAlias) {
                .name       = a->name,
                .alias_so   = a->alias_so,
                .src        = &a->src[1],
            };

            QSLIST_INSERT_HEAD(&dst->aliases, alias, next);
        }
    }
}

static const QListEntry *qobject_input_push(QObjectInputVisitor *qiv,
                                            const char *name,
                                            QObject *obj, void *qapi)
{
    GHashTable *h;
    StackObject *tos = g_new0(StackObject, 1);
    QDict *qdict = qobject_to(QDict, obj);
    QList *qlist = qobject_to(QList, obj);
    const QDictEntry *entry;

    assert(obj);
    tos->name = name;
    tos->obj = obj;
    tos->qapi = qapi;

    if (qdict) {
        h = g_hash_table_new(g_str_hash, g_str_equal);
        for (entry = qdict_first(qdict);
             entry;
             entry = qdict_next(qdict, entry)) {
            g_hash_table_insert(h, (void *)qdict_entry_key(entry), NULL);
        }
        tos->h = h;
        if (!QSLIST_EMPTY(&qiv->stack)) {
            propagate_aliases(tos, QSLIST_FIRST(&qiv->stack));
        }
    } else {
        assert(qlist);
        tos->entry = qlist_first(qlist);
        tos->index = -1;
    }

    QSLIST_INSERT_HEAD(&qiv->stack, tos, node);
    return tos->entry;
}


static bool qobject_input_check_struct(Visitor *v, Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    StackObject *tos = QSLIST_FIRST(&qiv->stack);
    GHashTableIter iter;
    const char *key;

    assert(tos && !tos->entry);

    g_hash_table_iter_init(&iter, tos->h);
    if (g_hash_table_iter_next(&iter, (void **)&key, NULL)) {
        error_setg(errp, "Parameter '%s' is unexpected",
                   full_name(qiv, key));
        return false;
    }
    return true;
}

static void qobject_input_stack_object_free(StackObject *tos)
{
    InputVisitorAlias *a;

    if (tos->h) {
        g_hash_table_unref(tos->h);
    }

    while ((a = QSLIST_FIRST(&tos->aliases))) {
        QSLIST_REMOVE_HEAD(&tos->aliases, next);
        g_free(a);
    }

    g_free(tos);
}

static void qobject_input_pop(Visitor *v, void **obj)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    StackObject *tos = QSLIST_FIRST(&qiv->stack);

    assert(tos && tos->qapi == obj);
    QSLIST_REMOVE_HEAD(&qiv->stack, node);
    qobject_input_stack_object_free(tos);
}

static void qobject_input_start_alias_scope(Visitor *v)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    StackObject *tos = QSLIST_FIRST(&qiv->stack);

    tos->alias_scope_nesting++;
}

static void qobject_input_end_alias_scope(Visitor *v)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    StackObject *tos = QSLIST_FIRST(&qiv->stack);
    InputVisitorAlias *a, *next;

    assert(tos->alias_scope_nesting > 0);
    tos->alias_scope_nesting--;

    QSLIST_FOREACH_SAFE(a, &tos->aliases, next, next) {
        if (a->scope_nesting > tos->alias_scope_nesting) {
            QSLIST_REMOVE(&tos->aliases, a, InputVisitorAlias, next);
            g_free(a);
        }
    }
}

static void qobject_input_define_alias(Visitor *v, const char *name,
                                       const char **source)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    StackObject *tos = QSLIST_FIRST(&qiv->stack);
    InputVisitorAlias *alias = g_new(InputVisitorAlias, 1);

    /*
     * The source path can become empty during alias propagation for
     * wildcard aliases, but not when defining an alias (it would map
     * all names onto themselves, which doesn't make sense).
     */
    assert(source[0]);

    *alias = (InputVisitorAlias) {
        .name       = name,
        .alias_so   = tos,
        .src        = source,
    };

    QSLIST_INSERT_HEAD(&tos->aliases, alias, next);
}

static bool qobject_input_start_struct(Visitor *v, const char *name, void **obj,
                                       size_t size, Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qobject_input_get_object(qiv, name, true, errp);

    if (obj) {
        *obj = NULL;
    }
    if (!qobj) {
        return false;
    }
    if (qobject_type(qobj) != QTYPE_QDICT) {
        error_setg(errp, QERR_INVALID_PARAMETER_TYPE,
                   full_name(qiv, name), "object");
        return false;
    }

    qobject_input_push(qiv, name, qobj, obj);

    if (obj) {
        *obj = g_malloc0(size);
    }
    return true;
}

static void qobject_input_end_struct(Visitor *v, void **obj)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    StackObject *tos = QSLIST_FIRST(&qiv->stack);

    assert(qobject_type(tos->obj) == QTYPE_QDICT && tos->h);
    qobject_input_pop(v, obj);
}


static bool qobject_input_start_list(Visitor *v, const char *name,
                                     GenericList **list, size_t size,
                                     Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qobject_input_get_object(qiv, name, true, errp);
    const QListEntry *entry;

    if (list) {
        *list = NULL;
    }
    if (!qobj) {
        return false;
    }
    if (qobject_type(qobj) != QTYPE_QLIST) {
        error_setg(errp, QERR_INVALID_PARAMETER_TYPE,
                   full_name(qiv, name), "array");
        return false;
    }

    entry = qobject_input_push(qiv, name, qobj, list);
    if (entry && list) {
        *list = g_malloc0(size);
    }
    return true;
}

static GenericList *qobject_input_next_list(Visitor *v, GenericList *tail,
                                            size_t size)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    StackObject *tos = QSLIST_FIRST(&qiv->stack);

    assert(tos && qobject_to(QList, tos->obj));

    if (!tos->entry) {
        return NULL;
    }
    tail->next = g_malloc0(size);
    return tail->next;
}

static bool qobject_input_check_list(Visitor *v, Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    StackObject *tos = QSLIST_FIRST(&qiv->stack);

    assert(tos && qobject_to(QList, tos->obj));

    if (tos->entry) {
        error_setg(errp, "Only %u list elements expected in %s",
                   tos->index + 1, full_name_so(qiv, NULL, true, tos));
        return false;
    }
    return true;
}

static void qobject_input_end_list(Visitor *v, void **obj)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    StackObject *tos = QSLIST_FIRST(&qiv->stack);

    assert(qobject_type(tos->obj) == QTYPE_QLIST && !tos->h);
    qobject_input_pop(v, obj);
}

static bool qobject_input_start_alternate(Visitor *v, const char *name,
                                          GenericAlternate **obj, size_t size,
                                          Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qobject_input_get_object(qiv, name, false, errp);

    if (!qobj) {
        *obj = NULL;
        return false;
    }
    *obj = g_malloc0(size);
    (*obj)->type = qobject_type(qobj);
    return true;
}

static bool qobject_input_type_int64(Visitor *v, const char *name, int64_t *obj,
                                     Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qobject_input_get_object(qiv, name, true, errp);
    QNum *qnum;

    if (!qobj) {
        return false;
    }
    qnum = qobject_to(QNum, qobj);
    if (!qnum || !qnum_get_try_int(qnum, obj)) {
        error_setg(errp, QERR_INVALID_PARAMETER_TYPE,
                   full_name(qiv, name), "integer");
        return false;
    }
    return true;
}

static bool qobject_input_type_int64_keyval(Visitor *v, const char *name,
                                            int64_t *obj, Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    const char *str = qobject_input_get_keyval(qiv, name, errp);

    if (!str) {
        return false;
    }

    if (qemu_strtoi64(str, NULL, 0, obj) < 0) {
        /* TODO report -ERANGE more nicely */
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
                   full_name(qiv, name), "integer");
        return false;
    }
    return true;
}

static bool qobject_input_type_uint64(Visitor *v, const char *name,
                                      uint64_t *obj, Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qobject_input_get_object(qiv, name, true, errp);
    QNum *qnum;
    int64_t val;

    if (!qobj) {
        return false;
    }
    qnum = qobject_to(QNum, qobj);
    if (!qnum) {
        goto err;
    }

    if (qnum_get_try_uint(qnum, obj)) {
        return true;
    }

    /* Need to accept negative values for backward compatibility */
    if (qnum_get_try_int(qnum, &val)) {
        *obj = val;
        return true;
    }

err:
    error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
               full_name(qiv, name), "uint64");
    return false;
}

static bool qobject_input_type_uint64_keyval(Visitor *v, const char *name,
                                             uint64_t *obj, Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    const char *str = qobject_input_get_keyval(qiv, name, errp);

    if (!str) {
        return false;
    }

    if (qemu_strtou64(str, NULL, 0, obj) < 0) {
        /* TODO report -ERANGE more nicely */
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
                   full_name(qiv, name), "integer");
        return false;
    }
    return true;
}

static bool qobject_input_type_bool(Visitor *v, const char *name, bool *obj,
                                    Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qobject_input_get_object(qiv, name, true, errp);
    QBool *qbool;

    if (!qobj) {
        return false;
    }
    qbool = qobject_to(QBool, qobj);
    if (!qbool) {
        error_setg(errp, QERR_INVALID_PARAMETER_TYPE,
                   full_name(qiv, name), "boolean");
        return false;
    }

    *obj = qbool_get_bool(qbool);
    return true;
}

static bool qobject_input_type_bool_keyval(Visitor *v, const char *name,
                                           bool *obj, Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    const char *str = qobject_input_get_keyval(qiv, name, errp);

    if (!str) {
        return false;
    }

    if (!qapi_bool_parse(name, str, obj, NULL)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
                   full_name(qiv, name), "'on' or 'off'");
        return false;
    }
    return true;
}

static bool qobject_input_type_str(Visitor *v, const char *name, char **obj,
                                   Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qobject_input_get_object(qiv, name, true, errp);
    QString *qstr;

    *obj = NULL;
    if (!qobj) {
        return false;
    }
    qstr = qobject_to(QString, qobj);
    if (!qstr) {
        error_setg(errp, QERR_INVALID_PARAMETER_TYPE,
                   full_name(qiv, name), "string");
        return false;
    }

    *obj = g_strdup(qstring_get_str(qstr));
    return true;
}

static bool qobject_input_type_str_keyval(Visitor *v, const char *name,
                                          char **obj, Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    const char *str = qobject_input_get_keyval(qiv, name, errp);

    *obj = g_strdup(str);
    return !!str;
}

static bool qobject_input_type_number(Visitor *v, const char *name, double *obj,
                                      Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qobject_input_get_object(qiv, name, true, errp);
    QNum *qnum;

    if (!qobj) {
        return false;
    }
    qnum = qobject_to(QNum, qobj);
    if (!qnum) {
        error_setg(errp, QERR_INVALID_PARAMETER_TYPE,
                   full_name(qiv, name), "number");
        return false;
    }

    *obj = qnum_get_double(qnum);
    return true;
}

static bool qobject_input_type_number_keyval(Visitor *v, const char *name,
                                             double *obj, Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    const char *str = qobject_input_get_keyval(qiv, name, errp);
    double val;

    if (!str) {
        return false;
    }

    if (qemu_strtod_finite(str, NULL, &val)) {
        /* TODO report -ERANGE more nicely */
        error_setg(errp, QERR_INVALID_PARAMETER_TYPE,
                   full_name(qiv, name), "number");
        return false;
    }

    *obj = val;
    return true;
}

static bool qobject_input_type_any(Visitor *v, const char *name, QObject **obj,
                                   Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qobject_input_get_object(qiv, name, true, errp);

    *obj = NULL;
    if (!qobj) {
        return false;
    }

    *obj = qobject_ref(qobj);
    return true;
}

static bool qobject_input_type_null(Visitor *v, const char *name,
                                    QNull **obj, Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qobject_input_get_object(qiv, name, true, errp);

    *obj = NULL;
    if (!qobj) {
        return false;
    }

    if (qobject_type(qobj) != QTYPE_QNULL) {
        error_setg(errp, QERR_INVALID_PARAMETER_TYPE,
                   full_name(qiv, name), "null");
        return false;
    }
    *obj = qnull();
    return true;
}

static bool qobject_input_type_size_keyval(Visitor *v, const char *name,
                                           uint64_t *obj, Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    const char *str = qobject_input_get_keyval(qiv, name, errp);

    if (!str) {
        return false;
    }

    if (qemu_strtosz(str, NULL, obj) < 0) {
        /* TODO report -ERANGE more nicely */
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
                   full_name(qiv, name), "size");
        return false;
    }
    return true;
}

static void qobject_input_optional(Visitor *v, const char *name, bool *present)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    Error *local_err = NULL;
    QObject *qobj = qobject_input_try_get_object(qiv, name, false, &local_err);

    /* If there was an error, let the caller try and run into the error */
    if (!qobj && !local_err) {
        *present = false;
        return;
    }

    error_free(local_err);
    *present = true;
}

static void qobject_input_free(Visitor *v)
{
    QObjectInputVisitor *qiv = to_qiv(v);

    while (!QSLIST_EMPTY(&qiv->stack)) {
        StackObject *tos = QSLIST_FIRST(&qiv->stack);

        QSLIST_REMOVE_HEAD(&qiv->stack, node);
        qobject_input_stack_object_free(tos);
    }

    qobject_unref(qiv->empty_qdict);
    qobject_unref(qiv->root);
    if (qiv->errname) {
        g_string_free(qiv->errname, TRUE);
    }
    g_free(qiv);
}

static QObjectInputVisitor *qobject_input_visitor_base_new(QObject *obj)
{
    QObjectInputVisitor *v = g_malloc0(sizeof(*v));

    assert(obj);

    v->visitor.type = VISITOR_INPUT;
    v->visitor.start_struct = qobject_input_start_struct;
    v->visitor.check_struct = qobject_input_check_struct;
    v->visitor.end_struct = qobject_input_end_struct;
    v->visitor.start_list = qobject_input_start_list;
    v->visitor.next_list = qobject_input_next_list;
    v->visitor.check_list = qobject_input_check_list;
    v->visitor.end_list = qobject_input_end_list;
    v->visitor.start_alternate = qobject_input_start_alternate;
    v->visitor.optional = qobject_input_optional;
    v->visitor.define_alias = qobject_input_define_alias;
    v->visitor.start_alias_scope = qobject_input_start_alias_scope;
    v->visitor.end_alias_scope = qobject_input_end_alias_scope;
    v->visitor.free = qobject_input_free;

    v->root = qobject_ref(obj);

    return v;
}

Visitor *qobject_input_visitor_new(QObject *obj)
{
    QObjectInputVisitor *v = qobject_input_visitor_base_new(obj);

    v->visitor.type_int64 = qobject_input_type_int64;
    v->visitor.type_uint64 = qobject_input_type_uint64;
    v->visitor.type_bool = qobject_input_type_bool;
    v->visitor.type_str = qobject_input_type_str;
    v->visitor.type_number = qobject_input_type_number;
    v->visitor.type_any = qobject_input_type_any;
    v->visitor.type_null = qobject_input_type_null;

    return &v->visitor;
}

Visitor *qobject_input_visitor_new_keyval(QObject *obj)
{
    QObjectInputVisitor *v = qobject_input_visitor_base_new(obj);

    v->visitor.type_int64 = qobject_input_type_int64_keyval;
    v->visitor.type_uint64 = qobject_input_type_uint64_keyval;
    v->visitor.type_bool = qobject_input_type_bool_keyval;
    v->visitor.type_str = qobject_input_type_str_keyval;
    v->visitor.type_number = qobject_input_type_number_keyval;
    v->visitor.type_any = qobject_input_type_any;
    v->visitor.type_null = qobject_input_type_null;
    v->visitor.type_size = qobject_input_type_size_keyval;
    v->keyval = true;

    return &v->visitor;
}

Visitor *qobject_input_visitor_new_str(const char *str,
                                       const char *implied_key,
                                       Error **errp)
{
    bool is_json = str[0] == '{';
    QObject *obj;
    QDict *args;
    Visitor *v;

    if (is_json) {
        obj = qobject_from_json(str, errp);
        if (!obj) {
            return NULL;
        }
        args = qobject_to(QDict, obj);
        assert(args);
        v = qobject_input_visitor_new(QOBJECT(args));
    } else {
        args = keyval_parse(str, implied_key, NULL, errp);
        if (!args) {
            return NULL;
        }
        v = qobject_input_visitor_new_keyval(QOBJECT(args));
    }
    qobject_unref(args);

    return v;
}
