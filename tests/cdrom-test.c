/*
 * Various tests for emulated CD-ROM drives.
 *
 * Copyright (c) 2018 Red Hat Inc.
 *
 * Author:
 *    Thomas Huth <thuth@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2
 * or later. See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "boot-sector.h"
#include "qapi/qmp/qdict.h"

static char isoimage[] = "cdrom-boot-iso-XXXXXX";

static int gen_iso(const char *fmt, ...)
{
    char *params, *command;
    va_list args;
    pid_t pid;
    int status;

    pid = fork();
    if (pid == 0) {
        va_start(args, fmt);
        params = g_strdup_vprintf(fmt, args);
        va_end(args);
        command = g_strdup_printf("exec genisoimage %s", params);
        g_free(params);
        execlp("/bin/sh", "sh", "-c", command, NULL);
        exit(1);
    }
    wait(&status);

    return WEXITSTATUS(status);
}

static int prepare_image(const char *arch, char *isoimage)
{
    char srcdir[] = "cdrom-test-dir-XXXXXX";
    char *codefile = NULL;
    int ifh, ret = -1;

    ifh = mkstemp(isoimage);
    if (ifh < 0) {
        perror("Error creating temporary iso image file");
        return -1;
    }
    if (!mkdtemp(srcdir)) {
        perror("Error creating temporary directory");
        goto cleanup;
    }

    if (g_str_equal(arch, "i386") || g_str_equal(arch, "x86_64") ||
        g_str_equal(arch, "s390x")) {
        codefile = g_strdup_printf("%s/bootcode-XXXXXX", srcdir);
        ret = boot_sector_init(codefile);
        if (ret) {
            goto cleanup;
        }
    } else {
        /* Just create a dummy file */
        char txt[] = "empty disc";
        codefile = g_strdup_printf("%s/readme.txt", srcdir);
        if (!g_file_set_contents(codefile, txt, sizeof(txt) - 1, NULL)) {
            fprintf(stderr, "Failed to create '%s'\n", codefile);
            goto cleanup;
        }
    }

    ret = gen_iso("-quiet -l -no-emul-boot -b %s -o %s %s",
                  strrchr(codefile, '/') + 1, isoimage, srcdir);
    if (ret) {
        fprintf(stderr, "genisoimage failed: %i\n", ret);
    }

    unlink(codefile);

cleanup:
    g_free(codefile);
    rmdir(srcdir);
    close(ifh);

    return ret;
}

/**
 * Check that at least the -cdrom parameter is basically working, i.e. we can
 * see the filename of the ISO image in the output of "info block" afterwards
 */
static void test_cdrom_param(gconstpointer data)
{
    QTestState *qts;
    char *resp;

    qts = qtest_startf("-M %s -cdrom %s", (const char *)data, isoimage);
    resp = qtest_hmp(qts, "info block");
    g_assert(strstr(resp, isoimage) != 0);
    g_free(resp);
    qtest_quit(qts);
}

static void add_cdrom_param_tests(const char **machines)
{
    while (*machines) {
        char *testname = g_strdup_printf("cdrom/param/%s", *machines);
        qtest_add_data_func(testname, *machines, test_cdrom_param);
        g_free(testname);
        machines++;
    }
}

static void test_cdboot(gconstpointer data)
{
    QTestState *qts;

    qts = qtest_startf("-accel kvm:tcg -no-shutdown %s%s", (const char *)data,
                       isoimage);
    boot_sector_test(qts);
    qtest_quit(qts);
}

static void add_x86_tests(void)
{
    qtest_add_data_func("cdboot/default", "-cdrom ", test_cdboot);
    qtest_add_data_func("cdboot/virtio-scsi",
                        "-device virtio-scsi -device scsi-cd,drive=cdr "
                        "-blockdev file,node-name=cdr,filename=", test_cdboot);
    qtest_add_data_func("cdboot/isapc", "-M isapc "
                        "-drive if=ide,media=cdrom,file=", test_cdboot);
    qtest_add_data_func("cdboot/am53c974",
                        "-device am53c974 -device scsi-cd,drive=cd1 "
                        "-drive if=none,id=cd1,format=raw,file=", test_cdboot);
    qtest_add_data_func("cdboot/dc390",
                        "-device dc390 -device scsi-cd,drive=cd1 "
                        "-blockdev file,node-name=cd1,filename=", test_cdboot);
    qtest_add_data_func("cdboot/lsi53c895a",
                        "-device lsi53c895a -device scsi-cd,drive=cd1 "
                        "-blockdev file,node-name=cd1,filename=", test_cdboot);
    qtest_add_data_func("cdboot/megasas", "-M q35 "
                        "-device megasas -device scsi-cd,drive=cd1 "
                        "-blockdev file,node-name=cd1,filename=", test_cdboot);
    qtest_add_data_func("cdboot/megasas-gen2", "-M q35 "
                        "-device megasas-gen2 -device scsi-cd,drive=cd1 "
                        "-blockdev file,node-name=cd1,filename=", test_cdboot);
}

static void add_s390x_tests(void)
{
    qtest_add_data_func("cdboot/default", "-cdrom ", test_cdboot);
    qtest_add_data_func("cdboot/virtio-scsi",
                        "-device virtio-scsi -device scsi-cd,drive=cdr "
                        "-blockdev file,node-name=cdr,filename=", test_cdboot);
}

int main(int argc, char **argv)
{
    int ret;
    const char *arch = qtest_get_arch();

    g_test_init(&argc, &argv, NULL);

    if (gen_iso("--version > /dev/null")) {
        /* genisoimage not available - so can't run tests */
        return 0;
    }

    ret = prepare_image(arch, isoimage);
    if (ret) {
        return ret;
    }

    if (g_str_equal(arch, "i386") || g_str_equal(arch, "x86_64")) {
        add_x86_tests();
    } else if (g_str_equal(arch, "s390x")) {
        add_s390x_tests();
    } else if (g_str_equal(arch, "ppc64")) {
        const char *ppcmachines[] = {
            "pseries", "mac99", "g3beige", "40p", "prep", NULL
        };
        add_cdrom_param_tests(ppcmachines);
    } else if (g_str_equal(arch, "sparc")) {
        const char *sparcmachines[] = {
            "LX", "SPARCClassic", "SPARCbook", "SS-10", "SS-20", "SS-4",
            "SS-5", "SS-600MP", "Voyager", "leon3_generic", NULL
        };
        add_cdrom_param_tests(sparcmachines);
    } else if (g_str_equal(arch, "sparc64")) {
        const char *sparc64machines[] = {
            "niagara", "sun4u", "sun4v", NULL
        };
        add_cdrom_param_tests(sparc64machines);
    } else if (!strncmp(arch, "mips64", 6)) {
        const char *mips64machines[] = {
            "magnum", "malta", "mips", "mipssim", "pica61", NULL
        };
        add_cdrom_param_tests(mips64machines);
    } else if (g_str_equal(arch, "aarch64")) {
        const char *aarch64machines[] = {
            "realview-eb", "realview-eb-mpcore", "realview-pb-a8",
            "realview-pbx-a9", "versatileab", "versatilepb", "vexpress-a15",
            "vexpress-a9", "virt", NULL
        };
        add_cdrom_param_tests(aarch64machines);
    }

    ret = g_test_run();

    unlink(isoimage);

    return ret;
}
