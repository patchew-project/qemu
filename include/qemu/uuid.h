/*
 *  QEMU UUID functions
 *
 *  Copyright 2016 Red Hat, Inc.,
 *
 *  Authors:
 *   Fam Zheng <famz@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#ifndef QEMU_UUID_H
#define QEMU_UUID_H

#include "qemu-common.h"

/* Version 4 UUID (pseudo random numbers), RFC4122 4.4. */

typedef struct {
    unsigned char data[16];
} QemuUUID;

#define UUID_FMT "%02hhx%02hhx%02hhx%02hhx-" \
                 "%02hhx%02hhx-%02hhx%02hhx-" \
                 "%02hhx%02hhx-" \
                 "%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx"

#define UUID_FMT_LEN 36

#define UUID_NONE "00000000-0000-0000-0000-000000000000"

void qemu_uuid_generate(QemuUUID *out);

int qemu_uuid_is_null(const QemuUUID *uu);

void qemu_uuid_unparse(char *out, const QemuUUID *uuid);

char *qemu_uuid_unparse_strdup(const QemuUUID *uuid);

int qemu_uuid_parse(QemuUUID *uuid, const char *str);

void qemu_uuid_convert(QemuUUID *uuid);

#endif
