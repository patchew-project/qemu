/*
 * Backtrace abstraction to gloss over the differences between architectures.
 *
 * Copyright (c) 2022 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/backtrace.h"
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define BT_BUF_SIZE 128

GString *qemu_backtrace(int max)
{
    int nptrs;
    void *buffer[BT_BUF_SIZE];
    char **strings;
    GString *res = g_string_new("");

    nptrs = backtrace(buffer, BT_BUF_SIZE);
    strings = backtrace_symbols(buffer, nptrs);
    if (strings == NULL) {
        g_string_printf(res, "Failed to extract symbols");
    } else {
        for (int j = 0; j < MIN(max, nptrs); j++) {
            g_string_append_printf(res, "%s\n", strings[j]);
        }
        free(strings);
    }

    return res;
}
