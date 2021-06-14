/*
 * Copyright (C) 2021, Alexandre Iooss <erdnaxe@crans.org>
 *
 * Log instruction execution and memory access to a file.
 * You may pass the output filename as argument.
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */
#include <glib.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

/* Execution trace buffer */
FILE *output;

/**
 * Log memory read or write
 */
static void vcpu_mem(unsigned int vcpu_index, qemu_plugin_meminfo_t info,
                     uint64_t vaddr, void *udata)
{
    struct qemu_plugin_hwaddr *hwaddr = qemu_plugin_get_hwaddr(info, vaddr);
    if (!hwaddr) {
        return;
    }

    /* Add data to execution log */
    const char *name = qemu_plugin_hwaddr_device_name(hwaddr);
    uint64_t addr = qemu_plugin_hwaddr_phys_addr(hwaddr);
    if (qemu_plugin_mem_is_store(info)) {
        fprintf(output, "mem: %s store at 0x%08lx\n", name, addr);
    } else {
        fprintf(output, "mem: %s load at 0x%08lx\n", name, addr);
    }
}

/**
 * Log instruction execution
 */
static void vcpu_insn_exec(unsigned int cpu_index, void *udata)
{
    char *insn_disas = (char *)udata;

    /* Add data to execution log */
    fprintf(output, "insn: %s\n", insn_disas);
}

/**
 * On translation block new translation
 *
 * QEMU convert code by translation block (TB). By hooking here we can then hook
 * a callback on each instruction and memory access.
 */
static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n = qemu_plugin_tb_n_insns(tb);
    for (size_t i = 0; i < n; i++) {
        /* insn is shared between translations in QEMU, copy needed data here */
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        char *insn_disas = qemu_plugin_insn_disas(insn);

        /* Register callback on memory read or write */
        qemu_plugin_register_vcpu_mem_cb(insn, vcpu_mem,
                                         QEMU_PLUGIN_CB_NO_REGS,
                                         QEMU_PLUGIN_MEM_RW, NULL);

        /* Register callback on instruction */
        qemu_plugin_register_vcpu_insn_exec_cb(
            insn, vcpu_insn_exec, QEMU_PLUGIN_CB_R_REGS, insn_disas);
    }
}

/**
 * On plugin exit, close output file
 */
static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    fclose(output);
}

/**
 * Install the plugin
 */
QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info, int argc,
                                           char **argv)
{
    /* Parse arguments to get output name and open for writing */
    char *filename = "execution.log";
    if (argc > 0) {
        filename = argv[0];
    }
    output = fopen(filename, "w");
    if (output == NULL) {
        qemu_plugin_outs("Cannot open output file for writing.\n");
        return -1;
    }

    /* Register translation block and exit callbacks */
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);

    return 0;
}
