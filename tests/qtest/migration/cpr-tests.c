/*
 * QTest testcases for CPR
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
#include "libqtest.h"
#include "migration/framework.h"
#include "migration/migration-qmp.h"
#include "migration/migration-util.h"


static char *tmpfs;

static void *migrate_hook_start_mode_reboot(QTestState *from, QTestState *to)
{
    migrate_set_parameter_str(from, "mode", "cpr-reboot");
    migrate_set_parameter_str(to, "mode", "cpr-reboot");

    return NULL;
}

static void test_mode_reboot(void)
{
    g_autofree char *uri = g_strdup_printf("file:%s/%s", tmpfs,
                                           FILE_TEST_FILENAME);
    MigrateCommon args = {
        .start.use_shmem = true,
        .connect_uri = uri,
        .listen_uri = "defer",
        .start_hook = migrate_hook_start_mode_reboot,
        .start = {
            .caps[MIGRATION_CAPABILITY_X_IGNORE_SHARED] = true,
        },
    };

    test_file_common(&args, true);
}

static void *test_mode_transfer_start(QTestState *from, QTestState *to)
{
    migrate_set_parameter_str(from, "mode", "cpr-transfer");
    return NULL;
}

/*
 * Create a pre-listened UNIX domain socket at the specified path.
 *
 * This is used to eliminate a race condition that can occur
 * intermittently in qtest during CPR tests. By pre-creating and
 * listening on the socket, we avoid timing-related issues.
 */
static int setup_socket_listener(const char *path)
{
    struct sockaddr_un un;
    size_t pathlen;
    int sock_fd;

    sock_fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        g_test_message("Failed to create Unix socket");
        return -1;
    }

    pathlen = strlen(path);
    if (pathlen >= sizeof(un.sun_path)) {
        g_test_message("UNIX socket path '%s' is too long", path);
        close(sock_fd);
        return -1;
    }

    memset(&un, 0, sizeof(un));
    un.sun_family = AF_UNIX;
    strncpy(un.sun_path, path, sizeof(un.sun_path) - 1);

    if (bind(sock_fd, (struct sockaddr *)&un, sizeof(un)) < 0) {
        g_test_message("Failed to bind socket to %s", path);
        close(sock_fd);
        return -1;
    }

    if (listen(sock_fd, 1) < 0) {
        g_test_message("Failed to listen on socket %s", path);
        close(sock_fd);
        return -1;
    }

    return sock_fd;
}

/*
 * cpr-transfer mode cannot use the target monitor prior to starting the
 * migration, and cannot connect synchronously to the monitor, so defer
 * the target connection.
 */
static void test_mode_transfer_common(bool incoming_defer)
{
    g_autofree char *cpr_path = g_strdup_printf("%s/cpr.sock", tmpfs);
    g_autofree char *mig_path = g_strdup_printf("%s/migsocket", tmpfs);
    g_autofree char *uri = g_strdup_printf("unix:%s", mig_path);
    g_autofree char *addr_type, *addr_key, *addr_value;
    g_autofree char *opts_target;

    const char *opts = "-machine aux-ram-share=on -nodefaults";
    g_autofree const char *cpr_channel = g_strdup_printf(
        "cpr,addr.transport=socket,addr.type=unix,addr.path=%s",
        cpr_path);

    g_autofree char *connect_channels = g_strdup_printf(
        "[ { 'channel-type': 'main',"
        "    'addr': { 'transport': 'socket',"
        "              'type': 'unix',"
        "              'path': '%s' } } ]",
        mig_path);

    /*
     * Determine socket address type and value.
     * If socket creation fails, provide the socket path to the target,
     * so it can create the Unix domain socket itself.
     * Otherwise, use the pre-listened socket file descriptor directly.
     */
    int cpr_sockfd = setup_socket_listener(cpr_path);

    if (cpr_sockfd < 0) {
        addr_type = g_strdup("unix");
        addr_key = g_strdup("path");
        addr_value = g_strdup(cpr_path);
    } else {
        addr_type = g_strdup("fd");
        addr_key = g_strdup("str");
        addr_value = g_strdup_printf("%d", cpr_sockfd);
    }

    opts_target = g_strdup_printf("-incoming cpr,addr.transport=socket,"
                                  "addr.type=%s,addr.%s=%s %s",
                                  addr_type, addr_key, addr_value, opts);


    MigrateCommon args = {
        .start.opts_source = opts,
        .start.opts_target = opts_target,
        .start.defer_target_connect = true,
        .start.memory_backend = "-object memory-backend-memfd,id=pc.ram,size=%s"
                                " -machine memory-backend=pc.ram",
        .listen_uri = incoming_defer ? "defer" : uri,
        .connect_channels = connect_channels,
        .cpr_channel = cpr_channel,
        .start_hook = test_mode_transfer_start,
    };

    test_precopy_common(&args);
}

static void test_mode_transfer(void)
{
    test_mode_transfer_common(NULL);
}

static void test_mode_transfer_defer(void)
{
    test_mode_transfer_common(true);
}

void migration_test_add_cpr(MigrationTestEnv *env)
{
    tmpfs = env->tmpfs;

    /* no tests in the smoke set for now */

    if (!env->full_set) {
        return;
    }

    /*
     * Our CI system has problems with shared memory.
     * Don't run this test until we find a workaround.
     */
    if (getenv("QEMU_TEST_FLAKY_TESTS")) {
        migration_test_add("/migration/mode/reboot", test_mode_reboot);
    }

    if (env->has_kvm) {
        migration_test_add("/migration/mode/transfer", test_mode_transfer);
        migration_test_add("/migration/mode/transfer/defer",
                           test_mode_transfer_defer);
    }
}
