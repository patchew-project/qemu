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

static uint64_t bb_count;
static uint64_t insn_count;
static int stdout_fd;
static bool do_inline;

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    dprintf(stdout_fd, "bb's: %" PRIu64", insns: %" PRIu64 "\n",
            bb_count, insn_count);
}

static void vcpu_tb_exec(unsigned int cpu_index, void *udata)
{
    unsigned long n_insns = (unsigned long)udata;

    insn_count += n_insns;
    bb_count++;
}

static void vcpu_tb_trans(qemu_plugin_id_t id, unsigned int cpu_index,
                          struct qemu_plugin_tb *tb)
{
    unsigned long n_insns = qemu_plugin_tb_n_insns(tb);

    if (do_inline) {
        qemu_plugin_register_vcpu_tb_exec_inline(tb, QEMU_PLUGIN_INLINE_ADD_U64,
                                                 &bb_count, 1);
        qemu_plugin_register_vcpu_tb_exec_inline(tb, QEMU_PLUGIN_INLINE_ADD_U64,
                                                 &insn_count, n_insns);
    } else {
        qemu_plugin_register_vcpu_tb_exec_cb(tb, vcpu_tb_exec,
                                             QEMU_PLUGIN_CB_NO_REGS,
                                             (void *)n_insns);
    }
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id, int argc,
                                           char **argv)
{
    if (argc && strcmp(argv[0], "inline") == 0) {
        do_inline = true;
    }

    /* to be used when in the exit hook */
    stdout_fd = dup(STDOUT_FILENO);
    assert(stdout_fd);

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
