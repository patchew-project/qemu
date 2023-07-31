/*
 * Copyright (C) 2021, Alexandre Iooss <erdnaxe@crans.org>
 *
 * Log instruction execution with memory access.
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

typedef struct CPU {
    /* Store last executed instruction on each vCPU as a GString */
    GString *last_exec;
    GByteArray *reg_buf;

    int reg;
} CPU;

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static CPU *cpus;
static int num_cpus;
static GRWLock expand_array_lock;

static GPtrArray *imatches;
static GArray *amatches;

static char *rfile_name;
static char *reg_name;

/*
 * Expand cpu array.
 *
 * As we could have multiple threads trying to do this we need to
 * serialise the expansion under a lock.
 */
static void expand_cpu(int cpu_index)
{
    g_rw_lock_writer_lock(&expand_array_lock);
    if (cpu_index >= num_cpus) {
        cpus = g_realloc_n(cpus, cpu_index + 1, sizeof(*cpus));
        while (cpu_index >= num_cpus) {
            cpus[num_cpus].last_exec = g_string_new(NULL);
            cpus[num_cpus].reg_buf = g_byte_array_new();
            num_cpus++;
        }
    }
    g_rw_lock_writer_unlock(&expand_array_lock);
}

/**
 * Add memory read or write information to current instruction log
 */
static void vcpu_mem(unsigned int cpu_index, qemu_plugin_meminfo_t info,
                     uint64_t vaddr, void *udata)
{
    GString *s;

    /* Find vCPU in array */
    g_rw_lock_reader_lock(&expand_array_lock);
    g_assert(cpu_index < num_cpus);
    s = cpus[cpu_index].last_exec;
    g_rw_lock_reader_unlock(&expand_array_lock);

    /* Indicate type of memory access */
    if (qemu_plugin_mem_is_store(info)) {
        g_string_append(s, ", store");
    } else {
        g_string_append(s, ", load");
    }

    /* If full system emulation log physical address and device name */
    struct qemu_plugin_hwaddr *hwaddr = qemu_plugin_get_hwaddr(info, vaddr);
    if (hwaddr) {
        uint64_t addr = qemu_plugin_hwaddr_phys_addr(hwaddr);
        const char *name = qemu_plugin_hwaddr_device_name(hwaddr);
        g_string_append_printf(s, ", 0x%08"PRIx64", %s", addr, name);
    } else {
        g_string_append_printf(s, ", 0x%08"PRIx64, vaddr);
    }
}

/**
 * Log instruction execution
 */
static void vcpu_insn_exec(unsigned int cpu_index, void *udata)
{
    CPU cpu;
    int n;
    int i;

    /* Find or create vCPU in array */
    g_rw_lock_reader_lock(&expand_array_lock);
    cpu = cpus[cpu_index];
    g_rw_lock_reader_unlock(&expand_array_lock);

    /* Print previous instruction in cache */
    if (cpu.last_exec->len) {
        qemu_plugin_outs(cpu.last_exec->str);
        qemu_plugin_outs("\n");
    }

    /* Store new instruction in cache */
    /* vcpu_mem will add memory access information to last_exec */
    g_string_printf(cpu.last_exec, "%u, ", cpu_index);
    g_string_append(cpu.last_exec, (char *)udata);

    if (cpu.reg >= 0) {
        g_string_append(cpu.last_exec, ", reg,");
        n = qemu_plugin_read_register(cpu.reg_buf, cpu.reg);
        for (i = 0; i < n; i++) {
            g_string_append_printf(cpu.last_exec, " 0x%02X",
                                   cpu.reg_buf->data[i]);
        }
        g_byte_array_set_size(cpu.reg_buf, 0);
    }
}

/**
 * On translation block new translation
 *
 * QEMU convert code by translation block (TB). By hooking here we can then hook
 * a callback on each instruction and memory access.
 */
static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    struct qemu_plugin_insn *insn;
    bool skip = (imatches || amatches);

    size_t n = qemu_plugin_tb_n_insns(tb);
    for (size_t i = 0; i < n; i++) {
        char *insn_disas;
        uint64_t insn_vaddr;

        /*
         * `insn` is shared between translations in QEMU, copy needed data here.
         * `output` is never freed as it might be used multiple times during
         * the emulation lifetime.
         * We only consider the first 32 bits of the instruction, this may be
         * a limitation for CISC architectures.
         */
        insn = qemu_plugin_tb_get_insn(tb, i);
        insn_disas = qemu_plugin_insn_disas(insn);
        insn_vaddr = qemu_plugin_insn_vaddr(insn);

        /*
         * If we are filtering we better check out if we have any
         * hits. The skip "latches" so we can track memory accesses
         * after the instruction we care about.
         */
        if (skip && imatches) {
            int j;
            for (j = 0; j < imatches->len && skip; j++) {
                char *m = g_ptr_array_index(imatches, j);
                if (g_str_has_prefix(insn_disas, m)) {
                    skip = false;
                }
            }
        }

        if (skip && amatches) {
            int j;
            for (j = 0; j < amatches->len && skip; j++) {
                uint64_t v = g_array_index(amatches, uint64_t, j);
                if (v == insn_vaddr) {
                    skip = false;
                }
            }
        }

        if (skip) {
            g_free(insn_disas);
        } else {
            uint32_t insn_opcode;
            insn_opcode = *((uint32_t *)qemu_plugin_insn_data(insn));
            char *output = g_strdup_printf("0x%"PRIx64", 0x%"PRIx32", \"%s\"",
                                           insn_vaddr, insn_opcode, insn_disas);

            /* Register callback on memory read or write */
            qemu_plugin_register_vcpu_mem_cb(insn, vcpu_mem,
                                             QEMU_PLUGIN_CB_NO_REGS,
                                             QEMU_PLUGIN_MEM_RW, NULL);

            /* Register callback on instruction */
            qemu_plugin_register_vcpu_insn_exec_cb(
                insn, vcpu_insn_exec,
                rfile_name ? QEMU_PLUGIN_CB_R_REGS : QEMU_PLUGIN_CB_NO_REGS,
                output);

            /* reset skip */
            skip = (imatches || amatches);
        }

    }
}

static void vcpu_init(qemu_plugin_id_t id, unsigned int vcpu_index)
{
    int reg = 0;
    bool found = false;

    expand_cpu(vcpu_index);

    if (rfile_name) {
        int i;
        int j;
        int n;

        qemu_plugin_register_file_t *rfiles =
            qemu_plugin_get_register_files(vcpu_index, &n);

        for (i = 0; i < n; i++) {
            if (g_strcmp0(rfiles[i].name, rfile_name) == 0) {
                for (j = 0; j < rfiles[i].num_regs; j++) {
                    if (g_strcmp0(rfiles[i].regs[j], reg_name) == 0) {
                        reg += j;
                        found = true;
                        break;
                    }
                }
                break;
            }

            reg += rfiles[i].num_regs;
        }

        g_free(rfiles);
    }

    g_rw_lock_writer_lock(&expand_array_lock);
    cpus[vcpu_index].reg = found ? reg : -1;
    g_rw_lock_writer_unlock(&expand_array_lock);
}

/**
 * On plugin exit, print last instruction in cache
 */
static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    guint i;
    for (i = 0; i < num_cpus; i++) {
        if (cpus[i].last_exec->str) {
            qemu_plugin_outs(cpus[i].last_exec->str);
            qemu_plugin_outs("\n");
        }
    }
}

/* Add a match to the array of matches */
static void parse_insn_match(char *match)
{
    if (!imatches) {
        imatches = g_ptr_array_new();
    }
    g_ptr_array_add(imatches, match);
}

static void parse_vaddr_match(char *match)
{
    uint64_t v = g_ascii_strtoull(match, NULL, 16);

    if (!amatches) {
        amatches = g_array_new(false, true, sizeof(uint64_t));
    }
    g_array_append_val(amatches, v);
}

/**
 * Install the plugin
 */
QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info, int argc,
                                           char **argv)
{
    /*
     * Initialize dynamic array to cache vCPU instruction. In user mode
     * we don't know the size before emulation.
     */
    if (info->system_emulation) {
        cpus = g_new(CPU, info->system.max_vcpus);
    }

    for (int i = 0; i < argc; i++) {
        char *opt = argv[i];
        g_auto(GStrv) tokens = g_strsplit(opt, "=", 2);
        if (g_strcmp0(tokens[0], "ifilter") == 0) {
            parse_insn_match(tokens[1]);
        } else if (g_strcmp0(tokens[0], "afilter") == 0) {
            parse_vaddr_match(tokens[1]);
        } else if (g_strcmp0(tokens[0], "rfile") == 0) {
            rfile_name = g_strdup(tokens[1]);
        } else if (g_strcmp0(tokens[0], "reg") == 0) {
            reg_name = g_strdup(tokens[1]);
        } else {
            fprintf(stderr, "option parsing failed: %s\n", opt);
            return -1;
        }
    }

    if ((!rfile_name) != (!reg_name)) {
        fputs("file and reg need to be set at the same time\n", stderr);
        return -1;
    }

    /* Register translation block and exit callbacks */
    qemu_plugin_register_vcpu_init_cb(id, vcpu_init);
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);

    return 0;
}
