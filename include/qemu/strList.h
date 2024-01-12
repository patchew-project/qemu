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
 * Break @in into a strList using the delimiter string @delim.
 * The delimiter is not included in the result.
 * Return NULL if @in is NULL or an empty string.
 * A leading, trailing, or consecutive delimiter produces an
 * empty string at that position in the output.
 * All strings are g_strdup'd, and the result can be freed
 * using qapi_free_strList.
 */
strList *strList_from_string(const char *in, const char *delim);

#endif
