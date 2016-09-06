/*
 * Drive mirror unit-tests.
 *
 * Copyright (c) 2014 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * Authors:
 *  Jie Wang <wangjie88@huawei.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <glib.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include "libqtest.h"

#define TEST_IMAGE_SIZE         (10 * 1014 * 1024)
#define PCI_SLOT                0x04
#define PCI_FN                  0x00

static char *drive_create(void)
{
    int fd, ret;
    char *tmp_path = g_strdup("/tmp/qtest-src-mirror.XXXXXX");

    /* Create a temporary raw image */
    fd = mkstemp(tmp_path);
    g_assert_cmpint(fd, >=, 0);
    ret = ftruncate(fd, TEST_IMAGE_SIZE);
    g_assert_cmpint(ret, ==, 0);
    close(fd);

    return tmp_path;
}

static void mirror_test_start(void)
{
    char *cmdline;
    char *tmp_path;

    tmp_path = drive_create();

    cmdline = g_strdup_printf("-drive if=none,id=drive0,file=%s,format=raw "
                              "-device virtio-blk-pci,id=drv0,drive=drive0,"
                              "addr=%x.%x",
                              tmp_path, PCI_SLOT, PCI_FN);

    qtest_start(cmdline);
    unlink(tmp_path);
    g_free(tmp_path);
    g_free(cmdline);
}

static void test_mirror_base(void)
{
    QDict *response;

    mirror_test_start();

    response = qmp("{\"execute\": \"drive-mirror\","
                   " \"arguments\": {"
                   "   \"device\": \"drive0\","
                   "   \"target\": \"/tmp/qtest-dest-mirror\","
                   "   \"sync\": \"full\","
                   "   \"mode\": \"absolute-paths\","
                   "   \"format\": \"raw\""
                   "}}");

    g_assert(response);
    g_assert(!qdict_haskey(response, "error"));
    QDECREF(response);

    qtest_end();
}

int main(int argc, char **argv)
{
    int ret;
    const char *arch = qtest_get_arch();

    g_test_init(&argc, &argv, NULL);

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        qtest_add_func("/mirror/mirror_base", test_mirror_base);
    } else if (strcmp(arch, "arm") == 0) {
        g_test_message("Skipping test for non-x86\n");
        return 0;
    }

    ret = g_test_run();

    return ret;
}
