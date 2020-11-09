/*
 * QNum Module
 *
 * Copyright (C) 2009 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *  Anthony Liguori <aliguori@us.ibm.com>
 *  Marc-André Lureau <marcandre.lureau@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/qmp/qnum.h"

QNum *qnum_from_value(QNumValue value)
{
    QNum *qn = g_new(QNum, 1);

    qobject_init(QOBJECT(qn), QTYPE_QNUM);
    qn->value = value;
    return qn;
}

/**
 * qnum_from_int(): Create a new QNum from an int64_t
 * @value: int64_t value
 *
 * Return strong reference.
 */
QNum *qnum_from_int(int64_t value)
{
    return qnum_from_value((QNumValue) QNUM_VAL_INT(value));
}

/**
 * qnum_from_uint(): Create a new QNum from an uint64_t
 * @value: uint64_t value
 *
 * Return strong reference.
 */
QNum *qnum_from_uint(uint64_t value)
{
    return qnum_from_value((QNumValue) QNUM_VAL_UINT(value));
}

/**
 * qnum_from_double(): Create a new QNum from a double
 * @value: double value
 *
 * Return strong reference.
 */
QNum *qnum_from_double(double value)
{
    return qnum_from_value((QNumValue) QNUM_VAL_DOUBLE(value));
}

/**
 * qnum_get_try_int(): Get an integer representation of the number
 * @qn: QNum object
 * @val: pointer to value
 *
 * Return true on success.
 */
bool qnum_get_try_int(const QNum *qn, int64_t *val)
{
    const QNumValue *qv = &qn->value;

    switch (qv->kind) {
    case QNUM_I64:
        *val = qv->u.i64;
        return true;
    case QNUM_U64:
        if (qv->u.u64 > INT64_MAX) {
            return false;
        }
        *val = qv->u.u64;
        return true;
    case QNUM_DOUBLE:
        return false;
    }

    assert(0);
    return false;
}

/**
 * qnum_get_int(): Get an integer representation of the number
 * @qn: QNum object
 *
 * assert() on failure.
 */
int64_t qnum_get_int(const QNum *qn)
{
    int64_t val;
    bool success = qnum_get_try_int(qn, &val);
    assert(success);
    return val;
}

/**
 * qnum_value_get_try_uint(): Get an unsigned integer from the number
 * @qn: QNum object
 * @val: pointer to value
 *
 * Return true on success.
 */
bool qnum_get_try_uint(const QNum *qn, uint64_t *val)
{
    const QNumValue *qv = &qn->value;

    switch (qv->kind) {
    case QNUM_I64:
        if (qv->u.i64 < 0) {
            return false;
        }
        *val = qv->u.i64;
        return true;
    case QNUM_U64:
        *val = qv->u.u64;
        return true;
    case QNUM_DOUBLE:
        return false;
    }

    assert(0);
    return false;
}

/**
 * qnum_get_uint(): Get an unsigned integer from the number
 * @qn: QNum object
 *
 * assert() on failure.
 */
uint64_t qnum_get_uint(const QNum *qn)
{
    uint64_t val;
    bool success = qnum_get_try_uint(qn, &val);
    assert(success);
    return val;
}

/**
 * qnum_get_double(): Get a float representation of the number
 * @qn: QNum object
 *
 * qnum_get_double() loses precision for integers beyond 53 bits.
 */
double qnum_get_double(const QNum *qn)
{
    const QNumValue *qv = &qn->value;

    switch (qv->kind) {
    case QNUM_I64:
        return qv->u.i64;
    case QNUM_U64:
        return qv->u.u64;
    case QNUM_DOUBLE:
        return qv->u.dbl;
    }

    assert(0);
    return 0.0;
}

char *qnum_to_string(QNum *qn)
{
    const QNumValue *qv = &qn->value;
    char *buffer;
    int len;

    switch (qv->kind) {
    case QNUM_I64:
        return g_strdup_printf("%" PRId64, qv->u.i64);
    case QNUM_U64:
        return g_strdup_printf("%" PRIu64, qv->u.u64);
    case QNUM_DOUBLE:
        /* FIXME: snprintf() is locale dependent; but JSON requires
         * numbers to be formatted as if in the C locale. Dependence
         * on C locale is a pervasive issue in QEMU. */
        /* FIXME: This risks printing Inf or NaN, which are not valid
         * JSON values. */
        /* FIXME: the default precision of 6 for %f often causes
         * rounding errors; we should be using DBL_DECIMAL_DIG (17),
         * and only rounding to a shorter number if the result would
         * still produce the same floating point value.  */
        buffer = g_strdup_printf("%f" , qv->u.dbl);
        len = strlen(buffer);
        while (len > 0 && buffer[len - 1] == '0') {
            len--;
        }

        if (len && buffer[len - 1] == '.') {
            buffer[len - 1] = 0;
        } else {
            buffer[len] = 0;
        }

        return buffer;
    }

    assert(0);
    return NULL;
}

/**
 * qnum_is_equal(): Test whether the two QNums are equal
 * @x: QNum object
 * @y: QNum object
 *
 * Negative integers are never considered equal to unsigned integers,
 * but positive integers in the range [0, INT64_MAX] are considered
 * equal independently of whether the QNum's kind is i64 or u64.
 *
 * Doubles are never considered equal to integers.
 */
bool qnum_is_equal(const QObject *x, const QObject *y)
{
    const QNum *qnum_x = qobject_to(QNum, x);
    const QNum *qnum_y = qobject_to(QNum, y);
    const QNumValue *num_x = &qnum_x->value;
    const QNumValue *num_y = &qnum_y->value;

    switch (num_x->kind) {
    case QNUM_I64:
        switch (num_y->kind) {
        case QNUM_I64:
            /* Comparison in native int64_t type */
            return num_x->u.i64 == num_y->u.i64;
        case QNUM_U64:
            /* Implicit conversion of x to uin64_t, so we have to
             * check its sign before */
            return num_x->u.i64 >= 0 && num_x->u.i64 == num_y->u.u64;
        case QNUM_DOUBLE:
            return false;
        }
        abort();
    case QNUM_U64:
        switch (num_y->kind) {
        case QNUM_I64:
            return qnum_is_equal(y, x);
        case QNUM_U64:
            /* Comparison in native uint64_t type */
            return num_x->u.u64 == num_y->u.u64;
        case QNUM_DOUBLE:
            return false;
        }
        abort();
    case QNUM_DOUBLE:
        switch (num_y->kind) {
        case QNUM_I64:
        case QNUM_U64:
            return false;
        case QNUM_DOUBLE:
            /* Comparison in native double type */
            return num_x->u.dbl == num_y->u.dbl;
        }
        abort();
    }

    abort();
}

/**
 * qnum_destroy_obj(): Free all memory allocated by a QNum object
 *
 * @obj: QNum object to be destroyed
 */
void qnum_destroy_obj(QObject *obj)
{
    assert(obj != NULL);
    g_free(qobject_to(QNum, obj));
}
