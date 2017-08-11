/*
 * PXE test cases.
 *
 * Copyright (c) 2016 Red Hat Inc.
 *
 * Authors:
 *  Michael S. Tsirkin <mst@redhat.com>,
 *  Victor Kaplansky <victork@redhat.com>,
 *  Jens Freimann <jfreiman@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <glib/gstdio.h>
#include <glib.h>
#include "qemu-common.h"
#include "libqtest.h"
#include "boot-sector.h"
#include <sys/vfs.h>

#define LPORT 5555
#define RPORT 4444
#define NETNAME "net0"
#define QEMU_CMD_MEM    "--enable-kvm -m %d "\
                        "-object memory-backend-file,id=mem,size=%dM,"\
                        "mem-path=%s,share=on -numa node,memdev=mem -mem-prealloc "
#define QEMU_CMD_CHR    " -chardev socket,id=%s,path=%s"
#define QEMU_CMD_NETDEV " -device virtio-net-pci,netdev=net0 "\
                        " -netdev vhost-user,id=net0,chardev=%s,vhostforce "\
                        " -netdev user,id=n0,tftp=./,bootfile=%s "\
                        " -netdev socket,id=n1,fd=%d"
#define QEMU_CMD_NET    " -device virtio-net-pci,netdev=n0 "\
                        " -device virtio-net-pci,netdev=n1 "

#define QEMU_CMD        QEMU_CMD_MEM QEMU_CMD_CHR \
                        QEMU_CMD_NETDEV QEMU_CMD_NET

#define VUBR_SOCK "vubr.sock"
#define MEMSZ 1024

static char disk[] = "tests/pxe-test-disk-XXXXXX";

static int vubr_create_socket(struct sockaddr_in *si_remote, int rport)
{
    int sock;

    si_remote->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == -1) {
        g_test_message("socket creation failed\n");
        return -1;
    }
    if (connect(sock, (struct sockaddr *) si_remote, sizeof(*si_remote))) {
        g_test_message("connect failed: %s", strerror(errno));
        return -1;
    }

    return sock;
}

static void vubr_watch(GPid pid, gint status, gpointer data)
{
    g_assert_cmpint(status, ==, 0);
    g_spawn_close_pid(pid);
}

static void abrt_handler(void *data)
{
    int *pid = data;

    g_spawn_close_pid(*pid);
    kill(*pid, SIGTERM);
    waitpid(*pid, NULL, 0);
}

static void test_pxe_vhost_user(void)
{
    char template[] = "/tmp/vhost-user-bridge-XXXXXX";
    gchar * vubr_args[] = {NULL, NULL, NULL, NULL};
    struct sockaddr_in si_remote = {
        .sin_family = AF_INET,
        .sin_port = htons(RPORT),
    };
    const char *hugefs = NULL;
    const char *tmpfs = NULL;
    GError *error = NULL;
    char *vubr_binary;
    char *qemu_args;
    GPid vubr_pid;
    int sock = -1;

    qtest_add_abrt_handler(abrt_handler, &vubr_pid);
    tmpfs = mkdtemp(template);
    if (!tmpfs) {
        g_test_message("mkdtemp on path(%s): %s\n",
                       template, strerror(errno));
    }
    vubr_binary = getenv("QTEST_VUBR_BINARY");
    g_assert(vubr_binary);
    vubr_args[0] = g_strdup_printf("%s", vubr_binary);
    vubr_args[1] = g_strdup_printf("-u");
    vubr_args[2] = g_strdup_printf("%s/%s", tmpfs, VUBR_SOCK);
    g_spawn_async(NULL, vubr_args, NULL,
                  G_SPAWN_SEARCH_PATH |
                  G_SPAWN_DO_NOT_REAP_CHILD,
                  NULL, NULL, &vubr_pid, &error);
    g_assert_no_error(error);
    g_child_watch_add(vubr_pid, vubr_watch, NULL);

    hugefs = getenv("QTEST_HUGETLBFS_PATH");
    if (!hugefs) {
        hugefs = tmpfs;
    }
    sock = vubr_create_socket(&si_remote, RPORT);
    g_assert_cmpint(sock, !=, -1);
    qemu_args = g_strdup_printf(QEMU_CMD, MEMSZ, MEMSZ, (hugefs),
                                "char0", vubr_args[2], "char0",
                                disk, sock);
    qtest_start(qemu_args);
    boot_sector_test();
    qtest_quit(global_qtest);
    g_free(qemu_args);
    g_free(vubr_args[0]);
    g_free(vubr_args[1]);
    g_free(vubr_args[2]);
    g_assert_cmpint(g_remove(g_strdup_printf("%s/%s", tmpfs, VUBR_SOCK)),
                    ==, 0);
    g_assert_cmpint(g_remove(g_strdup_printf("%s", disk)), ==, 0);
    g_assert_cmpint(g_rmdir(tmpfs), ==, 0);
    g_assert_cmpint(kill(vubr_pid, SIGTERM), ==, 0);
}

static void test_pxe_one(const char *params, bool ipv6)
{
    char *args;

    args = g_strdup_printf("-machine accel=kvm:tcg -nodefaults -boot order=n "
                           "-netdev user,id=" NETNAME ",tftp=./,bootfile=%s,"
                           "ipv4=%s,ipv6=%s %s", disk, ipv6 ? "off" : "on",
                           ipv6 ? "on" : "off", params);

    qtest_start(args);
    boot_sector_test();
    qtest_quit(global_qtest);
    g_free(args);
}

static void test_pxe_e1000(void)
{
    test_pxe_one("-device e1000,netdev=" NETNAME, false);
}

static void test_pxe_virtio_pci(void)
{
    test_pxe_one("-device virtio-net-pci,netdev=" NETNAME, false);
}

static void test_pxe_spapr_vlan(void)
{
    test_pxe_one("-device spapr-vlan,netdev=" NETNAME, true);
}

int main(int argc, char *argv[])
{
    int ret;
    const char *arch = qtest_get_arch();

    ret = boot_sector_init(disk);
    if(ret)
        return ret;

    g_test_init(&argc, &argv, NULL);

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        qtest_add_func("pxe/e1000", test_pxe_e1000);
        qtest_add_func("pxe/virtio", test_pxe_virtio_pci);
        qtest_add_func("pxe/vhost-user", test_pxe_vhost_user);
    } else if (strcmp(arch, "ppc64") == 0) {
        qtest_add_func("pxe/virtio", test_pxe_virtio_pci);
        qtest_add_func("pxe/spapr-vlan", test_pxe_spapr_vlan);
    }
    ret = g_test_run();
    boot_sector_cleanup(disk);
    return ret;
}
