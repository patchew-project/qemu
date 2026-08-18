/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QAPI_TYPE_INFO_H
#define QAPI_TYPE_INFO_H

#include "qapi/util.h"

/**
 * QAPITypeInfo - QAPI type metadata
 *
 * @name: QAPI type name (e.g. "str", "OnOffAuto", "int32List").
 *        QOM uses this as the property type string.
 * @masked_name: Name of the type in the QAPI introspection schema, assigned by
 *        scripts/qapi code generator (e.g. "368").
 *        NULL for implicit types.
 * @lookup: QEnumLookup for this type, or NULL for non-enum types.
 *        visit_type_enum(), qapi_enum_parse(), and qapi_enum_lookup()
 *        all rely on it.
 * @list: The list-type counterpart, or NULL if none exists.
 *        str_type_info.list points to strList_type_info.  qdev array
 *        properties follow this to find the list type for their element.
 */
typedef struct QAPITypeInfo {
    const char *name;
    const char *masked_name;
    const QEnumLookup *lookup;
    const struct QAPITypeInfo *list;
} QAPITypeInfo;

#endif /* QAPI_TYPE_INFO_H */
