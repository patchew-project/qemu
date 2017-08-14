/*
 * QAPI util functions
 *
 * Copyright Fujitsu, Inc. 2014
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef QAPI_UTIL_H
#define QAPI_UTIL_H

#include "qapi/qmp/qobject.h"
#include "qapi-types.h"

int qapi_enum_parse(const char * const lookup[], const char *buf,
                    int max, int def, Error **errp);

int parse_qapi_name(const char *name, bool complete);

/**
 * qobject_compare:
 *
 * Compare the value of @a and @b.
 *
 * If @a and @b have the same type and the same value (see list
 * of supported types below), return 0.
 *
 * If @a and @b are both strings, return strcmp(a, b).
 *
 * If @a and @b are numbers, return a negative value if a < b,
 * and a positive value if a > b.
 *
 * Otherwise (if @a and @b are not the same, have different types,
 * are of an unsupported type, or are different), return a non-zero value.
 *
 * Note that this function doesn't support some types, and may
 * return false if the types are unsupported, or if the types don't
 * match exactly.
 *
 * Supported types:
 * - QTYPE_QNULL
 * - QTYPE_QSTRING
 * - QTYPE_QBOOL
 * - QTYPE_QNUM (integers only)
 * - QTYPE_QLIST
 *
 * Unsupported (always return false):
 * - QTYPE_QNUM (non-integer values)
 * - QTYPE_QDICT
 *
 * TODO: rewrite documentation to be clearer.
 * TODO: support non-integer QTYPE_NUM values and QTYPE_QDICT.
 */
int qobject_compare(QObject *a, QObject *b);

#endif
