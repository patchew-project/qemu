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

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/uuid.h"
#include <glib.h>

void qemu_uuid_generate(qemu_uuid_t out)
{
    /* Version 4 UUID, RFC4122 4.4. */
    QEMU_BUILD_BUG_ON(sizeof(qemu_uuid_t) != 16);
    *((guint32 *)out + 0) = g_random_int();
    *((guint32 *)out + 1) = g_random_int();
    *((guint32 *)out + 2) = g_random_int();
    *((guint32 *)out + 3) = g_random_int();
    out[7] = (out[7] & 0xf) | 0x40;
    out[8] = (out[8] & 0x3f) | 0x80;
}

int qemu_uuid_is_null(const qemu_uuid_t uu)
{
    qemu_uuid_t null_uuid = { 0 };
    return memcmp(uu, null_uuid, sizeof(qemu_uuid_t)) == 0;
}

void qemu_uuid_unparse(const qemu_uuid_t uu, char *out)
{
    snprintf(out, 37, UUID_FMT,
             uu[0], uu[1], uu[2], uu[3], uu[4], uu[5], uu[6], uu[7],
             uu[8], uu[9], uu[10], uu[11], uu[12], uu[13], uu[14], uu[15]);
}

int qemu_uuid_parse(const char *str, uint8_t *uuid)
{
    int ret;

    if (strlen(str) != 36) {
        return -1;
    }

    ret = sscanf(str, UUID_FMT, &uuid[0], &uuid[1], &uuid[2], &uuid[3],
                 &uuid[4], &uuid[5], &uuid[6], &uuid[7], &uuid[8], &uuid[9],
                 &uuid[10], &uuid[11], &uuid[12], &uuid[13], &uuid[14],
                 &uuid[15]);

    if (ret != 16) {
        return -1;
    }
    return 0;
}
