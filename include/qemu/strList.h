/*
 * Copyright (c) 2022 - 2024 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_STR_LIST_H
#define QEMU_STR_LIST_H

#include "qapi/qapi-builtin-types.h"

/*
 * Split @str into a strList using the delimiter string @delim.
 * The delimiter is not included in the result.
 * Return NULL if @str is NULL or an empty string.
 * A leading, trailing, or consecutive delimiter produces an
 * empty string at that position in the output.
 * All strings are g_strdup'd, and the result can be freed
 * using qapi_free_strList.
 */
strList *str_split(const char *str, const char *delim);

/*
 * Produce and return a NULL-terminated array of strings from @list.
 * The result is g_malloc'd and all strings are g_strdup'd.
 */
char **strv_from_strList(const strList *list);

#endif
