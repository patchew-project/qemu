/*
 * Copyright (c) 2023 Red Hat, Inc.
 * Copyright (c) 2022 - 2024 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/strList.h"

strList *str_split(const char *str, const char *delim)
{
    g_autofree char **split = g_strsplit(str ?: "", delim, -1);
    strList *res = NULL;
    strList **tail = &res;
    int i;

    for (i = 0; split[i]; i++) {
        QAPI_LIST_APPEND(tail, split[i]);
    }

    return res;
}

char **strv_from_strList(const strList *list)
{
    const strList *tail;
    int i = 0;
    char **argv = g_new(char *, QAPI_LIST_LENGTH(list) + 1);

    for (tail = list; tail != NULL; tail = tail->next) {
        argv[i++] = g_strdup(tail->value);
    }
    argv[i] = NULL;

    return argv;
}
