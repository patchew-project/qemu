/*
 * QEMU Object Model.
 *
 * Based on ideas by Avi Kivity <avi@redhat.com>
 *
 * Copyright (C) 2009, 2015 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 * QObject Reference Counts Terminology
 * ------------------------------------
 *
 *  - Returning references: A function that returns an object may
 *  return it as either a weak or a strong reference.  If the reference
 *  is strong, you are responsible for calling qobject_unref() on
 *  the reference when you are done.
 *
 *  If the reference is weak, the owner of the reference may free it at
 *  any time in the future.  Before storing the reference anywhere, you
 *  should call qobject_ref() to make the reference strong.
 *
 *  - Transferring ownership: when you transfer ownership of a reference
 *  by calling a function, you are no longer responsible for calling
 *  qobject_unref() when the reference is no longer needed.  In other words,
 *  when the function returns you must behave as if the reference to the
 *  passed object was weak.
 */
#ifndef QOBJECT_H
#define QOBJECT_H

#include "qapi/qapi-builtin-types.h"

struct QObject {
    QType type;
    size_t refcnt;
};

/* This function gives an error if an invalid pointer type is passed
 * to QOBJECT.  For optimized builds, we can rely on dead-code
 * elimination from the compiler, and give the errors already at link
 * time.
 */
#if defined(__OPTIMIZE__) && !defined(__SANITIZE_ADDRESS__)
const void * qobject_unknown_type(const void *);
#else
static inline const void *
qobject_unknown_type(const void *unused)
{
    abort();
    return NULL;
}
#endif

/* A typecast, checking for the type of arguments */
/* QObject is at offset 0, for all QObject-derived types */
#define QOBJECT(x) QEMU_GENERIC(x,              \
    (QNull *, (QObject *) x),                   \
    (const QNull *, (const QObject *) x),       \
    (QNum *, (QObject *) x),                    \
    (const QNum *, (const QObject *) x),        \
    (QString *, (QObject *) x),                 \
    (const QString *, (const QObject *) x),     \
    (QDict *, (QObject *) x),                   \
    (const QDict *, (const QObject *) x),       \
    (QList *, (QObject *) x),                   \
    (const QList *, (const QObject *) x),       \
    (QBool *, (QObject *) x),                   \
    (const QBool *, (const QObject *) x),       \
    (QObject *, x),                             \
    (const QObject *, x),                       \
    qobject_unknown_type(x))

/**
 * qobject_ref(): Increment QObject's reference count
 */
#define qobject_ref(x) qobject_ref_impl(QOBJECT(x))

/**
 * qobject_unref(): Decrement QObject's reference count, deallocate
 * when it reaches zero
 */
#define qobject_unref(x) qobject_unref_impl(QOBJECT(x))

/* Required for qobject_to() */
#define QTYPE_CAST_TO_QNull     QTYPE_QNULL
#define QTYPE_CAST_TO_QNum      QTYPE_QNUM
#define QTYPE_CAST_TO_QString   QTYPE_QSTRING
#define QTYPE_CAST_TO_QDict     QTYPE_QDICT
#define QTYPE_CAST_TO_QList     QTYPE_QLIST
#define QTYPE_CAST_TO_QBool     QTYPE_QBOOL

QEMU_BUILD_BUG_MSG(QTYPE__MAX != 7,
                   "The QTYPE_CAST_TO_* list needs to be extended");

#define qobject_to(type, obj) ({ \
    QObject *_tmp = qobject_check_type(obj, glue(QTYPE_CAST_TO_, type)); \
    _tmp ? container_of(_tmp, type, base) : (type *)NULL; })

/* Initialize an object to default values */
static inline void qobject_init(QObject *obj, QType type)
{
    assert(QTYPE_NONE < type && type < QTYPE__MAX);
    obj->refcnt = 1;
    obj->type = type;
}

static inline void qobject_ref_impl(QObject *obj)
{
    if (obj)
        obj->refcnt++;
}

/**
 * qobject_is_equal(): Return whether the two objects are equal.
 *
 * Any of the pointers may be NULL; return true if both are.  Always
 * return false if only one is (therefore a QNull object is not
 * considered equal to a NULL pointer).
 */
bool qobject_is_equal(const QObject *x, const QObject *y);

/**
 * qobject_destroy(): Free resources used by the object
 */
void qobject_destroy(QObject *obj);

static inline void qobject_unref_impl(QObject *obj)
{
    assert(!obj || obj->refcnt);
    if (obj && --obj->refcnt == 0) {
        qobject_destroy(obj);
    }
}

/**
 * qobject_type(): Return the QObject's type
 */
static inline QType qobject_type(const QObject *obj)
{
    assert(QTYPE_NONE < obj->type && obj->type < QTYPE__MAX);
    return obj->type;
}

/**
 * qobject_check_type(): Helper function for the qobject_to() macro.
 * Return @obj, but only if @obj is not NULL and @type is equal to
 * @obj's type.  Return NULL otherwise.
 */
static inline QObject *qobject_check_type(const QObject *obj, QType type)
{
    if (obj && qobject_type(obj) == type) {
        return (QObject *)obj;
    } else {
        return NULL;
    }
}

#endif /* QOBJECT_H */
