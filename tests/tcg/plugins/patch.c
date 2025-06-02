/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This plugin patches instructions matching a pattern to a different
 * instruction as they execute
 *
 */

#include "glib.h"
#include "glibconfig.h"

#include <qemu-plugin.h>
#include <string.h>
#include <stdio.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static bool use_hwaddr;
static bool debug_insns;
static GByteArray *target_data;
static GByteArray *patch_data;

/**
 * Parse a string of hexadecimal digits into a GByteArray. The string must be
 * even length
 */
static GByteArray *str_to_bytes(const char *str)
{
    GByteArray *bytes = g_byte_array_new();
    char byte[3] = {0};
    size_t len = strlen(str);
    guint8 value = 0;

    if (len % 2 != 0) {
        g_byte_array_free(bytes, true);
        return NULL;
    }

    for (size_t i = 0; i < len; i += 2) {
        byte[0] = str[i];
        byte[1] = str[i + 1];
        value = (guint8)g_ascii_strtoull(byte, NULL, 16);
        g_byte_array_append(bytes, &value, 1);
    }

    return bytes;
}

static void patch_hwaddr(unsigned int vcpu_index, void *userdata)
{
    uint64_t addr = (uint64_t)userdata;
    GString *str = g_string_new(NULL);
    g_string_printf(str, "patching: @0x%"
                    PRIx64 "\n",
                    addr);
    qemu_plugin_outs(str->str);
    g_string_free(str, true);

    enum qemu_plugin_hwaddr_operation_result result =
        qemu_plugin_write_memory_hwaddr(addr, patch_data);


    if (result != QEMU_PLUGIN_HWADDR_OPERATION_OK) {
        GString *errmsg = g_string_new(NULL);
        g_string_printf(errmsg, "Failed to write memory: %d\n", result);
        qemu_plugin_outs(errmsg->str);
        g_string_free(errmsg, true);
        return;
    }

    GByteArray *read_data = g_byte_array_new();

    result = qemu_plugin_read_memory_hwaddr(addr, read_data,
                                            patch_data->len);

    qemu_plugin_outs("Reading memory...\n");

    if (result != QEMU_PLUGIN_HWADDR_OPERATION_OK) {
        GString *errmsg = g_string_new(NULL);
        g_string_printf(errmsg, "Failed to read memory: %d\n", result);
        qemu_plugin_outs(errmsg->str);
        g_string_free(errmsg, true);
        return;
    }

    if (memcmp(patch_data->data, read_data->data, patch_data->len) != 0) {
        qemu_plugin_outs("Failed to read back written data\n");
    }

    qemu_plugin_outs("Success!\n");

    return;
}

static void patch_vaddr(unsigned int vcpu_index, void *userdata)
{
    uint64_t addr = (uint64_t)userdata;
    uint64_t hwaddr = 0;
    if (!qemu_plugin_translate_vaddr(addr, &hwaddr)) {
        qemu_plugin_outs("Failed to translate vaddr\n");
        return;
    }
    GString *str = g_string_new(NULL);
    g_string_printf(str, "patching: @0x%"
                    PRIx64 " hw: @0x%" PRIx64 "\n",
                    addr, hwaddr);
    qemu_plugin_outs(str->str);
    g_string_free(str, true);

    qemu_plugin_outs("Writing memory (vaddr)...\n");

    if (!qemu_plugin_write_memory_vaddr(addr, patch_data)) {
        qemu_plugin_outs("Failed to write memory\n");
        return;
    }

    qemu_plugin_outs("Reading memory (vaddr)...\n");


    GByteArray *read_data = g_byte_array_new();

    if (!qemu_plugin_read_memory_vaddr(addr, read_data, patch_data->len)) {
        qemu_plugin_outs("Failed to read memory\n");
        return;
    }

    if (memcmp(patch_data->data, read_data->data, patch_data->len) != 0) {
        qemu_plugin_outs("Failed to read back written data\n");
    }

    qemu_plugin_outs("Success!\n");

    return;
}

static void debug_disas(unsigned int vcpu_index, void *userdata)
{
    GString *debug_info = (GString *)userdata;
    qemu_plugin_outs(debug_info->str);
}

static void debug_print_newline(unsigned int vcpu_index, void *userdata)
{
    qemu_plugin_outs("\n");
}

/*
 * Callback on translation of a translation block.
 */
static void vcpu_tb_trans_cb(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    uint64_t addr = 0;
    GByteArray *insn_data = g_byte_array_new();
    for (size_t i = 0; i < qemu_plugin_tb_n_insns(tb); i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);

        if (use_hwaddr) {
            uint64_t vaddr = qemu_plugin_insn_vaddr(insn);
            if (!qemu_plugin_translate_vaddr(vaddr, &addr)) {
                qemu_plugin_outs("Failed to translate vaddr\n");
                continue;
            }
        } else {
            addr = qemu_plugin_insn_vaddr(insn);
        }

        g_byte_array_set_size(insn_data, qemu_plugin_insn_size(insn));
        qemu_plugin_insn_data(insn, insn_data->data, insn_data->len);

        if (insn_data->len >= target_data->len &&
            !memcmp(insn_data->data, target_data->data,
                    MIN(target_data->len, insn_data->len))) {
            if (use_hwaddr) {
                qemu_plugin_register_vcpu_tb_exec_cb(tb, patch_hwaddr,
                                                     QEMU_PLUGIN_CB_NO_REGS,
                                                     (void *)addr);
            } else {
                qemu_plugin_register_vcpu_tb_exec_cb(tb, patch_vaddr,
                                                     QEMU_PLUGIN_CB_NO_REGS,
                                                     (void *)addr);
            }
        }
    }
    for (size_t i = 0; i < qemu_plugin_tb_n_insns(tb); i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        uint64_t vaddr = qemu_plugin_insn_vaddr(insn);
        uint64_t hwaddr = (uint64_t)qemu_plugin_insn_haddr(insn);
        uint64_t translated_hwaddr = 0;
        if (!qemu_plugin_translate_vaddr(vaddr, &translated_hwaddr)) {
            qemu_plugin_outs("Failed to translate vaddr\n");
            continue;
        }
        char *disas = qemu_plugin_insn_disas(insn);
        GString *str = g_string_new(NULL);
        g_string_printf(str,
                        "vaddr: 0x%" PRIx64 " hwaddr: 0x%" PRIx64
                        " translated: 0x%" PRIx64 " : %s\n",
                        vaddr, hwaddr, translated_hwaddr, disas);
        g_free(disas);
        if (debug_insns) {
            qemu_plugin_register_vcpu_insn_exec_cb(insn, debug_disas,
                                                   QEMU_PLUGIN_CB_NO_REGS,
                                                   str);
        }

    }

    if (debug_insns) {
        qemu_plugin_register_vcpu_tb_exec_cb(tb, debug_print_newline,
                                             QEMU_PLUGIN_CB_NO_REGS,
                                             NULL);
    }

    g_byte_array_free(insn_data, true);
}

static void usage(void)
{
    fprintf(stderr, "Usage: <lib>,target=<target>,patch=<patch>"
            "[,use_hwaddr=<use_hwaddr>]"
            "[,debug_insns=<debug_insns>]\n");
}

/*
 * Called when the plugin is installed
 */
QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info, int argc,
                                           char **argv)
{

    use_hwaddr = true;
    debug_insns = false;
    target_data = NULL;
    patch_data = NULL;

    if (argc > 4) {
        usage();
        return -1;
    }

    for (size_t i = 0; i < argc; i++) {
        char *opt = argv[i];
        g_auto(GStrv) tokens = g_strsplit(opt, "=", 2);
        if (g_strcmp0(tokens[0], "use_hwaddr") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &use_hwaddr)) {
                fprintf(stderr,
                        "Failed to parse boolean argument use_hwaddr\n");
                return -1;
            }
        } else if (g_strcmp0(tokens[0], "debug_insns") == 0) {
            if (!qemu_plugin_bool_parse(tokens[0], tokens[1], &debug_insns)) {
                fprintf(stderr,
                        "Failed to parse boolean argument debug_insns\n");
                return -1;
            }
        } else if (g_strcmp0(tokens[0], "target") == 0) {
            target_data = str_to_bytes(tokens[1]);
            if (!target_data) {
                fprintf(stderr,
                         "Failed to parse target bytes.\n");
                return -1;
            }
        } else if (g_strcmp0(tokens[0], "patch") == 0) {
            patch_data = str_to_bytes(tokens[1]);
            if (!patch_data) {
                fprintf(stderr, "Failed to parse patch bytes.\n");
                return -1;
            }
        } else {
            fprintf(stderr, "Unknown argument: %s\n", tokens[0]);
            usage();
            return -1;
        }
    }

    if (!target_data) {
        fprintf(stderr, "target argument is required\n");
        usage();
        return -1;
    }

    if (!patch_data) {
        fprintf(stderr, "patch argument is required\n");
        usage();
        return -1;
    }

    if (target_data->len != patch_data->len) {
        fprintf(stderr, "Target and patch data must be the same length\n");
        return -1;
    }

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans_cb);

    return 0;
}
