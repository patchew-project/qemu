/*
 * Copyright (C) 2025, Alex Bennée <alex.bennee@linaro.org>
 *
 * System tracing tool. Log changes to system registers and where IRQ
 * and exceptions occur in the code.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <glib.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

/* The base register we are tracking */
typedef struct {
    struct qemu_plugin_register *handle;
    const char *name;
    int index;
    int instrumentation_count;
} BaseRegister;

/*
 * Array of BaseRegister - the position in the array will also
 * control the index into the access score board. As the list may grow
 * dynamically as additional vCPUs are initialised we need to protect
 * the list with a lock.
 */
static GArray *base_registers;
static GMutex base_reg_lock;
static struct qemu_plugin_scoreboard *base_reg_hits;

#define MAX_TRACKING_REGISTERS 32

/*
 * Scoreboard for tracking last sysreg write - usually the last one
 * touched is the one that triggered something ;-)
 */
static bool track_sysreg_write;
static char *sysreg_ins;
static struct qemu_plugin_scoreboard *last_sysreg_write;

/*
 * Scoreboard for tracking last executed PC. It's possible to be
 * missing a translation of from_pc due to the fact we may have
 * advanced the PC before attempting the translation. However the last
 * insn in a block will generally be the last thing we did.
 */
static struct qemu_plugin_scoreboard *last_exec_pc;

/* per-vcpu initialisation lock */
static GMutex vcpu_init_lock;

/* the passed matching parameters */
static GPtrArray *rmatches;

/* The per-cpu register tracking structure */
typedef struct {
    GByteArray *last;
    uint64_t last_dump_count;
    int index;
} Register;

/* CPU specific data */
typedef struct CPU {
    /* Track available registers over multiple vcpu_init calls */
    int available_reg_count;
    /* Ptr array of Register */
    GPtrArray *registers;
} CPU;

/* This is defined at start time */
static GArray *cpus;

/* Track the disassembly */
static GHashTable *haddr_disas;
static GMutex disas_lock;

static bool show_from_pc;

static CPU *get_cpu(int vcpu_index)
{
    CPU *c = &g_array_index(cpus, CPU, vcpu_index);
    return c;
}

/*
 * BaseRegister handling
 *
 * We return copies of the BaseRegister entry so we don't get
 * surprised by resizing of the underlying array.
 */

static BaseRegister get_base_reg(int index)
{
    BaseRegister info, *entry;

    g_mutex_lock(&base_reg_lock);

    entry = &g_array_index(base_registers, BaseRegister, index);
    info = *entry;

    g_mutex_unlock(&base_reg_lock);

    return info;
}

static BaseRegister find_or_add_base_register(qemu_plugin_reg_descriptor * rd)
{
    g_autofree gchar *lower = g_utf8_strdown(rd->name, -1);
    BaseRegister base;
    bool found = false;

    g_mutex_lock(&base_reg_lock);

    for (int i = 0; i < base_registers->len; i++) {
        BaseRegister *check = &g_array_index(base_registers, BaseRegister, i);
        if (check->handle == rd->handle) {
            base = *check;
            found = true;
            break;
        }
    }

    /* didn't find, then add it */
    if (!found) {
        base.handle = rd->handle;
        base.name = g_intern_string(lower);
        base.index = base_registers->len;

        g_array_append_val(base_registers, base);
    }

    g_assert(base_registers->len < MAX_TRACKING_REGISTERS);

    g_mutex_unlock(&base_reg_lock);

    return base;
}

/* Sets *info on find */
static bool find_base_reg_by_str(const gchar *insn_arg, BaseRegister *info)
{
    bool reg_hit = false;

    g_mutex_lock(&base_reg_lock);

    for (int n = 0; n < base_registers->len; n++) {
        BaseRegister *base = &g_array_index(base_registers, BaseRegister, n);
        if (g_strrstr(insn_arg, base->name)) {
            *info = *base;
            reg_hit = true;
            break;
        }
    }

    g_mutex_unlock(&base_reg_lock);

    return reg_hit;
}

/**
 * On translation block new translation
 *
 * QEMU convert code by translation block (TB). We are only going to
 * add hooks to instructions that modify the registers we care about.
 * However we do need a record of every instruction we come across so
 * we can resolve information for the discontinuities.
 */
static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    struct qemu_plugin_insn *insn;
    uint64_t vaddr;

    g_mutex_lock(&disas_lock);

    size_t n_insns = qemu_plugin_tb_n_insns(tb);
    for (size_t i = 0; i < n_insns; i++) {
        g_autofree char *insn_disas = NULL;
        const gchar *intern_disas;

        /*
         * `insn` is shared between translations in QEMU, copy needed
         * data here.
         *
         * We will generate an interned string from the disassembly
         * and save it in out hash table indexed by the vaddr.
         *
         * This is vulnerable to old pages being swapped out because
         * we aren't tracking the underlying physical address. But
         * generally we expect this to be sane.
         *
         * The interned strings are never free'd but hopefully there
         * is enough repetition we don't need a string for every
         * instruction we execute.
         */
        insn = qemu_plugin_tb_get_insn(tb, i);
        insn_disas = qemu_plugin_insn_disas(insn);
        intern_disas = g_intern_string(insn_disas);
        /* haddr = qemu_plugin_insn_haddr(insn); */
        vaddr = qemu_plugin_insn_vaddr(insn);

        /* replaces any existing interned string */
        g_hash_table_insert(haddr_disas,
                            GUINT_TO_POINTER(vaddr),
                            (gpointer) intern_disas);

        /*
         * Check the disassembly to see if a register we care about
         * will be affected by this instruction. This relies on the
         * dissembler doing something sensible for the registers we
         * care about.
         */
        g_auto(GStrv) args = g_strsplit_set(insn_disas, " \t", 2);

        if (args && args[1]) {
            BaseRegister info;

            if (find_base_reg_by_str(args[1], &info)) {
                qemu_plugin_u64 cnt = {
                    .score = base_reg_hits,
                    .offset = (size_t)info.index * sizeof(uint64_t)
                };
                qemu_plugin_register_inline_per_vcpu(insn,
                                                    QEMU_PLUGIN_INLINE_ADD_U64,
                                                     cnt, 1);
            }
        }

        /*
         * If we are tracking system register writes lets check here.
         */
        if (args && track_sysreg_write) {
            if (g_strrstr(args[0], sysreg_ins)) {
                qemu_plugin_u64 write_pc = {
                    .score = last_sysreg_write,
                };
                qemu_plugin_register_inline_per_vcpu(
                    insn, QEMU_PLUGIN_INLINE_STORE_U64,
                    write_pc, vaddr);
            }
        }
    }

    /*
     * On the last instruction store the PC so we can recover if we
     * are missing translations we haven't done yet.
     */
    if (n_insns > 0) {
        qemu_plugin_u64 last_pc = { .score = last_exec_pc };
        qemu_plugin_register_inline_per_vcpu(
                insn, QEMU_PLUGIN_INLINE_STORE_U64,
                last_pc, vaddr);
    }

    g_mutex_unlock(&disas_lock);
}

static void dump_reg(GString *out, GByteArray *value) {
    /* TODO: handle BE properly */
    for (int j = value->len - 1; j >= 0; j--) {
        g_string_append_printf(out, "%02x", value->data[j]);
    }
}

static void check_reg_changes(unsigned int vcpu_index, CPU *cpu, GString *out)
{
    uint64_t *hits = qemu_plugin_scoreboard_find(base_reg_hits, vcpu_index);

    for (int i = 0; i < cpu->registers->len; i++) {
        Register *reg = g_ptr_array_index(cpu->registers, i);
        uint64_t hit_count = hits[reg->index];
        if (hit_count > reg->last_dump_count) {
            BaseRegister base = get_base_reg(reg->index);
            g_autoptr(GByteArray) new_val = g_byte_array_new();
            int bytes = qemu_plugin_read_register(base.handle, new_val);
            g_assert(bytes > 0);
            g_assert(bytes == reg->last->len);
            if (memcmp(reg->last->data, new_val->data, reg->last->len) != 0) {
                g_string_append_printf(out, "  REG: %s is ", base.name);
                dump_reg(out, new_val);
                g_string_append_printf(out, " (previously ");
                dump_reg(out, reg->last);
                g_string_append_printf(out, ", %"PRId64" to %"PRId64" hits)\n", reg->last_dump_count, hit_count);

                /* record the new value */
                g_byte_array_set_size(reg->last, 0);
                g_byte_array_append(reg->last, new_val->data, new_val->len);
            }
            reg->last_dump_count = hit_count;
        }
    }
}


static void vcpu_discon(qemu_plugin_id_t id, unsigned int vcpu_index,
                        enum qemu_plugin_discon_type type, uint64_t from_pc,
                        uint64_t to_pc)
{
    CPU *cpu = get_cpu(vcpu_index);
    g_autoptr(GString) report = g_string_new("");
    const char *type_string;
    uint64_t from_hwaddr;
    const char *disas;

    qemu_plugin_translate_vaddr(from_pc, &from_hwaddr);

    switch (type) {
    case QEMU_PLUGIN_DISCON_INTERRUPT:
        type_string = "irq";
        break;
    case QEMU_PLUGIN_DISCON_EXCEPTION:
        type_string = "exception";
        break;
    case QEMU_PLUGIN_DISCON_HOSTCALL:
        type_string = "host call";
        break;
    default:
        g_assert_not_reached();
        break;
    }

    g_string_append_printf(report,
                           "CPU: %d taking %s from 0x%" PRIx64 " to 0x%" PRIx64 "\n",
                           vcpu_index, type_string, from_pc, to_pc);

    g_mutex_lock(&disas_lock);

    if (show_from_pc) {
        bool le_fallback = false;
        uint64_t *le_pc = qemu_plugin_scoreboard_find(last_exec_pc, vcpu_index);
        disas = g_hash_table_lookup(haddr_disas, GUINT_TO_POINTER(from_pc));
        if (!disas) {
            le_fallback = true;
            disas = g_hash_table_lookup(haddr_disas, GUINT_TO_POINTER(*le_pc));
            g_assert(disas);
        }
        g_string_append_printf(report, "  FROM: 0x%" PRIx64 " %s\t(%s)\n",
                               le_fallback ? *le_pc : from_pc, disas,
                               le_fallback ? "lepc" : "fpc");
    }

    if (track_sysreg_write) {
        uint64_t *last_write = qemu_plugin_scoreboard_find(last_sysreg_write, vcpu_index);
        disas = g_hash_table_lookup(haddr_disas, GUINT_TO_POINTER(*last_write));
        if (disas) {
            g_string_append_printf(report, "  LAST SYSREG: 0x%"PRIx64" %s\n", *last_write, disas);
        }
    }

    g_mutex_unlock(&disas_lock);

    if (base_reg_hits && cpu->registers) {
        check_reg_changes(vcpu_index, cpu, report);
    }

    qemu_plugin_outs(report->str);
}

/**
 * On vcpu exit, print the final state of the registers
 */
static void vcpu_exit(qemu_plugin_id_t id, unsigned int cpu_index)
{
    g_autoptr(GString) result = g_string_new("Register, Value, Accesses ");
    g_autoptr(GByteArray) value = g_byte_array_new();

    g_string_append_printf(result, "for CPU%d\n", cpu_index);
    for (int i = 0; i < base_registers->len; i++) {
        BaseRegister *base = &g_array_index(base_registers, BaseRegister, i);
        qemu_plugin_u64 cnt = {
            .score = base_reg_hits,
            .offset = (size_t)base->index * sizeof(uint64_t)
        };
        uint64_t sum_hits = qemu_plugin_u64_get(cnt, cpu_index );

        if (sum_hits > 0) {
            g_string_append_printf(result, "%s, ", base->name);
            qemu_plugin_read_register(base->handle, value);
            dump_reg(result, value);
            g_string_append_printf(result, ", % "PRId64"\n", sum_hits);
        }
    }
    qemu_plugin_outs(result->str);
}


/*
 * g_pattern_match_string has been deprecated in Glib since 2.70 and
 * will complain about it if you try to use it. Fortunately the
 * signature of both functions is the same making it easy to work
 * around.
 */
static inline
gboolean g_pattern_spec_match_string_qemu(GPatternSpec *pspec,
                                          const gchar *string)
{
#if GLIB_CHECK_VERSION(2, 70, 0)
    return g_pattern_spec_match_string(pspec, string);
#else
    return g_pattern_match_string(pspec, string);
#endif
};
#define g_pattern_spec_match_string(p, s) g_pattern_spec_match_string_qemu(p, s)


static Register *init_vcpu_register(BaseRegister *base)
{
    Register *reg = g_new0(Register, 1);
    int r;

    reg->index = base->index;
    reg->last = g_byte_array_new();

    /* read the initial value */
    r = qemu_plugin_read_register(base->handle, reg->last);
    /* we currently don't handle the bigger ones */
    g_assert(r > 0);
    g_assert(r <= 8);
    return reg;
}

static void free_vcpu_register(gpointer data)
{
    Register *reg = (Register *)data;
    g_byte_array_unref(reg->last);
    g_free(reg);
}

static GPtrArray *registers_init(GArray *reg_list, int vcpu_index)
{
    g_autoptr(GPtrArray) registers = g_ptr_array_new_with_free_func(free_vcpu_register);

    if (!rmatches) {
        return NULL;
    }

    /*
     * Go through each register in the complete list and
     * see if we want to track it.
     */
    for (int r = 0; r < reg_list->len; r++) {
        qemu_plugin_reg_descriptor *rd = &g_array_index(
            reg_list, qemu_plugin_reg_descriptor, r);
        for (int p = 0; p < rmatches->len; p++) {
            g_autoptr(GPatternSpec) pat = g_pattern_spec_new(rmatches->pdata[p]);
            g_autofree gchar *rd_lower = g_utf8_strdown(rd->name, -1);
            if (g_pattern_spec_match_string(pat, rd->name) ||
                g_pattern_spec_match_string(pat, rd_lower)) {
                BaseRegister base = find_or_add_base_register(rd);
                Register *reg = init_vcpu_register(&base);
                g_ptr_array_add(registers, reg);
            }
        }
    }

    return registers->len ? g_steal_pointer(&registers) : NULL;
}

/*
 * Initialise a new vcpu with:
 *   - initial value of registers
 *   - scoreboard to track reg hits
 *   - optional scoreboard to track sysreg writes
 */
static void vcpu_init(qemu_plugin_id_t id, unsigned int vcpu_index)
{
    CPU *c;
    g_autoptr(GArray) reg_list = qemu_plugin_get_registers();

    g_mutex_lock(&vcpu_init_lock);

    c = get_cpu(vcpu_index);

    /* Are more registers available now? */
    if (c->registers && (reg_list->len > c->available_reg_count)) {
        fprintf(stderr, "%s: reset list....\n", __func__);
        g_ptr_array_free(c->registers, true);
        c->registers = NULL;
    }

    c->available_reg_count = reg_list->len;

    if (!c->registers) {
        c->registers = registers_init(reg_list, vcpu_index);
        fprintf(stderr, "%s:%d reglen %d\n", __func__, vcpu_index, c->registers ? c->registers->len : 0);
    }

    if (track_sysreg_write && !last_sysreg_write) {
        last_sysreg_write = qemu_plugin_scoreboard_new(sizeof(uint64_t));
    }

    if (!last_exec_pc) {
        last_exec_pc = qemu_plugin_scoreboard_new(sizeof(uint64_t));
    }

    g_mutex_unlock(&vcpu_init_lock);
}

/*
 * We have to wait until vCPUs are started before we can check the
 * patterns find anything.
 */
static void add_regpat(char *regpat)
{
    if (!rmatches) {
        rmatches = g_ptr_array_new();
    }
    g_ptr_array_add(rmatches, g_strdup(regpat));
}

/**
 * Install the plugin
 */
QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info, int argc,
                                           char **argv)
{
    /* We only work for system emulation */
    if (!info->system_emulation) {
        qemu_plugin_outs("The systrace plugin is for system emulation only.");
        return -1;
    }

    /*
     * Initialize dynamic array to cache vCPU instruction. We also
     * need a hash table to track disassembly.
     */
    cpus = g_array_sized_new(true, true, sizeof(CPU), info->system.max_vcpus);
    g_array_set_size(cpus, info->system.max_vcpus);
    haddr_disas = g_hash_table_new(NULL, NULL);

    base_registers = g_array_new(true, true, sizeof(BaseRegister));
    base_reg_hits = qemu_plugin_scoreboard_new(MAX_TRACKING_REGISTERS * sizeof(uint64_t));

    for (int i = 0; i < argc; i++) {
        char *opt = argv[i];
        g_auto(GStrv) tokens = g_strsplit(opt, "=", 2);
        if (g_strcmp0(tokens[0], "reg") == 0) {
            add_regpat(tokens[1]);
        } else if (g_strcmp0(tokens[0], "tracksw") == 0) {
            track_sysreg_write = true;
            if (tokens[1]) {
                sysreg_ins = g_strdup(tokens[1]);
            } else {
                sysreg_ins = g_strdup("msr");
            }
        } else if (g_strcmp0(tokens[0], "show_frompc") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &show_from_pc)) {
                fprintf(stderr, "boolean argument parsing failed: %s\n", opt);
                return -1;
            }
        } else {
            fprintf(stderr, "option parsing failed: %s\n", opt);
            return -1;
        }
    }

    /* Register init, translation block and exit callbacks */
    qemu_plugin_register_vcpu_init_cb(id, vcpu_init);
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_vcpu_discon_cb(id, QEMU_PLUGIN_DISCON_ALL, vcpu_discon);
    qemu_plugin_register_vcpu_exit_cb(id, vcpu_exit);

    return 0;
}
