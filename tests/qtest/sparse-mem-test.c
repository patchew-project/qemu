/*
 * QTest testcases for the sparse memory device
 *
 * Copyright Red Hat Inc., 2021
 *
 * Authors:
 *  Alexander Bulekov   <alxndr@bu.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "libqos/libqtest.h"

static void test_sparse_memwrite(void)
{
    QTestState *s;
    uint8_t *buf;
    const int bufsize = 0x10000;

    s = qtest_init("-device sparse-mem");

    buf = malloc(bufsize);

    for (int i = 0; i < bufsize; i++) {
        buf[i] = (uint8_t)i;
    }
    qtest_memwrite(s, 0x100000000, buf, bufsize);
    memset(buf, 0, bufsize);
    qtest_memread(s, 0x100000000, buf, bufsize);

    for (int i = 0; i < bufsize; i++) {
        assert(buf[i] == (uint8_t)i);
    }

    free(buf);
    qtest_quit(s);
}

static void test_sparse_int_writes(void)
{
    QTestState *s;
    const int num_writes = 0x1000;

    s = qtest_init("-device sparse-mem");

    size_t addr = 0x10000000;
    for (uint64_t i = 0; i < num_writes; i++) {
        qtest_writeq(s, addr, i);
        addr += sizeof(uint64_t);
    }

    addr = 0x10000000;
    for (uint64_t i = 0; i < num_writes; i++) {
        assert(qtest_readq(s, addr) == i);
        addr += sizeof(uint64_t);
    }

    addr = 0x10000002;
    for (uint64_t i = 0; i < num_writes; i++) {
        qtest_writeq(s, addr, i);
        addr += sizeof(uint64_t);
    }

    addr = 0x10000002;
    for (uint64_t i = 0; i < num_writes; i++) {
        assert(qtest_readq(s, addr) == i);
        addr += sizeof(uint64_t);
    }

    qtest_quit(s);
}

int main(int argc, char **argv)
{
    const char *arch = qtest_get_arch();

    g_test_init(&argc, &argv, NULL);

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        qtest_add_func("sparse-mem/memwrite", test_sparse_memwrite);
        qtest_add_func("sparse-mem/ints", test_sparse_int_writes);
    }

    return g_test_run();
}
