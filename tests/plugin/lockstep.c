/*
 * Lockstep Execution Plugin
 *
 * Allows you to execute two QEMU instances in lockstep and report
 * when their execution diverges. This is mainly useful for developers
 * who want to see where a change to TCG code generation has
 * introduced a subtle and hard to find bug.
 *
 * Caveats:
 *   - single-threaded linux-user apps only with non-deterministic syscalls
 *   - icount based system emulation (no MTTCG)
 *
 * This code is not thread safe!
 *
 * Copyright (c) 2020 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <glib.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <errno.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

/* saved so we can uninstall later */
static qemu_plugin_id_t our_id;

static unsigned long bb_count;
static unsigned long insn_count;

typedef struct {
    uint64_t pc;
    uint64_t insns_in_block;
    uint64_t insns_executed;
} BlockInfo;

static GSList *log;

static int socket_fd;
static char *path_to_unlink;


static void plugin_cleanup(qemu_plugin_id_t id)
{

    /* Free our block data */
    g_slist_free_full(log, &g_free);

    close(socket_fd);
    if (path_to_unlink) {
        unlink(path_to_unlink);
    }
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    g_autoptr(GString) out = g_string_new("No divergence :-)\n");
    g_string_append_printf(out, "Executed %ld/%d blocks\n",
                           bb_count, g_slist_length(log));
    g_string_append_printf(out, "Executed ~%ld instructions\n", insn_count);
    qemu_plugin_outs(out->str);

    plugin_cleanup(id);
}

static void report_divergance(BlockInfo *us, BlockInfo *them)
{
    int i;
    GSList *entry = log;
    g_autoptr(GString) out = g_string_new("I feel a divergence in the force\n");
    g_string_append_printf(out, "Us @ %#016lx (%ld)\n",
                           us->pc, us->insns_executed);
    g_string_append_printf(out, "Them @ %#016lx (%ld)\n",
                           them->pc, them->insns_executed);
    for (i = 0; i < 5; i++) {
        BlockInfo *prev = (BlockInfo *) entry->data;
        g_string_append_printf(out, "  previously @ %#016lx\n", prev->pc);
        entry = g_slist_next(entry);
    }

    qemu_plugin_outs(out->str);

    /* we can't do anything else now so uninstall ourselves */
    qemu_plugin_uninstall(our_id, plugin_cleanup);
}

static void vcpu_tb_exec(unsigned int cpu_index, void *udata)
{
    BlockInfo *bi = (BlockInfo *) udata;
    BlockInfo remote;
    ssize_t bytes;

    bi->insns_executed = insn_count;

    /* write our execution state */
    bytes = write(socket_fd, bi, sizeof(BlockInfo));
    if (bytes < sizeof(BlockInfo)) {
        if (bytes < 0) {
            qemu_plugin_outs("problem writing to socket");
            abort();
        }
        qemu_plugin_outs("wrote less than expected");
    }
    /* read where our peer has reached */
    bytes = read(socket_fd, &remote, sizeof(BlockInfo));
    if (bytes < sizeof(BlockInfo)) {
        if (bytes < 0) {
            qemu_plugin_outs("problem reading from socket");
            abort();
        }
        qemu_plugin_outs("read less than expected");
        abort();
    }

    // compare and bail
    if ((bi->pc != remote.pc) ||
        (bi->insns_executed != remote.insns_executed)) {
        report_divergance(bi, &remote);
    }

    // mark the execution as complete
    log = g_slist_prepend(log, bi);
    insn_count += bi->insns_in_block;
    bb_count++;
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    BlockInfo *bi = g_new0(BlockInfo, 1);
    bi->pc = qemu_plugin_tb_vaddr(tb);
    bi->insns_in_block = qemu_plugin_tb_n_insns(tb);

    qemu_plugin_register_vcpu_tb_exec_cb(tb, vcpu_tb_exec,
                                         QEMU_PLUGIN_CB_NO_REGS, (void *)bi);
}


/*
 * Instead of encoding master/slave status into what is essentially
 * two peers we shall just take the simple approach of checking for
 * the existence of the pipe and assuming if it's not there we are the
 * first process.
 */
static bool setup_socket(const char *path)
{
    struct sockaddr_un sockaddr;
    int fd;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("create socket");
        return false;
    }

    sockaddr.sun_family = AF_UNIX;
    g_strlcpy(sockaddr.sun_path, path, sizeof(sockaddr.sun_path)-1);
    if (bind(fd, (struct sockaddr *)&sockaddr, sizeof(sockaddr)) < 0) {
        perror("bind socket");
        close(fd);
        return false;
    }

    /* remember to clean-up */
    path_to_unlink = g_strdup(path);

    if (listen(fd, 1) < 0) {
        perror("listen socket");
        close(fd);
        return false;
    }

    socket_fd = accept(fd, NULL, NULL);
    if (socket_fd < 0 && errno != EINTR) {
        perror("accept socket");
        return false;
    }

    qemu_plugin_outs("setup_socket::ready\n");

    return true;
}

static bool connect_socket(const char *path)
{
    int fd;
    struct sockaddr_un sockaddr;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("create socket");
        return false;
    }

    sockaddr.sun_family = AF_UNIX;
    g_strlcpy(sockaddr.sun_path, path, sizeof(sockaddr.sun_path)-1);

    if (connect(fd, (struct sockaddr *)&sockaddr, sizeof(sockaddr)) < 0) {
        perror("failed to connect");
        return false;
    }

    qemu_plugin_outs("connect_socket::ready\n");

    socket_fd = fd;
    return true;
}

static bool setup_unix_socket(const char *path)
{
    if (g_file_test(path, G_FILE_TEST_EXISTS)) {
        return connect_socket(path);
    } else {
        return setup_socket(path);
    }
}


QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    if (!argc || !argv[0]) {
        qemu_plugin_outs("Need a socket path to talk to other instance.");
        return -1;
    }

    if (!setup_unix_socket(argv[0])) {
        qemu_plugin_outs("Failed to setup socket for communications.");
        return -1;
    }

    our_id = id;

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
