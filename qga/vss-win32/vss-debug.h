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

#include <vss-handles.h>

#ifndef VSS_DEBUG_H
#define VSS_DEBUG_H

#define PRINT_DEBUG(fmt, ...) {                                               \
    char user_sting[512] = { 0 };                                             \
    char full_string[640] = { 0 };                                            \
    snprintf(user_sting, 512, fmt, ## __VA_ARGS__);                           \
    snprintf(full_string, 640, QGA_PROVIDER_NAME"[%lu]: %s %s\n",             \
        GetCurrentThreadId(), __func__, user_sting);                          \
    OutputDebugString(full_string);                                           \
    fprintf(stderr, "%s", full_string);                                       \
}

#define PRINT_DEBUG_BEGIN PRINT_DEBUG("begin")
#define PRINT_DEBUG_END PRINT_DEBUG("end")

#endif
