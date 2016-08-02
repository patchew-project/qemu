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

typedef unsigned char qemu_uuid_t[16];

#define UUID_FMT "%02hhx%02hhx%02hhx%02hhx-" \
                 "%02hhx%02hhx-%02hhx%02hhx-" \
                 "%02hhx%02hhx-" \
                 "%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx"
#define UUID_NONE "00000000-0000-0000-0000-000000000000"

void qemu_uuid_generate(qemu_uuid_t out);

int qemu_uuid_is_null(const qemu_uuid_t uu);

void qemu_uuid_unparse(const qemu_uuid_t uu, char *out);

int qemu_uuid_parse(const char *str, uint8_t *uuid);

#endif
