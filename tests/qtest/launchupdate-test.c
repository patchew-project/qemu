/*
 * vmlaunchupdate device fwcfg test.
 *
 * Copyright (c) 2026 Red Hat, Inc.
 *
 * Author:
 *   Ani Sinha <anisinha@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqos/libqos-pc.h"
#include "libqtest.h"
#include "standard-headers/linux/qemu_fw_cfg.h"
#include "libqos/fw_cfg.h"
#include "qemu/bswap.h"
#include "hw/misc/vmlaunchupdate.h"

#define WAIT_SEC 10
static bool debug;
static bool confidential;

static void test_vm_launch_update_capability(void)
{
    QFWCFG *fw_cfg;
    QTestState *s;
    VMLaunchUpdate launch_update;
    size_t filesize;
    uint64_t capabilities;

    s = qtest_init("-device vm-launch-update");
    fw_cfg = pc_fw_cfg_init(s);

    filesize = qfw_cfg_get_file(fw_cfg, FILE_VMLAUNCHUPDATE,
                                &launch_update, sizeof(launch_update));
    g_assert_cmpint(filesize, ==, sizeof(launch_update));
    capabilities = le64_to_cpu(launch_update.capabilities);
    g_assert_cmpint(capabilities, ==, VM_LAUNCHUPDATE_FORMAT_IGVM);
    pc_fw_cfg_uninit(fw_cfg);
    qtest_quit(s);
}


static void test_vm_launch_update_disable(void)
{
    QFWCFG *fw_cfg;
    QOSState *qs;
    VMLaunchUpdate launch_update;
    uint64_t control;
    size_t filesize;

    qs = qtest_pc_boot("-device vm-launch-update");

    fw_cfg = pc_fw_cfg_init(qs->qts);

    filesize = qfw_cfg_get_file(fw_cfg, FILE_VMLAUNCHUPDATE,
                                &launch_update, sizeof(launch_update));
    g_assert_cmpint(filesize, ==, sizeof(launch_update));
    control = le64_to_cpu(launch_update.control);
    g_assert_cmpint(VM_LAUNCHUPDATE_CTL_DISABLE & control, ==, 0);

    /* disable the device */
    memset(&launch_update, 0, sizeof(launch_update));
    launch_update.control |= VM_LAUNCHUPDATE_CTL_DISABLE;

    filesize = qfw_cfg_write_file(fw_cfg, qs, FILE_VMLAUNCHUPDATE,
                                  &launch_update, sizeof(launch_update));
    g_assert_cmpint(filesize, ==, sizeof(launch_update));

    /* try to clear the dsable flag */
    memset(&launch_update, 0, sizeof(launch_update));

    filesize = qfw_cfg_write_file(fw_cfg, qs, FILE_VMLAUNCHUPDATE,
                                  &launch_update, sizeof(launch_update));
    g_assert_cmpint(filesize, ==, sizeof(launch_update));

    /* check if the device is still disabled */
    filesize = qfw_cfg_get_file(fw_cfg, FILE_VMLAUNCHUPDATE,
                                &launch_update, sizeof(launch_update));
    g_assert_cmpint(filesize, ==, sizeof(launch_update));
    control = le64_to_cpu(launch_update.control);
    g_assert_cmpint(VM_LAUNCHUPDATE_CTL_DISABLE & control, ==, 1);

    pc_fw_cfg_uninit(fw_cfg);
    qtest_shutdown(qs);
}

static int64_t get_image_size(const char *filename)
{
    int fd;
    int64_t size;
    fd = open(filename, O_RDONLY | O_BINARY);
    g_assert_true(fd > 0);
    size = lseek(fd, 0, SEEK_END);
    close(fd);
    return size;
}

static ssize_t load_image(const char *igvm_f, void **addr, size_t *size)
{
    ssize_t actsize = 0, l = 0;
    int f_igvm_f;
    size_t l_size;

    f_igvm_f = open(igvm_f, O_RDONLY | O_BINARY);
    g_assert_true(f_igvm_f);
    l_size = get_image_size(igvm_f);
    g_assert_true(l_size > 0);
    *addr = g_malloc0(l_size);
    g_assert_true(*addr);

    while (l < l_size) {
        actsize = read(f_igvm_f, *addr + l, 1);
        if (actsize < 0) {
            break;
        }
        l += actsize;
    }

    close(f_igvm_f);
    *size = l_size;
    return actsize < 0 ? -1 : l;
}

static gboolean match_hello_world(char *serial_f, const char *exp_out)
{
    GError *error = NULL;
    g_autofree gchar *f_contents = NULL;
    g_autofree GRegex *regex = NULL;
    g_autofree GMatchInfo *match_info = NULL;
    gsize len;
    gboolean ret;

    ret = g_file_get_contents(serial_f, &f_contents, &len, &error);
    g_assert(ret);
    g_assert_no_error(error);

    regex = g_regex_new(exp_out, G_REGEX_CASELESS, 0, &error);
    g_assert_no_error(error);

    ret = g_regex_match_full(regex, f_contents, -1, 0, 0, &match_info, &error);
    g_assert_no_error(error);

    return ret;
}

static int wait_for_match(char *serial_f,
                          const char *exp_out, int64_t timeout_s)
{
    time_t start, delta;
    int ret = -1;

    start = time(NULL);
    while (1) {
        if (match_hello_world(serial_f, exp_out)) {
            ret = 0;
            break;
        }

        delta = time(NULL) - start;
        if (delta >= timeout_s) {
            fprintf(stderr, "timed out waiting to read serial output\n");
            break;
        }

        /* wait 20 ms before trying again */
        if (false) {
            fprintf(stderr,
                    "sleeping 20 ms before checking serial output again.\n");
        }
        g_usleep(20000);
    }
    return ret;
}

static void test_load_igvm(void)
{
    const char *igvm_f;
    const char *igvm_init;
    int ser_fd;
    g_autofree void *igvm_blob = NULL;
    g_autofree char *serialtmp = NULL;
    const char exp_out[] = "Hello world!";
    const char exp_out2[] = "Test succeeded!";
    const char *snp, *cgs;
    uint64_t gaddr;
    size_t igvm_sz;
    size_t filesize;
    QFWCFG *fw_cfg;
    QOSState *qs;
    VMLaunchUpdate launch_update;

    if (confidential) {
        snp = "-object \'{\"qom-type\":\"sev-snp-guest\",\"id\":\"lsec0\","
            "\"cbitpos\":51,\"reduced-phys-bits\":1,\"policy\":196608}\'";
        cgs = "confidential-guest-support=lsec0";
        /*
         * The following two IGVM files can be built from the source
         * present in https://gitlab.com/anisinha/virt-firmware-rs .
         * Typing 'make' from the top of this repository will build the
         * IGVM files for both confidential and
         * non-confidential tests. The IGVM files for the non-coco
         * case has been checked-in into the QEMU repository for
         * convenience and easy CI pipeline testing.
         */
        igvm_f = "tests/data/igvm/snptest.igvm"; /*
                                                  * this prints 'hello world'
                                                  * on console
                                                  */
        igvm_init = "tests/data/igvm/snptest-nohello.igvm";
    } else {
        igvm_f = "tests/data/igvm/hello.igvm";
        igvm_init = "tests/data/igvm/qemuinit.igvm";
        snp = "";
        cgs = "";
    }

    if (!g_file_test(igvm_f, G_FILE_TEST_EXISTS)) {
        g_test_skip("igvm file bundle does not exist!");
        return;
    }

    ser_fd = g_file_open_tmp("launchupdate-qtest-serial-sXXXXXX",
                             &serialtmp, NULL);
    g_assert_true(ser_fd != -1);

    if (debug) {
        fprintf(stderr, "serial console file is %s\n", serialtmp);
    }

    qs = qtest_pc_boot("-machine q35,igvm-cfg=igvm0,%s -m 1G -accel kvm "
                       "-device vm-launch-update "
                       "-chardev file,id=serial0,path=%s "
                       "-serial chardev:serial0 "
                       "-object igvm-cfg,id=igvm0,file=%s %s",
                       cgs, serialtmp, igvm_init, snp);

    fw_cfg = pc_fw_cfg_init(qs->qts);

    if (debug) {
        fprintf(stderr, "target endianness: %s\n",
                qtest_big_endian(qs->qts) ? "big" : "little");
    }

    g_assert_true(load_image(igvm_f, &igvm_blob, &igvm_sz) == igvm_sz);

    /* create a data buffer in guest memory */
    gaddr = guest_alloc(&qs->alloc, igvm_sz);

    if (debug) {
        fprintf(stderr, "guest paddr: %"PRIx64 "  igvm size: %lu\n",
                gaddr, igvm_sz);
    }

    if (debug) {
        fprintf(stderr, "writing igvm file into the guest memory\n");
    }

    qtest_bufwrite(qs->qts, gaddr, igvm_blob, igvm_sz);

    if (debug) {
        fprintf(stderr,
                "tell hypervisor where igvm is loaded in guest memory\n");
    }

    /* now tell hypervisor where we loaded the bios */
    memset(&launch_update, 0, sizeof(launch_update));
    launch_update.fw_image_size = cpu_to_le32(igvm_sz);
    launch_update.fw_image_addr = cpu_to_le64(gaddr);
    launch_update.control |= VM_LAUNCHUPDATE_FORMAT_IGVM;

    filesize = qfw_cfg_write_file(fw_cfg, qs, FILE_VMLAUNCHUPDATE,
                                  &launch_update, sizeof(launch_update));
    g_assert_cmpint(filesize, ==, sizeof(launch_update));

    if (debug) {
        fprintf(stderr, "resetting the virtual machine now\n");
    }

    qtest_system_reset(qs->qts);

    /* expected string should be printed on the console */
    g_assert_true(wait_for_match(serialtmp, exp_out, WAIT_SEC) == 0);
    g_assert_true(wait_for_match(serialtmp, exp_out2, WAIT_SEC) == 0);

    close(ser_fd);

    guest_free(&qs->alloc, gaddr);
    pc_fw_cfg_uninit(fw_cfg);
    /* qtest_quit() kils QEMU, first by sending SIGTERM, then SIGKILL */
    qtest_quit(qs->qts);
}

int main(int argc, char **argv)
{
    const char *arch = qtest_get_arch();

    g_test_init(&argc, &argv, NULL);

    if (strcmp(arch, "i386") && strcmp(arch, "x86_64")) {
        g_printerr("vmlaunchupdate tests are only available on x86\n");
        exit(EXIT_FAILURE);
    }

    g_test_add_func("/vm-launch-update/cap", test_vm_launch_update_capability);
    g_test_add_func("/vm-launch-update/disabled",
                    test_vm_launch_update_disable);

    g_test_add_func("/vm-launch-update/load_igvm",
                    test_load_igvm);

    if (getenv("LAUNCHUPDATE_DEBUG")) {
        debug = true;
    }
    if (getenv("COCO")) {
        confidential = true;
    }

    return g_test_run();
}
