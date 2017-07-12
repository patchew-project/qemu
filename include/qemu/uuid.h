/*
 *  QEMU UUID functions
 *
 *  Copyright 2016 Red Hat, Inc.
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
    union {
        unsigned char data[16];
        struct {
            /* Generated in BE endian, can be swapped with qemu_uuid_bswap. */
            uint32_t time_low;
            uint16_t time_mid;
            uint16_t time_high_and_version;
            uint8_t  clock_seq_and_reserved;
            uint8_t  clock_seq_low;
            uint8_t  node[6];
        } fields;
    };
} QemuUUID;

#define UUID_FMT "%02hhx%02hhx%02hhx%02hhx-" \
                 "%02hhx%02hhx-%02hhx%02hhx-" \
                 "%02hhx%02hhx-" \
                 "%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx"

#define UUID_FMT_LEN 36

#define UUID_NONE "00000000-0000-0000-0000-000000000000"

#define UUID_BE(a, b, c, d0, d1, d2, d3, d4, d5, d6, d7)        \
{{{ ((a) >> 24) & 0xff, ((a) >> 16) & 0xff, ((a) >> 8) & 0xff, (a) & 0xff, \
   ((b) >> 8) & 0xff, (b) & 0xff,                   \
    ((c) >> 8) & 0xff, (c) & 0xff,                    \
    (d0), (d1), (d2), (d3), (d4), (d5), (d6), (d7) } } }

/* Platform Memory, this is from UEFI 2.6 N.2.2 Section Descriptor */
#define UEFI_CPER_SEC_PLATFORM_MEM                   \
    UUID_BE(0xA5BC1114, 0x6F64, 0x4EDE, 0xB8, 0x63, 0x3E, 0x83, \
    0xED, 0x7C, 0x83, 0xB1)

void qemu_uuid_generate(QemuUUID *out);

int qemu_uuid_is_null(const QemuUUID *uu);

void qemu_uuid_unparse(const QemuUUID *uuid, char *out);

char *qemu_uuid_unparse_strdup(const QemuUUID *uuid);

int qemu_uuid_parse(const char *str, QemuUUID *uuid);

void qemu_uuid_bswap(QemuUUID *uuid);

#endif
