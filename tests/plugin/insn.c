/*
 * Copyright (C) 2018, Emilio G. Cota <cota@braap.org>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include <qemu-plugin.h>

static int stdout_fd;
static uint64_t insn_count;
static bool do_inline;

static void vcpu_insn_exec_before(unsigned int cpu_index, void *udata)
{
    insn_count++;
}

static void vcpu_tb_trans(qemu_plugin_id_t id, unsigned int cpu_index,
                          struct qemu_plugin_tb *tb)
{
    size_t n = qemu_plugin_tb_n_insns(tb);
    size_t i;

    for (i = 0; i < n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);

        if (do_inline) {
            qemu_plugin_register_vcpu_insn_exec_inline(
                insn, QEMU_PLUGIN_INLINE_ADD_U64, &insn_count, 1);
        } else {
            qemu_plugin_register_vcpu_insn_exec_cb(
                insn, vcpu_insn_exec_before, QEMU_PLUGIN_CB_NO_REGS, NULL);
        }
    }
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    dprintf(stdout_fd, "insns: %" PRIu64 "\n", insn_count);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id, int argc,
                                           char **argv)
{
    if (argc && !strcmp(argv[0], "inline")) {
        do_inline = true;
    }

    /* to be used when in the exit hook */
    stdout_fd = dup(STDOUT_FILENO);
    assert(stdout_fd);

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
