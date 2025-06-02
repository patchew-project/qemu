/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This plugin implements a simple hypercall interface for guests (both system
 * and user mode) to call certain operations from the host.
 */
#include "glib.h"
#include "glibconfig.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

#define AARCH64_N_HYPERCALL_INSNS  (28)
#define AARCH64_HYPERCALL_INSN_LEN (4)
#define AARCH64_HYPERCALL_MAX (AARCH64_N_HYPERCALL_INSNS)
#define ARM_N_HYPERCALL_INSNS  (12)
#define ARM_HYPERCALL_INSN_LEN (4)
#define ARM_HYPERCALL_MAX (ARM_N_HYPERCALL_INSNS)
#define X86_HYPERCALL_INSN_LEN (2)
#define X86_HYPERCALL_VALUE_BASE (0x4711)
#define X86_HYPERCALL_MAX (0x10000)
#define N_HYPERCALL_ARGS (4)

static bool ignore_unsupported;

static struct qemu_plugin_register *get_register(const char *name);
static uint64_t byte_array_to_uint64(GByteArray *buf);

enum HypercallInsnType {
    CONSTANT,
    CALLBACK,
};


/*
 * Checks an instruction and returns its hypercall number, if it is
 * a hypercall instruction, or -1 if it is not. Called at execution
 * time.
 */
typedef int32_t (*hypercall_nr_cb)(GByteArray *);

/*
 * Checks an instruction and returns whether it is a hypercall, or -1 if it is
 * not. Called at execution time.
 */
typedef bool (*is_hypercall_cb)(GByteArray *);

/*
 * Specifies a Hypercall for an architecture:
 *
 * - Architecture name
 * - Whether it is enabled
 * - The hypercall instruction
 * - The register names to pass the hypercall # and args
 */
struct HypercallSpec {
    const bool enabled;
    const char *name;
    const bool le;
    const char *args[N_HYPERCALL_ARGS];
    const hypercall_nr_cb hypercall_nr_cb;
    const is_hypercall_cb is_hypercall_cb;
};

static int32_t aarch64_hypercall_nr_cb(GByteArray *insn)
{
    if (insn->len != AARCH64_HYPERCALL_INSN_LEN) {
        return -1;
    }

    static const uint8_t
    hypercall_insns[AARCH64_N_HYPERCALL_INSNS][AARCH64_HYPERCALL_INSN_LEN] = {
        { 0xaa, 0x4, 0x0, 0x84 },
        { 0xaa, 0x5, 0x0, 0xa5 },
        { 0xaa, 0x6, 0x0, 0xc6 },
        { 0xaa, 0x7, 0x0, 0xe7 },
        { 0xaa, 0x8, 0x1, 0x8 },
        { 0xaa, 0x9, 0x1, 0x29 },
        { 0xaa, 0xa, 0x1, 0x4a },
        { 0xaa, 0xb, 0x1, 0x6b },
        { 0xaa, 0xc, 0x1, 0x8c },
        { 0xaa, 0xd, 0x1, 0xad },
        { 0xaa, 0xe, 0x1, 0xce },
        { 0xaa, 0xf, 0x1, 0xef },
        { 0xaa, 0x10, 0x2, 0x10 },
        { 0xaa, 0x11, 0x2, 0x31 },
        { 0xaa, 0x12, 0x2, 0x52 },
        { 0xaa, 0x13, 0x2, 0x73 },
        { 0xaa, 0x14, 0x2, 0x94 },
        { 0xaa, 0x15, 0x2, 0xb5 },
        { 0xaa, 0x16, 0x2, 0xd6 },
        { 0xaa, 0x17, 0x2, 0xf7 },
        { 0xaa, 0x18, 0x3, 0x18 },
        { 0xaa, 0x19, 0x3, 0x39 },
        { 0xaa, 0x1a, 0x3, 0x5a },
        { 0xaa, 0x1b, 0x3, 0x7b },
        { 0xaa, 0x1c, 0x3, 0x9c },
        { 0xaa, 0x1d, 0x3, 0xbd },
        { 0xaa, 0x1e, 0x3, 0xde },
        { 0xaa, 0x1f, 0x3, 0xff },
    };

    for (int32_t i = 0; i < AARCH64_N_HYPERCALL_INSNS; i++) {
        if (!memcmp(hypercall_insns[i], insn->data, insn->len)) {
            return i;
        }
    }
    return -1;
}

static bool aarch64_is_hypercall_cb(GByteArray *insn)
{
    return aarch64_hypercall_nr_cb(insn) < 0;
}


static int32_t aarch64_be_hypercall_nr_cb(GByteArray *insn)
{
    if (insn->len != AARCH64_HYPERCALL_INSN_LEN) {
        return -1;
    }

    static const uint8_t
    hypercall_insns[AARCH64_N_HYPERCALL_INSNS][AARCH64_HYPERCALL_INSN_LEN] = {
        {0x84, 0x0, 0x4, 0xaa},
        {0xa5, 0x0, 0x5, 0xaa},
        {0xc6, 0x0, 0x6, 0xaa},
        {0xe7, 0x0, 0x7, 0xaa},
        {0x8, 0x1, 0x8, 0xaa},
        {0x29, 0x1, 0x9, 0xaa},
        {0x4a, 0x1, 0xa, 0xaa},
        {0x6b, 0x1, 0xb, 0xaa},
        {0x8c, 0x1, 0xc, 0xaa},
        {0xad, 0x1, 0xd, 0xaa},
        {0xce, 0x1, 0xe, 0xaa},
        {0xef, 0x1, 0xf, 0xaa},
        {0x10, 0x2, 0x10, 0xaa},
        {0x31, 0x2, 0x11, 0xaa},
        {0x52, 0x2, 0x12, 0xaa},
        {0x73, 0x2, 0x13, 0xaa},
        {0x94, 0x2, 0x14, 0xaa},
        {0xb5, 0x2, 0x15, 0xaa},
        {0xd6, 0x2, 0x16, 0xaa},
        {0xf7, 0x2, 0x17, 0xaa},
        {0x18, 0x3, 0x18, 0xaa},
        {0x39, 0x3, 0x19, 0xaa},
        {0x5a, 0x3, 0x1a, 0xaa},
        {0x7b, 0x3, 0x1b, 0xaa},
        {0x9c, 0x3, 0x1c, 0xaa},
        {0xbd, 0x3, 0x1d, 0xaa},
        {0xde, 0x3, 0x1e, 0xaa},
        {0xff, 0x3, 0x1f, 0xaa},
    };

    for (int32_t i = 0; i < AARCH64_N_HYPERCALL_INSNS; i++) {
        if (!memcmp(hypercall_insns[i], insn->data, insn->len)) {
            return i;
        }
    }
    return -1;
}

static bool aarch64_be_is_hypercall_cb(GByteArray *insn)
{
    return aarch64_be_hypercall_nr_cb(insn) < 0;
}


static int32_t arm_hypercall_nr_cb(GByteArray *insn)
{
    if (insn->len != ARM_HYPERCALL_INSN_LEN) {
        return -1;
    }

    static const uint8_t
    hypercall_insns[ARM_N_HYPERCALL_INSNS][ARM_HYPERCALL_INSN_LEN] = {
        { 0xe1, 0x84, 0x40, 0x4 },
        { 0xe1, 0x85, 0x50, 0x5 },
        { 0xe1, 0x86, 0x60, 0x6 },
        { 0xe1, 0x87, 0x70, 0x7 },
        { 0xe1, 0x88, 0x80, 0x8 },
        { 0xe1, 0x89, 0x90, 0x9 },
        { 0xe1, 0x8a, 0xa0, 0xa },
        { 0xe1, 0x8b, 0xb0, 0xb },
        { 0xe1, 0x8c, 0xc0, 0xc },
        { 0xe1, 0x8d, 0xd0, 0xd },
        { 0xe1, 0x8e, 0xe0, 0xe },
        { 0xe1, 0x8f, 0xf0, 0xf },
    };

    for (int32_t i = 0; i < ARM_N_HYPERCALL_INSNS; i++) {
        if (!memcmp(hypercall_insns[i], insn->data, insn->len)) {
            return i;
        }
    }
    return -1;
}

static bool arm_is_hypercall_cb(GByteArray *insn)
{
    return arm_hypercall_nr_cb(insn) < 0;
}

static int32_t arm_be_hypercall_nr_cb(GByteArray *insn)
{
    if (insn->len != ARM_HYPERCALL_INSN_LEN) {
        return -1;
    }

    static const uint8_t
    hypercall_insns[ARM_N_HYPERCALL_INSNS][ARM_HYPERCALL_INSN_LEN] = {
        {0x4, 0x40, 0x84, 0xe1},
        {0x5, 0x50, 0x85, 0xe1},
        {0x6, 0x60, 0x86, 0xe1},
        {0x7, 0x70, 0x87, 0xe1},
        {0x8, 0x80, 0x88, 0xe1},
        {0x9, 0x90, 0x89, 0xe1},
        {0xa, 0xa0, 0x8a, 0xe1},
        {0xb, 0xb0, 0x8b, 0xe1},
        {0xc, 0xc0, 0x8c, 0xe1},
        {0xd, 0xd0, 0x8d, 0xe1},
        {0xe, 0xe0, 0x8e, 0xe1},
        {0xf, 0xf0, 0x8f, 0xe1},
    };

    for (int32_t i = 0; i < ARM_N_HYPERCALL_INSNS; i++) {
        if (!memcmp(hypercall_insns[i], insn->data, insn->len)) {
            return i;
        }
    }
    return -1;
}

static bool arm_be_is_hypercall_cb(GByteArray *insn)
{
    return arm_be_hypercall_nr_cb(insn) < 0;
}

static int32_t x86_64_hypercall_nr_cb(GByteArray *insn)
{
    if (insn->len != X86_HYPERCALL_INSN_LEN) {
        return -1;
    }

    uint8_t cpuid[] = { 0x0f, 0xa2 };
    if (!memcmp(cpuid, insn->data, insn->len)) {
        GByteArray *reg = g_byte_array_new();
        qemu_plugin_read_register(get_register("rax"), reg);
        uint64_t value = byte_array_to_uint64(reg);
        g_byte_array_free(reg, true);

        if (!(value & X86_HYPERCALL_VALUE_BASE)) {
            return -1;
        }

        value = (value >> 16) & 0xffff;

        if (value >= X86_HYPERCALL_MAX) {
            return -1;
        }

        return (int32_t)value;
    }

    return -1;
}

static bool x86_64_is_hypercall_cb(GByteArray *insn)
{
    if (insn->len != X86_HYPERCALL_INSN_LEN) {
        return false;
    }

    uint8_t cpuid[] = { 0x0f, 0xa2 };
    if (!memcmp(cpuid, insn->data, insn->len)) {
        return true;
    }

    return false;
}

static int32_t i386_hypercall_nr_cb(GByteArray *insn)
{
    if (insn->len != X86_HYPERCALL_INSN_LEN) {
        return -1;
    }

    uint8_t cpuid[] = { 0x0f, 0xa2 };
    if (!memcmp(cpuid, insn->data, insn->len)) {
        GByteArray *reg = g_byte_array_new();
        qemu_plugin_read_register(get_register("eax"), reg);
        uint64_t value = byte_array_to_uint64(reg);
        g_byte_array_free(reg, true);

        if (!(value & X86_HYPERCALL_VALUE_BASE)) {
            return -1;
        }

        value = (value >> 16) & 0xffff;

        if (value >= X86_HYPERCALL_MAX) {
            return -1;
        }
        return (int32_t)value;
    }

    return -1;

}

static bool i386_is_hypercall_cb(GByteArray *insn)
{
    if (insn->len != X86_HYPERCALL_INSN_LEN) {
        return false;
    }

    uint8_t cpuid[] = { 0x0f, 0xa2 };
    if (!memcmp(cpuid, insn->data, insn->len)) {
        return true;
    }

    return false;

}

static const struct HypercallSpec *hypercall_spec;

static const struct HypercallSpec hypercall_specs[] = {
    { true, "aarch64", true, {
            "x0", "x1", "x2", "x3",
        }, aarch64_hypercall_nr_cb, aarch64_is_hypercall_cb
    },
    { true, "aarch64_be", false,  {
            "x0", "x1", "x2", "x3",
        }, aarch64_be_hypercall_nr_cb, aarch64_be_is_hypercall_cb
    },
    { true, "arm", true,  {
            "r0", "r1", "r2", "r3",
        }, arm_hypercall_nr_cb, arm_is_hypercall_cb
    },
    { true, "armeb", false,  {
            "r0", "r1", "r2", "r3"
        }, arm_be_hypercall_nr_cb, arm_be_is_hypercall_cb
    },
    { true, "i386", true, {
            "edi", "esi", "edx", "ecx"
        }, i386_hypercall_nr_cb, i386_is_hypercall_cb
    },
    { true, "x86_64", true, {
            "rdi", "rsi", "rdx", "rcx"

        }, x86_64_hypercall_nr_cb, x86_64_is_hypercall_cb
    },
    { false, NULL, .le = false,  {NULL, NULL, NULL, NULL}, NULL},
};

static GArray *hypercall_insns;

/*
 * Returns a handle to a register with a given name, or NULL if there is no
 * such register.
 */
static struct qemu_plugin_register *get_register(const char *name)
{
    GArray *registers = qemu_plugin_get_registers();

    struct qemu_plugin_register *handle = NULL;

    qemu_plugin_reg_descriptor *reg_descriptors =
        (qemu_plugin_reg_descriptor *)registers->data;

    for (size_t i = 0; i < registers->len; i++) {
        if (!strcmp(reg_descriptors[i].name, name)) {
            handle = reg_descriptors[i].handle;
        }
    }

    g_array_free(registers, true);

    return handle;
}

/*
 * Transforms a byte array with at most 8 entries into a uint64_t
 * depending on the target machine's endianness.
 */
static uint64_t byte_array_to_uint64(GByteArray *buf)
{
    uint64_t value = 0;
    if (hypercall_spec->le) {
        for (int i = 0; i < buf->len && i < sizeof(uint64_t); i++) {
            value |= ((uint64_t)buf->data[i]) << (i * 8);
        }
    } else {
        for (int i = 0; i < buf->len && i < sizeof(uint64_t); i++) {
            value |= ((uint64_t)buf->data[i]) << ((buf->len - 1 - i) * 8);
        }
    }
    return value;
}

/*
 * Handle a "hypercall" instruction, which has some special meaning for this
 * plugin.
 */
static void hypercall(unsigned int vcpu_index, void *userdata)
{
    GByteArray *insn_data = (GByteArray *)userdata;
    int32_t hypercall_nr = hypercall_spec->hypercall_nr_cb(insn_data);

    if (hypercall_nr < 0) {
        return;
    }

    uint64_t args[N_HYPERCALL_ARGS] = {0};
    GByteArray *buf = g_byte_array_new();
    for (size_t i = 0; i < N_HYPERCALL_ARGS; i++) {
        g_byte_array_set_size(buf, 0);
        struct qemu_plugin_register *reg =
            get_register(hypercall_spec->args[i]);
        qemu_plugin_read_register(reg, buf);
        args[i] = byte_array_to_uint64(buf);
    }
    g_byte_array_free(buf, true);

    switch (hypercall_nr) {
    /*
     * The write hypercall (#0x01) tells the plugin to write random bytes
     * of a given size into the memory of the emulated system at a particular
     * vaddr
     */
    case 1: {
        GByteArray *data = g_byte_array_new();
        g_byte_array_set_size(data, args[1]);
        for (uint64_t i = 0; i < args[1]; i++) {
            data->data[i] = (uint8_t)g_random_int();
        }
        qemu_plugin_write_memory_vaddr(args[0], data);
        break;
    }
    default:
        break;
    }
}

/*
 * Callback on translation of a translation block.
 */
static void vcpu_tb_trans_cb(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    for (size_t i = 0; i < qemu_plugin_tb_n_insns(tb); i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        GByteArray *insn_data = g_byte_array_new();
        size_t insn_len = qemu_plugin_insn_size(insn);
        g_byte_array_set_size(insn_data, insn_len);
        qemu_plugin_insn_data(insn, insn_data->data, insn_data->len);

        if (hypercall_spec->is_hypercall_cb(insn_data)) {
            g_array_append_val(hypercall_insns, insn_data);
            qemu_plugin_register_vcpu_insn_exec_cb(insn, hypercall,
                                                   QEMU_PLUGIN_CB_R_REGS,
                                                   (void *)insn_data);
        } else {
            g_byte_array_free(insn_data, true);
        }

    }
}

static void atexit_cb(qemu_plugin_id_t id, void *userdata)
{
    for (size_t i = 0; i < hypercall_insns->len; i++) {
        g_byte_array_free(g_array_index(hypercall_insns, GByteArray *, i),
                          true);
    }

    g_array_free(hypercall_insns, true);
}

static void usage(void)
{
    fprintf(stderr,
            "Usage: <lib>,[ignore_unsupported=<ignore_unsupported>]");
}

/*
 * Called when the plugin is installed
 */
QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info, int argc,
                                           char **argv)
{
    if (argc > 1) {
        usage();
        return -1;
    }

    for (size_t i = 0; i < argc; i++) {
        char *opt = argv[i];
        g_auto(GStrv) tokens = g_strsplit(opt, "=", 2);
        if (g_strcmp0(tokens[0], "ignore_unsupported") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0],
                                    tokens[1], &ignore_unsupported)) {
                fprintf(stderr,
                        "Failed to parse argument ignore_unsupported\n");
                return -1;
            }
        } else {
            fprintf(stderr, "Unknown argument: %s\n", tokens[0]);
            usage();
            return -1;
        }
    }


    hypercall_spec = &hypercall_specs[0];

    while (hypercall_spec->name != NULL) {
        if (!strcmp(hypercall_spec->name, info->target_name)) {
            break;
        }
        hypercall_spec++;
    }

    if (hypercall_spec->name == NULL || !hypercall_spec->enabled) {
        qemu_plugin_outs("Error: no hypercall spec.");
        if (ignore_unsupported) {
            return 0;
        }
        return -1;
    }

    hypercall_insns = g_array_new(true, true, sizeof(GByteArray *));

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans_cb);
    qemu_plugin_register_atexit_cb(id, atexit_cb, NULL);

    return 0;
}
