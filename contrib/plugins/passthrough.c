/*
 * Copyright (C) 2026, Ziyang Zhang <functioner@sjtu.edu.cn>
 *
 * Passthrough Plugin: lets a guest invoke host functions via a magic
 * system call. This grants the guest full host access (dlopen/dlsym and
 * arbitrary function calls), so use it only with trusted guests.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <glib.h>
#include <dlfcn.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

/* The magic system call number for pass-through. */
enum {
    SYSCALL_PASSTHROUGH_NUMBER = 4096,
};

/*
 * Pass-through calling convention.
 *
 * The guest issues system call SYSCALL_PASSTHROUGH_NUMBER. The first syscall
 * argument (a1) is one of the call IDs below; the remaining arguments (a2, a3,
 * a4, ...) are that ID's operands. All pointer operands are guest virtual
 * addresses that the plugin dereferences as host addresses directly. This
 * assumes guest_base is 0, so the guest and host address spaces coincide;
 * with a non-zero guest_base every pointer operand would be off by guest_base
 * and the dereferences would hit unrelated host memory. Results are written
 * back through caller-provided "out" pointers rather than returned in the
 * syscall value.
 *
 * The syscall return value (*sysret) only reports dispatch status: 0 on a
 * recognised ID, -EINVAL for an unknown one. The actual success/failure of an
 * operation (e.g. a NULL handle from dlopen) is delivered through its out
 * pointer, exactly like the underlying libdl call.
 *
 * Operands per ID:
 *
 *   PASSTHROUGH_ID_GET_HOST_ATTRIBUTE
 *     a2  const char *key        in:  attribute name to query
 *     a3  const char **attr_ptr  out: matching value, or NULL if unknown
 *
 *   PASSTHROUGH_ID_LOAD_LIBRARY                            (wraps dlopen)
 *     a2  const char *path       in:  library path
 *     a3  int flags              in:  dlopen() flags (e.g. RTLD_NOW)
 *     a4  void **handle_ptr      out: library handle, or NULL on failure
 *
 *   PASSTHROUGH_ID_GET_PROC_ADDRESS                        (wraps dlsym)
 *     a2  void *handle           in:  library handle
 *     a3  const char *name       in:  symbol name
 *     a4  void **entry_ptr       out: symbol address, or NULL if not found
 *
 *   PASSTHROUGH_ID_FREE_LIBRARY                            (wraps dlclose)
 *     a2  void *handle           in:  library handle
 *     a3  int *ret_ptr           out: dlclose() return value (0 on success)
 *
 *   PASSTHROUGH_ID_GET_LIBRARY_ERROR                       (wraps dlerror)
 *     a2  const char **error_ptr out: last libdl error string, or NULL
 *
 *   PASSTHROUGH_ID_INVOKE_PROC                             (calls the symbol)
 *     a2  void *proc             in:  function pointer, signature
 *                                     void (*)(void *arg1, void *arg2)
 *     a3  void *arg1             in:  first argument forwarded to proc
 *     a4  void *arg2             in:  second argument forwarded to proc
 */
enum PassThroughID {
    PASSTHROUGH_ID_GET_HOST_ATTRIBUTE,
    PASSTHROUGH_ID_LOAD_LIBRARY,
    PASSTHROUGH_ID_GET_PROC_ADDRESS,
    PASSTHROUGH_ID_FREE_LIBRARY,
    PASSTHROUGH_ID_GET_LIBRARY_ERROR,
    PASSTHROUGH_ID_INVOKE_PROC,
};

static inline const char *query_host_attribute(const char *key)
{
    if (strcmp(key, "emu") == 0) {
        return "qemu";
    }
    return NULL;
}

static inline void invoke_proc(void *proc, void *arg1, void *arg2)
{
    typedef void (*Func)(void * /*arg1*/, void * /*arg2*/);
    Func func = (Func) proc;
    func(arg1, arg2);
}

static bool vcpu_syscall_filter(qemu_plugin_id_t id, unsigned int vcpu_index,
                                int64_t num, uint64_t a1, uint64_t a2,
                                uint64_t a3, uint64_t a4, uint64_t a5,
                                uint64_t a6, uint64_t a7, uint64_t a8,
                                uint64_t *sysret)
{
    if (num == SYSCALL_PASSTHROUGH_NUMBER) {
        switch (a1) {
        /* Query host attribute by a reserved key. */
        case PASSTHROUGH_ID_GET_HOST_ATTRIBUTE: {
            const char *key = (const char *) a2;
            const char **attr_ptr = (const char **) a3;
            assert(attr_ptr);
            *attr_ptr = query_host_attribute(key);
            *sysret = 0;
            break;
        }

        /* Load a shared library. */
        case PASSTHROUGH_ID_LOAD_LIBRARY: {
            const char *path = (const char *) a2;
            int flags = (int) a3;
            void **handle_ptr = (void **) a4;
            assert(handle_ptr);
            *handle_ptr = dlopen(path, flags);
            *sysret = 0;
            break;
        }

        /* Get the address of a function in a shared library. */
        case PASSTHROUGH_ID_GET_PROC_ADDRESS: {
            void *handle = (void *) a2;
            const char *name = (const char *) a3;
            void **entry_ptr = (void **) a4;
            assert(entry_ptr);
            *entry_ptr = dlsym(handle, name);
            *sysret = 0;
            break;
        }

        /* Free a shared library. */
        case PASSTHROUGH_ID_FREE_LIBRARY: {
            void *handle = (void *) a2;
            int *ret_ptr = (int *) a3;
            *ret_ptr = dlclose(handle);
            *sysret = 0;
            break;
        }

        /* Get the last error message for a library event. */
        case PASSTHROUGH_ID_GET_LIBRARY_ERROR: {
            const char **error_ptr = (const char **) a2;
            *error_ptr = dlerror();
            *sysret = 0;
            break;
        }

        /* Invoke a function of a common interface. */
        case PASSTHROUGH_ID_INVOKE_PROC: {
            void *proc = (void *) a2;
            void *arg1 = (void *) a3;
            void *arg2 = (void *) a4;
            assert(proc);
            invoke_proc(proc, arg1, arg2);
            *sysret = 0;
            break;
        }

        default:
            *sysret = -EINVAL;
            break;
        }
        return true;
    }
    return false;
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    if (info->system_emulation) {
        fprintf(stderr, "plugin passthrough: only useful for user emulation\n");
        return -1;
    }

    qemu_plugin_register_vcpu_syscall_filter_cb(id, vcpu_syscall_filter);

    return 0;
}
