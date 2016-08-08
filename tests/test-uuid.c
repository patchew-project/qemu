/*
 * QEMU UUID Library
 *
 * Copyright (c) 2016 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "qemu/uuid.h"

struct { const char *uuidstr; QemuUUID uuid; } uuid_test_data[] = {
    {
        "586ece27-7f09-41e0-9e74-e901317e9d42",
        {0x58, 0x6e, 0xce, 0x27, 0x7f, 0x09, 0x41, 0xe0,
         0x9e, 0x74, 0xe9, 0x01, 0x31, 0x7e, 0x9d, 0x42},
    }, {
        "0cc6c752-3961-4028-a286-c05cc616d396",
        {0x0c, 0xc6, 0xc7, 0x52, 0x39, 0x61, 0x40, 0x28,
         0xa2, 0x86, 0xc0, 0x5c, 0xc6, 0x16, 0xd3, 0x96}
    }, {
        "00000000-0000-0000-0000-000000000000",
        { 0 },
    }
};

static inline bool uuid_is_valid(QemuUUID uuid)
{
    return qemu_uuid_is_null(uuid) ||
            ((uuid[6] & 0xf0) == 0x40 && (uuid[8] & 0xc0) == 0x80);
}

static void test_uuid_generate(void)
{
    QemuUUID uuid;
    int i;

    for (i = 0; i < 100; ++i) {
        qemu_uuid_generate(uuid);
        g_assert(uuid_is_valid(uuid));
    }
}

static void test_uuid_parse(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(uuid_test_data); i++) {
        QemuUUID uuid;

        qemu_uuid_parse(uuid_test_data[i].uuidstr, uuid);
        g_assert_cmpmem(uuid_test_data[i].uuid, sizeof(uuid),
                        uuid, sizeof(uuid));
        g_assert(uuid_is_valid(uuid));
    }
}

static void test_uuid_unparse(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(uuid_test_data); i++) {
        char out[strlen(UUID_FMT)];

        qemu_uuid_unparse(uuid_test_data[i].uuid, out);
        g_assert_cmpmem(uuid_test_data[i].uuidstr, UUID_FMT_LEN,
                        out, UUID_FMT_LEN);
    }
}

static void test_uuid_is_null(void)
{
    QemuUUID uuid_null = { 0 };
    QemuUUID uuid_not_null = {
        0x58, 0x6e, 0xce, 0x27, 0x7f, 0x09, 0x41, 0xe0,
        0x9e, 0x74, 0xe9, 0x01, 0x31, 0x7e, 0x9d, 0x42
    };
    QemuUUID uuid_not_null_2 = { 1 };

    g_assert(qemu_uuid_is_null(uuid_null));
    g_assert_false(qemu_uuid_is_null(uuid_not_null));
    g_assert_false(qemu_uuid_is_null(uuid_not_null_2));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/uuid/generate",
                    test_uuid_generate);
    g_test_add_func("/uuid/is_null",
                    test_uuid_is_null);
    g_test_add_func("/uuid/parse",
                    test_uuid_parse);
    g_test_add_func("/uuid/unparse",
                    test_uuid_unparse);

    return g_test_run();
}
