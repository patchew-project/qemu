/*
 * Guest code setup for migration tests
 *
 * Copyright (c) 2016-2018 Red Hat, Inc. and/or its affiliates
 *   based on the vhost-user-test.c that is:
 *      Copyright (c) 2014 Virtual Open Systems Sarl.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "framework.h"
#include "migration-util.h"

/*
 * The boot file modifies memory area in [start_address, end_address)
 * repeatedly. It outputs a 'B' at a fixed rate while it's still running.
 */
#include "bootfile.h"
#include "i386/a-b-bootblock.h"
#include "aarch64/a-b-kernel.h"
#include "loongarch64/a-b-kernel.h"
#include "ppc64/a-b-kernel.h"
#include "s390x/a-b-bios.h"

unsigned start_address;
unsigned end_address;

static char *bootpath;

void bootfile_delete(void)
{
    if (!bootpath) {
        return;
    }
    unlink(bootpath);
    g_free(bootpath);
    bootpath = NULL;
}

char *bootfile_create(const char *arch, const char *dir, bool suspend_me)
{
    unsigned char *content;
    size_t len;

    bootfile_delete();
    bootpath = g_strdup_printf("%s/bootsect", dir);
    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        /* the assembled x86 boot sector should be exactly one sector large */
        g_assert(sizeof(x86_bootsect) == 512);
        x86_bootsect[SYM_suspend_me - SYM_start] = suspend_me;
        content = x86_bootsect;
        len = sizeof(x86_bootsect);
        start_address = X86_TEST_MEM_START;
        end_address = X86_TEST_MEM_END;
    } else if (g_str_equal(arch, "s390x")) {
        content = s390x_elf;
        len = sizeof(s390x_elf);
        start_address = S390_TEST_MEM_START;
        end_address = S390_TEST_MEM_END;
    } else if (strcmp(arch, "ppc64") == 0) {
        content = ppc64_kernel;
        len = sizeof(ppc64_kernel);
        start_address = PPC_TEST_MEM_START;
        end_address = PPC_TEST_MEM_END;
    } else if (strcmp(arch, "aarch64") == 0) {
        content = aarch64_kernel;
        len = sizeof(aarch64_kernel);
        g_assert(sizeof(aarch64_kernel) <= ARM_TEST_MAX_KERNEL_SIZE);
        start_address = ARM_TEST_MEM_START;
        end_address = ARM_TEST_MEM_END;
    } else if (strcmp(arch, "loongarch64") == 0) {
        content = loongarch64_kernel;
        len = sizeof(loongarch64_kernel);
        start_address = LOONGARCH_TEST_MEM_START;
        end_address = LOONGARCH_TEST_MEM_END;
    } else {
        g_assert_not_reached();
    }

    FILE *bootfile = fopen(bootpath, "wb");

    g_assert_cmpint(fwrite(content, len, 1, bootfile), ==, 1);
    fclose(bootfile);

    return bootpath;
}

char *bootfile_get_opts(const char *arch, char **mem_size)
{
    const char *memory_size;
    char *boot;

    if (g_str_equal(arch, "i386") || g_str_equal(arch, "x86_64")) {
        memory_size = "150M";
        boot = g_strdup_printf(
            "-drive if=none,id=d0,file=%s,format=raw "
            "-device ide-hd,drive=d0,secs=1,cyls=1,heads=1", bootpath);

    } else if (g_str_equal(arch, "s390x")) {
        memory_size = "128M";
        boot = g_strdup_printf("-bios %s", bootpath);

    } else if (g_str_equal(arch, "ppc64")) {
        memory_size = "256M";
        boot = g_strdup_printf("-bios %s", bootpath);

    } else if (g_str_equal(arch, "aarch64")) {
        memory_size = "150M";
        boot = g_strdup_printf("-kernel %s", bootpath);

    } else if (g_str_equal(arch, "loongarch64")) {
        memory_size = "256M";
        boot = g_strdup_printf("-bios %s", bootpath);
    } else {
        g_assert_not_reached();
    }

    *mem_size = g_strdup(memory_size);
    return boot;
}

void migrate_wait_for_dirty_mem(QTestState *from, QTestState *to)
{
    uint64_t watch_address = start_address + MAGIC_OFFSET_BASE;
    uint64_t marker_address = start_address + MAGIC_OFFSET;
    uint8_t watch_byte;

    /*
     * Wait for the MAGIC_MARKER to get transferred, as an
     * indicator that a migration pass has made some known
     * amount of progress.
     */
    do {
        usleep(1000 * 10);
    } while (qtest_readq(to, marker_address) != MAGIC_MARKER);

    /* If suspended, src only iterates once, and watch_byte may never change */
    if (get_src()->suspend_me) {
        return;
    }

    /*
     * Now ensure that already transferred bytes are
     * dirty again from the guest workload. Note the
     * guest byte value will wrap around and by chance
     * match the original watch_byte. This is harmless
     * as we'll eventually see a different value if we
     * keep watching
     */
    watch_byte = qtest_readb(from, watch_address);
    do {
        usleep(1000 * 10);
    } while (qtest_readb(from, watch_address) == watch_byte);
}

void migrate_prepare_for_dirty_mem(QTestState *from)
{
    /*
     * The guest workflow iterates from start_address to
     * end_address, writing 1 byte every TEST_MEM_PAGE_SIZE
     * bytes.
     *
     * IOW, if we write to mem at a point which is NOT
     * a multiple of TEST_MEM_PAGE_SIZE, our write won't
     * conflict with the migration workflow.
     *
     * We put in a marker here, that we'll use to determine
     * when the data has been transferred to the dst.
     */
    qtest_writeq(from, start_address + MAGIC_OFFSET, MAGIC_MARKER);
}

void migrate_check_guests_ram(QTestState *who)
{
    /*
     * Our ASM test will have been incrementing one byte from each page from
     * start_address to < end_address in order. This gives us a constraint
     * that any page's byte should be equal or less than the previous pages
     * byte (mod 256); and they should all be equal except for one transition
     * at the point where we meet the incrementer. (We're running this with
     * the guest stopped).
     */
    unsigned address;
    uint8_t first_byte;
    uint8_t last_byte;
    bool hit_edge = false;
    int bad = 0;
    unsigned char dest_byte_a, dest_byte_b, dest_byte_c, dest_byte_d;

    qtest_memread(who, start_address, &dest_byte_a, 1);

    /* Destination still running, wait for a byte to change */
    do {
        qtest_memread(who, start_address, &dest_byte_b, 1);
        usleep(1000 * 10);
    } while (dest_byte_a == dest_byte_b);

    qtest_qmp_assert_success(who, "{ 'execute' : 'stop'}");

    /* With it stopped, check nothing changes */
    qtest_memread(who, start_address, &dest_byte_c, 1);
    usleep(1000 * 200);
    qtest_memread(who, start_address, &dest_byte_d, 1);
    g_assert_cmpint(dest_byte_c, ==, dest_byte_d);

    qtest_memread(who, start_address, &first_byte, 1);
    last_byte = first_byte;

    for (address = start_address + TEST_MEM_PAGE_SIZE; address < end_address;
         address += TEST_MEM_PAGE_SIZE)
    {
        uint8_t b;
        qtest_memread(who, address, &b, 1);
        if (b != last_byte) {
            if (((b + 1) % 256) == last_byte && !hit_edge) {
                /*
                 * This is OK, the guest stopped at the point of
                 * incrementing the previous page but didn't get
                 * to us yet.
                 */
                hit_edge = true;
                last_byte = b;
            } else {
                bad++;
                if (bad <= 10) {
                    fprintf(stderr, "Memory content inconsistency at %x"
                            " first_byte = %x last_byte = %x current = %x"
                            " hit_edge = %x\n",
                            address, first_byte, last_byte, b, hit_edge);
                }
            }
        }
    }
    if (bad >= 10) {
        fprintf(stderr, "and in another %d pages", bad - 10);
    }
    g_assert(bad == 0);
}
