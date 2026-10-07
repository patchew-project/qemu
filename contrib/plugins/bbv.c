/*
 * Generate basic block vectors for use with the SimPoint analysis tool.
 * SimPoint: https://cseweb.ucsd.edu/~calder/simpoint/
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <glib.h>

#include <qemu-plugin.h>

typedef struct Bb {
    uint64_t vaddr;
    struct qemu_plugin_scoreboard *count;
    unsigned int index;
} Bb;

typedef struct Vcpu {
    uint64_t count;
    FILE *file;
} Vcpu;

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;
static GHashTable *bbs;
static GRWLock bbs_lock;
static char *filename;
static struct qemu_plugin_scoreboard *vcpus;
static bool begin_present;
static uint64_t begin;
static bool end_present;
static uint64_t end;
static int64_t interval = 100000000;

static void plugin_exit(void *p)
{
    Vcpu *vcpu;

    for (int i = 0; i < qemu_plugin_num_vcpus(); i++) {
        vcpu = qemu_plugin_scoreboard_find(vcpus, i);
        if (vcpu->file) {
            fclose(vcpu->file);
        }
    }

    g_hash_table_unref(bbs);
    g_free(filename);
    qemu_plugin_scoreboard_free(vcpus);
}

static void free_bb(void *data)
{
    qemu_plugin_scoreboard_free(((Bb *)data)->count);
    g_free(data);
}

static qemu_plugin_u64 count_u64(void)
{
    return qemu_plugin_scoreboard_u64_in_struct(vcpus, Vcpu, count);
}

static qemu_plugin_u64 bb_count_u64(Bb *bb)
{
    return qemu_plugin_scoreboard_u64(bb->count);
}

static void vcpu_init(unsigned int vcpu_index, void *userdata)
{
    g_autofree gchar *vcpu_filename = NULL;
    Vcpu *vcpu = qemu_plugin_scoreboard_find(vcpus, vcpu_index);

    vcpu_filename = g_strdup_printf("%s.%u.bb", filename, vcpu_index);
    vcpu->count = begin_present ? INT64_MIN : -interval;
    vcpu->file = fopen(vcpu_filename, "w");
}

static void vcpu_interval_exec(unsigned int vcpu_index, void *udata)
{
    Vcpu *vcpu = qemu_plugin_scoreboard_find(vcpus, vcpu_index);
    GHashTableIter iter;
    void *value;

    if (!vcpu->file) {
        return;
    }

    vcpu->count -= interval;

    fputc('T', vcpu->file);

    g_rw_lock_reader_lock(&bbs_lock);
    g_hash_table_iter_init(&iter, bbs);

    while (g_hash_table_iter_next(&iter, NULL, &value)) {
        Bb *bb = value;
        uint64_t bb_count = qemu_plugin_u64_get(bb_count_u64(bb), vcpu_index);

        if (!bb_count) {
            continue;
        }

        fprintf(vcpu->file, ":%u:%" PRIu64 " ", bb->index, bb_count);
        qemu_plugin_u64_set(bb_count_u64(bb), vcpu_index, 0);
    }

    g_rw_lock_reader_unlock(&bbs_lock);
    fputc('\n', vcpu->file);
}

static void vcpu_begin_insn_exec(unsigned int vcpu_index, void *userdata)
{
    GHashTableIter iter;
    void *value;

    g_rw_lock_reader_lock(&bbs_lock);
    g_hash_table_iter_init(&iter, bbs);

    while (g_hash_table_iter_next(&iter, NULL, &value)) {
        qemu_plugin_u64_set(bb_count_u64(value), vcpu_index, 0);
    }

    g_rw_lock_reader_unlock(&bbs_lock);
    qemu_plugin_u64_set(count_u64(), vcpu_index, -interval);
}

static void vcpu_tb_trans(struct qemu_plugin_tb *tb, void *userdata)
{
    uint64_t n_insns = qemu_plugin_tb_n_insns(tb);
    uint64_t vaddr = qemu_plugin_tb_vaddr(tb);
    Bb *bb;

    g_rw_lock_writer_lock(&bbs_lock);
    bb = g_hash_table_lookup(bbs, &vaddr);
    if (!bb) {
        bb = g_new(Bb, 1);
        bb->vaddr = vaddr;
        bb->count = qemu_plugin_scoreboard_new(sizeof(uint64_t));
        bb->index = g_hash_table_size(bbs) + 1;
        g_hash_table_replace(bbs, &bb->vaddr, bb);
    }
    g_rw_lock_writer_unlock(&bbs_lock);

    for (size_t idx = 0; idx < n_insns; idx++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, idx);
        uint64_t insn_vaddr = qemu_plugin_insn_vaddr(insn);

        if (begin_present && insn_vaddr == begin) {
            qemu_plugin_register_vcpu_insn_exec_cb(
                insn, vcpu_begin_insn_exec, QEMU_PLUGIN_CB_NO_REGS, NULL);
        } else if (end_present && insn_vaddr == end) {
            qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(
                insn, QEMU_PLUGIN_INLINE_STORE_U64, count_u64(), INT64_MIN);
        }
    }

    qemu_plugin_register_vcpu_tb_exec_inline_per_vcpu(
        tb, QEMU_PLUGIN_INLINE_ADD_U64, count_u64(), n_insns);

    qemu_plugin_register_vcpu_tb_exec_inline_per_vcpu(
        tb, QEMU_PLUGIN_INLINE_ADD_U64, bb_count_u64(bb), n_insns);

    qemu_plugin_register_vcpu_tb_exec_cond_cb(
        tb, vcpu_interval_exec, QEMU_PLUGIN_CB_NO_REGS,
        QEMU_PLUGIN_COND_LT, count_u64(), INT64_MIN, NULL);
}

static bool parse_vaddr(uint64_t *vaddr, const char *opt, const char *token)
{
    char *endptr;

    if (!*token) {
        fprintf(stderr, "value is missing: %s\n", opt);
        return false;
    }

    errno = 0;
    *vaddr = g_ascii_strtoull(token, &endptr, 0);

    if (*endptr) {
        fprintf(stderr, "malformed integer: %s\n", opt);
        return false;
    }

    if (errno) {
        perror(opt);
        return false;
    }

    return true;
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    for (int i = 0; i < argc; i++) {
        char *opt = argv[i];
        g_auto(GStrv) tokens = g_strsplit(opt, "=", 2);
        if (g_strcmp0(tokens[0], "begin") == 0) {
            if (!parse_vaddr(&begin, opt, tokens[1])) {
                return -1;
            }
            begin_present = true;
        } else if (g_strcmp0(tokens[0], "end") == 0) {
            if (!parse_vaddr(&end, opt, tokens[1])) {
                return -1;
            }
            end_present = true;
        } else if (g_strcmp0(tokens[0], "interval") == 0) {
            char *endptr;
            interval = g_ascii_strtoll(tokens[1], &endptr, 10);
            if (*endptr) {
                fprintf(stderr, "malformed integer: %s\n", opt);
                return -1;
            }
            if (interval <= 0) {
                fprintf(stderr, "unexpected non-positive value: %s\n", opt);
                return -1;
            }
        } else if (g_strcmp0(tokens[0], "outfile") == 0) {
            filename = tokens[1];
            tokens[1] = NULL;
        } else {
            fprintf(stderr, "option parsing failed: %s\n", opt);
            return -1;
        }
    }

    if (begin_present && end_present && begin == end) {
        fprintf(stderr, "begin and end have the same value");
        return -1;
    }

    if (!filename) {
        fputs("outfile unspecified\n", stderr);
        return -1;
    }

    bbs = g_hash_table_new_full(g_int64_hash, g_int64_equal, NULL, free_bb);
    vcpus = qemu_plugin_scoreboard_new(sizeof(Vcpu));
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    qemu_plugin_register_vcpu_init_cb(id, vcpu_init, NULL);
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans, NULL);

    return 0;
}
