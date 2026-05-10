/*
 * QAPITypeInfo — associates a QOM property with its QAPI schema type.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QAPI_TYPE_INFO_H
#define QAPI_TYPE_INFO_H

#include "qapi/util.h"

typedef struct QAPITypeInfo {
    const char *name;
    const char *schema_name;
    const QEnumLookup *lookup;
    const struct QAPITypeInfo *list;
} QAPITypeInfo;

#endif /* QAPI_TYPE_INFO_H */
