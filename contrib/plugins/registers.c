/*
 * Log register states
 *
 * Copyright (c) 2022 YADRO.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <glib.h>
#include <inttypes.h>
#include <stdlib.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

/* Print report to file every N instructions */
#define REPORT_BUF_N_INSN 1000000

typedef enum target_t {
    UNKNOWN_TARGET,
    X86_64_TARGET,
    RISCV64_TARGET
} target_t;

target_t target = UNKNOWN_TARGET;
bool system_emulation = false;

const char *const X86_64_REGS[] = { "rax", "rbx", "rcx", "rdx", "rsi", "rdi",
                                    "rbp", "rsp", "rip", "eflags", "xmm0", "ymm0h" };
const char *const RISCV64_REGS[] = { "zero", "ra", "sp", "gp", "tp", "t0",
                                     "a0", "a1", "ft0", "vstart" };

/*
 * Each vcpu has its own independent data set, which is only initialized once
 */
typedef struct vcpu_cache {
    struct qemu_plugin_reg_ctx *reg_ctx;
    GString *report;
    size_t report_counter;
    unsigned int vcpu_index;
} vcpu_cache;

vcpu_cache *caches = NULL;

static void print_register_values(GString *report, const void *data, size_t size)
{
    if (size == 4) {
        g_string_append_printf(report, "%08x", *(uint32_t *)data);
    }
    else if (size == 8) {
        g_string_append_printf(report, "%016" PRIx64, *(uint64_t *)data);
    }
    else if (size % sizeof(uint64_t) == 0) {
        const uint64_t *vec = (uint64_t *)data;
        int i, vec_length = size / sizeof(uint64_t);
        for (i = 0; i < vec_length; i++) {
            g_string_append_printf(report, "%016" PRIx64 " ", vec[i]);
        }
    }
    else {
        qemu_plugin_outs("Unknown register\n");
        exit(EXIT_FAILURE);
    }
}

static void print_avail_register_names(vcpu_cache *cache)
{
    char *buf = NULL;
    size_t buf_size = 0, bytes_written = 0;

    buf_size = qemu_plugin_get_available_reg_names(NULL, 0);
    g_assert(buf_size > 0);

    buf = g_new0(char, buf_size);
    bytes_written = qemu_plugin_get_available_reg_names(buf, buf_size);
    g_assert(bytes_written == buf_size);

    g_string_append_printf(cache->report, "vcpu=%u, available registers: %s", cache->vcpu_index, buf);
    g_string_append_printf(cache->report, "\n");
    g_free(buf);
}

static void init_vcpu_cache(unsigned int vcpu_index, vcpu_cache *cache)
{
    if (cache->reg_ctx != NULL)
        return;

    cache->report = g_string_new("");
    cache->report_counter = 0;
    cache->vcpu_index = vcpu_index;

    print_avail_register_names(cache);

    if (target == X86_64_TARGET) {
        cache->reg_ctx = qemu_plugin_reg_create_context(X86_64_REGS,
            sizeof(X86_64_REGS) / sizeof(X86_64_REGS[0]));
    }
    else if (target == RISCV64_TARGET) {
        cache->reg_ctx = qemu_plugin_reg_create_context(RISCV64_REGS,
            sizeof(RISCV64_REGS) / sizeof(RISCV64_REGS[0]));
    }
    else {
        g_assert_not_reached();
    }

    if (cache->reg_ctx == NULL) {
        qemu_plugin_outs("Failed to create context\n");
        exit(EXIT_FAILURE);
    }
}

static void free_vcpu_cache(vcpu_cache *cache)
{
    if (cache == NULL)
        return;

    if (cache->report)
        g_string_free(cache->report, true);
    qemu_plugin_reg_free_context(cache->reg_ctx);
}

/**
 * Log registers on instruction execution
 */
static void vcpu_insn_exec(unsigned int vcpu_index, void *udata)
{
    vcpu_cache *cache = &caches[vcpu_index];
    init_vcpu_cache(vcpu_index, cache);

    qemu_plugin_regs_load(cache->reg_ctx);

    size_t i, n_regs = qemu_plugin_n_regs(cache->reg_ctx);
    for (i = 0; i < n_regs; i++) {
        const void *data = qemu_plugin_reg_ptr(cache->reg_ctx, i);
        size_t size = qemu_plugin_reg_size(cache->reg_ctx, i);
        const char *name = qemu_plugin_reg_name(cache->reg_ctx, i);
        g_string_append_printf(cache->report, "vcpu=%u, %s=", vcpu_index, name);
        print_register_values(cache->report, data, size);
        g_string_append_printf(cache->report, ", size=%ld\n", size);
    }

    cache->report_counter++;
    if (cache->report_counter >= REPORT_BUF_N_INSN) {
        qemu_plugin_outs(cache->report->str);
        g_string_erase(cache->report, 0, cache->report->len);
        cache->report_counter = 0;
    }
}

/**
 * On translation block new translation
 *
 * QEMU converts code by translation block (TB). By hooking here we can then hook
 * a callback on each instruction.
 */
static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    struct qemu_plugin_insn *insn;

    size_t n = qemu_plugin_tb_n_insns(tb);
    for (size_t i = 0; i < n; i++) {
        insn = qemu_plugin_tb_get_insn(tb, i);
        qemu_plugin_register_vcpu_insn_exec_cb(insn, vcpu_insn_exec,
                                               QEMU_PLUGIN_CB_R_REGS, NULL);
    }
}

static int get_n_max_vcpus(void)
{
    return (system_emulation) ? qemu_plugin_n_max_vcpus() : 1;
}

/**
 * On plugin exit, print report and free memory
 */
static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    if (caches != NULL) {
        int n_cpus = get_n_max_vcpus();
        int i;
        for (i = 0; i < n_cpus; i++) {
            if (caches[i].report)
                qemu_plugin_outs(caches[i].report->str);
            free_vcpu_cache(&caches[i]);
        }
        g_free(caches);
    }
}

static void vcpu_init(qemu_plugin_id_t id, unsigned int vcpu_index)
{
    if (!system_emulation && vcpu_index > 0) {
        qemu_plugin_outs("Multithreading in user-mode is not supported\n");
        exit(EXIT_FAILURE);
    }
}

/**
 * Install the plugin
 */
QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info, int argc,
                                           char **argv)
{
    if (strcmp(info->target_name, "x86_64") == 0)
        target = X86_64_TARGET;
    else if (strcmp(info->target_name, "riscv64") == 0)
        target = RISCV64_TARGET;
    else {
        qemu_plugin_outs("Unknown architecture\n");
        return -1;
    }
    system_emulation = info->system_emulation;

    caches = g_new0(vcpu_cache, get_n_max_vcpus());
    qemu_plugin_register_vcpu_init_cb(id, vcpu_init);
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);

    return 0;
}
