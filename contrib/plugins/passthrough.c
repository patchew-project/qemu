/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Minimal linux-user plugin that demonstrates local-library passthrough with
 * the syscall filter API.
 *
 * The guest thunk library (GTL) can:
 * 1) ask this plugin to load a host thunk library (HTL),
 * 2) resolve HTL entry points,
 * 3) invoke resolved HTL entry points with a void *args[] payload.
 *
 * This demo intentionally assumes 64-bit little-endian linux-user guests with
 * guest_base == 0 on a little-endian 64-bit host, so guest virtual addresses
 * are directly usable as host pointers.
 */

#include <glib.h>
#include <gmodule.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <qemu-plugin.h>

#include "passthrough-protocol.h"

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

typedef void (*PassthroughThunkEntry)(void **args, void *ret);

static char *htl_dir;

static char *build_htl_path(const char *guest_library)
{
    g_autofree char *basename = g_path_get_basename(guest_library);
    g_autofree char *htl_basename = NULL;
    const char *so = strstr(basename, ".so");

    g_assert(so != NULL);
    htl_basename = g_strdup_printf("%.*s_HTL%s",
                                   (int)(so - basename), basename, so);
    return g_build_filename(htl_dir, htl_basename, NULL);
}

static bool handle_load_htl(uint64_t library_ptr, uint64_t *sysret)
{
    g_autofree char *htl_path = NULL;
    const char *guest_library = (const char *)(uintptr_t)library_ptr;
    GModule *module;

    g_assert(guest_library != NULL);
    htl_path = build_htl_path(guest_library);
    module = g_module_open(htl_path, G_MODULE_BIND_LOCAL);
    g_assert(module != NULL);

    *sysret = (uint64_t)(uintptr_t)module;
    return true;
}

static bool handle_dlsym(uint64_t handle, uint64_t symbol_ptr, uint64_t *sysret)
{
    const char *symbol = (const char *)(uintptr_t)symbol_ptr;
    GModule *module = (GModule *)(uintptr_t)handle;
    gpointer func = NULL;

    g_assert(module != NULL);
    g_assert(symbol != NULL);
    g_assert(g_module_symbol(module, symbol, &func));
    g_assert(func != NULL);

    *sysret = (uint64_t)(uintptr_t)func;
    return true;
}

static bool handle_invoke(uint64_t func_ptr, uint64_t args_ptr,
                          uint64_t ret_ptr, uint64_t *sysret)
{
    PassthroughThunkEntry entry = (PassthroughThunkEntry)(uintptr_t)func_ptr;

    g_assert(entry != NULL);
    entry((void **)(uintptr_t)args_ptr, (void *)(uintptr_t)ret_ptr);
    *sysret = 0;
    return true;
}

static bool handle_close_htl(uint64_t handle, uint64_t *sysret)
{
    GModule *module = (GModule *)(uintptr_t)handle;

    g_assert(module != NULL);
    g_assert(g_module_close(module));
    *sysret = 0;
    return true;
}

static bool passthrough_syscall_filter(qemu_plugin_id_t id,
                                       unsigned int vcpu_index,
                                       int64_t num, uint64_t a1, uint64_t a2,
                                       uint64_t a3, uint64_t a4, uint64_t a5,
                                       uint64_t a6, uint64_t a7, uint64_t a8,
                                       uint64_t *sysret)
{
    if (num != PASSTHROUGH_MAGIC_SYSCALL) {
        return false;
    }

    switch (a1) {
    case PASSTHROUGH_OP_LOAD_HTL:
        return handle_load_htl(a2, sysret);
    case PASSTHROUGH_OP_DLSYM:
        return handle_dlsym(a2, a3, sysret);
    case PASSTHROUGH_OP_INVOKE:
        return handle_invoke(a2, a3, a4, sysret);
    case PASSTHROUGH_OP_CLOSE_HTL:
        return handle_close_htl(a2, sysret);
    default:
        g_assert_not_reached();
    }
}

static void passthrough_exit(qemu_plugin_id_t id, void *userdata)
{
    g_free(htl_dir);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    int i;

    if (info->system_emulation) {
        fprintf(stderr,
                "passthrough: this demo supports linux-user only\n");
        return -1;
    }

    for (i = 0; i < argc; i++) {
        g_auto(GStrv) tokens = g_strsplit(argv[i], "=", 2);

        if (g_strcmp0(tokens[0], "htl_dir") == 0) {
            g_assert(tokens[1] != NULL);
            g_free(htl_dir);
            htl_dir = g_strdup(tokens[1]);
            continue;
        }

        fprintf(stderr, "passthrough: unsupported argument: %s\n", argv[i]);
        return -1;
    }

    if (htl_dir == NULL) {
        fprintf(stderr,
                "passthrough: missing required argument htl_dir=PATH\n");
        return -1;
    }

    qemu_plugin_register_vcpu_syscall_filter_cb(id, passthrough_syscall_filter);
    qemu_plugin_register_atexit_cb(id, passthrough_exit, NULL);
    return 0;
}
