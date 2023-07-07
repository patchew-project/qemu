/*
 * QEMU Guest Agent VSS debug declarations
 *
 * Copyright (C) 2023 Red Hat Inc
 *
 * Authors:
 *  Konstantin Kostiuk <kkostiuk@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "vss-common.h"

void G_GNUC_PRINTF(2, 3)
qga_debug_internal(const char *funcname, const char *fmt, ...)  {
    char user_sting[512] = {0};
    char full_string[640] = {0};

    va_list args;
    va_start(args, fmt);
    vsnprintf(user_sting, 512, fmt, args);
    va_end(args);

    snprintf(full_string, 640, QGA_PROVIDER_NAME "[%lu]: %s %s\n",
             GetCurrentThreadId(), funcname, user_sting);

    OutputDebugString(full_string);
    fprintf(stderr, "%s", full_string);
}
