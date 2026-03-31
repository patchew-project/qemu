/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Minimal linux-user plugin that demonstrates syscall filtering for
 * local library interception with a host zlib compression example.
 *
 * When the guest dynamic loader attempts to open "./libdemo-zlib.so", this
 * plugin intercepts open(), openat(), or openat2() and instead returns a file
 * descriptor for "libdemo-zlib-thunk.so" in the same directory. The thunk
 * library then forwards compression requests through magic syscalls, which are
 * handled by this plugin and executed by the host's zlib implementation.
 *
 * This demo intentionally assumes a linux-user run with guest_base == 0 on a
 * little-endian 64-bit host, so guest virtual addresses are directly usable
 * as host pointers.
 */

#include <fcntl.h>
#include <glib.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

#define MAGIC_SYSCALL 4096
#define ZLIB_COMPRESS_OP_BOUND 1
#define ZLIB_COMPRESS_OP_COMPRESS2 2
#define ZLIB_COMPRESS_OP_UNCOMPRESS 3
#define ZLIB_COMPRESS_LIBRARY "libdemo-zlib.so"
#define ZLIB_COMPRESS_THUNK_LIBRARY "libdemo-zlib-thunk.so"
#define ZLIB_COMPRESS_MAX_INPUT (64 * 1024 * 1024)
#define ZLIB_COMPRESS_MAX_BUFFER (128 * 1024 * 1024)
#define GUEST_STRING_CHUNK 64
#define GUEST_STRING_LIMIT (1 << 20)
#define X86_64_OPEN_NR 2
#define X86_64_OPENAT_NR 257
#define X86_64_OPENAT2_NR 437

typedef struct GuestOpenHow {
    uint64_t flags;
    uint64_t mode;
} GuestOpenHow;

static char *read_guest_cstring(uint64_t addr)
{
    g_autoptr(GByteArray) data = g_byte_array_sized_new(GUEST_STRING_CHUNK);
    g_autoptr(GString) str = g_string_sized_new(GUEST_STRING_CHUNK);
    size_t offset;

    for (offset = 0;
         offset < GUEST_STRING_LIMIT;
         offset += GUEST_STRING_CHUNK) {
        g_byte_array_set_size(data, GUEST_STRING_CHUNK);
        if (!qemu_plugin_read_memory_vaddr(addr + offset, data,
                                           GUEST_STRING_CHUNK)) {
            return NULL;
        }

        for (guint i = 0; i < data->len; i++) {
            if (data->data[i] == '\0') {
                return g_string_free(g_steal_pointer(&str), FALSE);
            }
            g_string_append_c(str, data->data[i]);
        }
    }

    return NULL;
}

static void read_guest_buffer(uint64_t addr, void *dst, size_t len)
{
    g_autoptr(GByteArray) data = g_byte_array_sized_new(len);

    if (len == 0) {
        return;
    }

    g_byte_array_set_size(data, len);
    g_assert(qemu_plugin_read_memory_vaddr(addr, data, len));
    memcpy(dst, data->data, len);
}

static void read_guest_open_how(uint64_t addr, uint64_t guest_size,
                                GuestOpenHow *how)
{
    g_assert(guest_size >= sizeof(*how));
    read_guest_buffer(addr, how, sizeof(*how));
}
static bool guest_path_matches_zlib_compress(const char *path)
{
    g_autofree char *basename = g_path_get_basename(path);

    return strcmp(basename, ZLIB_COMPRESS_LIBRARY) == 0;
}

static char *build_thunk_path(const char *path)
{
    g_autofree char *dirname = g_path_get_dirname(path);

    if (strcmp(dirname, ".") == 0) {
        return g_strdup(ZLIB_COMPRESS_THUNK_LIBRARY);
    }

    return g_build_filename(dirname, ZLIB_COMPRESS_THUNK_LIBRARY, NULL);
}

static bool handle_library_open(int64_t num, uint64_t a1, uint64_t a2,
                                uint64_t a3, uint64_t a4, uint64_t *sysret)
{
    g_autofree char *path = NULL;
    g_autofree char *thunk_path = NULL;
    g_autofree char *out = NULL;
    GuestOpenHow how = { 0 };
    uint64_t path_addr;
    int dirfd;
    int flags;
    mode_t mode;
    int fd;

    if (num == X86_64_OPEN_NR) {
        dirfd = AT_FDCWD;
        path_addr = a1;
        flags = (int)a2;
        mode = (mode_t)a3;
    } else if (num == X86_64_OPENAT_NR) {
        dirfd = (int)a1;
        path_addr = a2;
        flags = (int)a3;
        mode = (mode_t)a4;
    } else if (num == X86_64_OPENAT2_NR) {
        dirfd = (int)a1;
        path_addr = a2;
        flags = 0;
        mode = 0;
    } else {
        return false;
    }

    path = read_guest_cstring(path_addr);
    if (path == NULL || !guest_path_matches_zlib_compress(path)) {
        return false;
    }
    if (num == X86_64_OPENAT2_NR) {
        read_guest_open_how(a3, a4, &how);
        flags = (int)how.flags;
        mode = (mode_t)how.mode;
    }
    thunk_path = build_thunk_path(path);
    if (access(thunk_path, F_OK) != 0) {
        return false;
    }

    fd = openat(dirfd, thunk_path, flags, mode);
    g_assert(fd >= 0);

    *sysret = fd;
    out = g_strdup_printf("syscall_filter_zlib: redirected %s -> %s (fd=%d)\n",
                          path, thunk_path, fd);
    qemu_plugin_outs(out);
    return true;
}

static bool handle_compress_bound(int64_t num, uint64_t a1, uint64_t a2,
                                  uint64_t *sysret)
{
    if (num != MAGIC_SYSCALL || a1 != ZLIB_COMPRESS_OP_BOUND) {
        return false;
    }

    if (a2 > ZLIB_COMPRESS_MAX_INPUT) {
        *sysret = 0;
        return true;
    }

    *sysret = compressBound((uLong)a2);
    return true;
}

static bool handle_compress2(int64_t num, uint64_t a1, uint64_t a2,
                             uint64_t a3, uint64_t a4, uint64_t a5,
                             uint64_t a6, uint64_t *sysret)
{
    g_autofree char *out = NULL;
    const Bytef *source;
    Bytef *dest;
    uLongf *dest_lenp;
    uLongf guest_dest_len;
    int status;
    int level = (int)a6;

    if (num != MAGIC_SYSCALL || a1 != ZLIB_COMPRESS_OP_COMPRESS2) {
        return false;
    }

    g_assert(a2 != 0 && a4 != 0 && a5 != 0);
    g_assert(level == Z_DEFAULT_COMPRESSION ||
             (level >= Z_NO_COMPRESSION && level <= Z_BEST_COMPRESSION));

    dest_lenp = (uLongf *)(uintptr_t)a5;
    guest_dest_len = *dest_lenp;

    g_assert(a3 <= ZLIB_COMPRESS_MAX_INPUT);
    g_assert(guest_dest_len <= ZLIB_COMPRESS_MAX_BUFFER);

    source = (const Bytef *)(uintptr_t)a2;
    dest = (Bytef *)(uintptr_t)a4;
    *dest_lenp = guest_dest_len;
    status = compress2(dest, dest_lenp, source, (uLong)a3, level);

    if (status != Z_OK) {
        *sysret = (uint64_t)(uint32_t)status;
        return true;
    }

    *sysret = Z_OK;
    out = g_strdup_printf(
        "syscall_filter_zlib: compressed %" PRIu64
        " guest bytes to %lu host bytes\n",
                          a3, (unsigned long)*dest_lenp);
    qemu_plugin_outs(out);
    return true;
}

static bool handle_uncompress(int64_t num, uint64_t a1, uint64_t a2,
                              uint64_t a3, uint64_t a4, uint64_t a5,
                              uint64_t *sysret)
{
    g_autofree char *out = NULL;
    const Bytef *source;
    Bytef *dest;
    uLongf *dest_lenp;
    uLongf guest_dest_len;
    int status;

    if (num != MAGIC_SYSCALL || a1 != ZLIB_COMPRESS_OP_UNCOMPRESS) {
        return false;
    }

    g_assert(a2 != 0 && a4 != 0 && a5 != 0);

    dest_lenp = (uLongf *)(uintptr_t)a5;
    guest_dest_len = *dest_lenp;

    g_assert(a3 <= ZLIB_COMPRESS_MAX_BUFFER);
    g_assert(guest_dest_len <= ZLIB_COMPRESS_MAX_INPUT);

    source = (const Bytef *)(uintptr_t)a2;
    dest = (Bytef *)(uintptr_t)a4;
    *dest_lenp = guest_dest_len;
    status = uncompress(dest, dest_lenp, source, (uLong)a3);

    if (status != Z_OK) {
        *sysret = (uint64_t)(uint32_t)status;
        return true;
    }

    *sysret = Z_OK;
    out = g_strdup_printf(
        "syscall_filter_zlib: uncompressed %" PRIu64
        " guest bytes to %lu host bytes\n",
                          a3, (unsigned long)*dest_lenp);
    qemu_plugin_outs(out);
    return true;
}

static bool vcpu_syscall_filter(qemu_plugin_id_t id, unsigned int vcpu_index,
                                int64_t num, uint64_t a1, uint64_t a2,
                                uint64_t a3, uint64_t a4, uint64_t a5,
                                uint64_t a6, uint64_t a7, uint64_t a8,
                                uint64_t *sysret)
{
    if (handle_library_open(num, a1, a2, a3, a4, sysret)) {
        return true;
    }

    if (handle_compress_bound(num, a1, a2, sysret)) {
        return true;
    }

    if (handle_compress2(num, a1, a2, a3, a4, a5, a6, sysret)) {
        return true;
    }

    if (handle_uncompress(num, a1, a2, a3, a4, a5, sysret)) {
        return true;
    }

    return false;
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    if (argc != 0) {
        fprintf(stderr,
                "syscall_filter_zlib: this example plugin does not take arguments\n");
        return -1;
    }

    if (strcmp(info->target_name, "x86_64") != 0) {
        fprintf(stderr,
                "syscall_filter_zlib: unsupported linux-user target '%s' "
                "(supported: x86_64)\n",
                info->target_name);
        return -1;
    }

    qemu_plugin_register_vcpu_syscall_filter_cb(id, vcpu_syscall_filter);
    return 0;
}
