/*
 * Copyright (C) 2023, Pierrick Bouvier <pierrick.bouvier@linaro.org>
 *
 * Demonstrates and tests usage of inline ops.
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */

#include <glib.h>
#include <stdint.h>
#include <stdio.h>

#include <qemu-plugin.h>

#define MAX_CPUS 8

static uint64_t count_tb;
static uint64_t count_tb_per_vcpu[MAX_CPUS];
static uint64_t count_tb_inline_per_vcpu[MAX_CPUS];
static uint64_t count_insn;
static uint64_t count_insn_per_vcpu[MAX_CPUS];
static uint64_t count_insn_inline_per_vcpu[MAX_CPUS];
static uint64_t count_mem;
static uint64_t count_mem_per_vcpu[MAX_CPUS];
static uint64_t count_mem_inline_per_vcpu[MAX_CPUS];
static GMutex tb_lock;
static GMutex insn_lock;
static GMutex mem_lock;

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static uint64_t collect_per_vcpu(uint64_t *values)
{
    uint64_t count = 0;
    for (int i = 0; i < MAX_CPUS; ++i) {
        count += values[i];
    }
    return count;
}

static void stats_insn(void)
{
    const uint64_t expected = count_insn;
    const uint64_t per_vcpu = collect_per_vcpu(count_insn_per_vcpu);
    const uint64_t inl_per_vcpu = collect_per_vcpu(count_insn_inline_per_vcpu);
    printf("insn: %" PRIu64 "\n", expected);
    printf("insn: %" PRIu64 " (per vcpu)\n", per_vcpu);
    printf("insn: %" PRIu64 " (per vcpu inline)\n", inl_per_vcpu);
    g_assert(expected > 0);
    g_assert(per_vcpu == expected);
    g_assert(inl_per_vcpu == expected);
}

static void stats_tb(void)
{
    const uint64_t expected = count_tb;
    const uint64_t per_vcpu = collect_per_vcpu(count_tb_per_vcpu);
    const uint64_t inl_per_vcpu = collect_per_vcpu(count_tb_inline_per_vcpu);
    printf("tb: %" PRIu64 "\n", expected);
    printf("tb: %" PRIu64 " (per vcpu)\n", per_vcpu);
    printf("tb: %" PRIu64 " (per vcpu inline)\n", inl_per_vcpu);
    g_assert(expected > 0);
    g_assert(per_vcpu == expected);
    g_assert(inl_per_vcpu == expected);
}

static void stats_mem(void)
{
    const uint64_t expected = count_mem;
    const uint64_t per_vcpu = collect_per_vcpu(count_mem_per_vcpu);
    const uint64_t inl_per_vcpu = collect_per_vcpu(count_mem_inline_per_vcpu);
    printf("mem: %" PRIu64 "\n", expected);
    printf("mem: %" PRIu64 " (per vcpu)\n", per_vcpu);
    printf("mem: %" PRIu64 " (per vcpu inline)\n", inl_per_vcpu);
    g_assert(expected > 0);
    g_assert(per_vcpu == expected);
    g_assert(inl_per_vcpu == expected);
}

static void plugin_exit(qemu_plugin_id_t id, void *udata)
{
    for (int i = 0; i < MAX_CPUS; ++i) {
        const uint64_t tb = count_tb_per_vcpu[i];
        const uint64_t tb_inline = count_tb_inline_per_vcpu[i];
        const uint64_t insn = count_insn_per_vcpu[i];
        const uint64_t insn_inline = count_insn_inline_per_vcpu[i];
        const uint64_t mem = count_mem_per_vcpu[i];
        const uint64_t mem_inline = count_mem_inline_per_vcpu[i];
        printf("cpu %d: tb (%" PRIu64 ", %" PRIu64 ") | "
               "insn (%" PRIu64 ", %" PRIu64 ") | "
               "mem (%" PRIu64 ", %" PRIu64 ")"
               "\n",
               i, tb, tb_inline, insn, insn_inline, mem, mem_inline);
        g_assert(tb == tb_inline);
        g_assert(insn == insn_inline);
        g_assert(mem == mem_inline);
    }

    stats_tb();
    stats_insn();
    stats_mem();
}

static void vcpu_tb_exec(unsigned int cpu_index, void *udata)
{
    count_tb_per_vcpu[cpu_index]++;
    g_mutex_lock(&tb_lock);
    count_tb++;
    g_mutex_unlock(&tb_lock);
}

static void vcpu_insn_exec(unsigned int cpu_index, void *udata)
{
    count_insn_per_vcpu[cpu_index]++;
    g_mutex_lock(&insn_lock);
    count_insn++;
    g_mutex_unlock(&insn_lock);
}

static void vcpu_mem_access(unsigned int cpu_index,
                            qemu_plugin_meminfo_t info,
                            uint64_t vaddr,
                            void *userdata)
{
    count_mem_per_vcpu[cpu_index]++;
    g_mutex_lock(&mem_lock);
    count_mem++;
    g_mutex_unlock(&mem_lock);
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    qemu_plugin_register_vcpu_tb_exec_cb(tb, vcpu_tb_exec,
                                         QEMU_PLUGIN_CB_NO_REGS, 0);
    qemu_plugin_register_vcpu_tb_exec_inline_per_vcpu(
        tb, QEMU_PLUGIN_INLINE_ADD_U64,
        count_tb_inline_per_vcpu, sizeof(uint64_t), 1);

    for (int idx = 0; idx < qemu_plugin_tb_n_insns(tb); ++idx) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, idx);
        qemu_plugin_register_vcpu_insn_exec_cb(insn, vcpu_insn_exec,
                                               QEMU_PLUGIN_CB_NO_REGS, 0);
        qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(
            insn, QEMU_PLUGIN_INLINE_ADD_U64,
            count_insn_inline_per_vcpu, sizeof(uint64_t), 1);
        qemu_plugin_register_vcpu_mem_cb(insn, &vcpu_mem_access,
                                         QEMU_PLUGIN_CB_NO_REGS,
                                         QEMU_PLUGIN_MEM_RW, 0);
        qemu_plugin_register_vcpu_mem_inline_per_vcpu(
            insn, QEMU_PLUGIN_MEM_RW,
            QEMU_PLUGIN_INLINE_ADD_U64,
            count_mem_inline_per_vcpu, sizeof(uint64_t), 1);
    }
}

QEMU_PLUGIN_EXPORT
int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info,
                        int argc, char **argv)
{
    g_assert(info->system.smp_vcpus <= MAX_CPUS);
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);

    return 0;
}
