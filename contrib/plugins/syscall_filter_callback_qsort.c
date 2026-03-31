/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * x86_64-only prototype that demonstrates a callback-capable syscall filter
 * plugin by redirecting a qsort() thunk library and bridging comparator calls
 * back into guest translated code with ucontext. The loader redirection
 * handles open(), openat(), and openat2().
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
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

#define MAGIC_SYSCALL 4096
#define CALLBACK_QSORT_OP_START 1
#define CALLBACK_QSORT_OP_RESUME 2
#define CALLBACK_QSORT_LIBRARY "libdemo-callback-qsort.so"
#define CALLBACK_QSORT_THUNK_LIBRARY "libdemo-callback-qsort-thunk.so"
#define GUEST_STRING_CHUNK 64
#define GUEST_STRING_LIMIT (1 << 20)
#define CALLBACK_QSORT_MAX_ELEMS (1 << 20)
#define CALLBACK_QSORT_STACK_SIZE (1 << 20)
#define X86_64_OPEN_NR 2
#define X86_64_OPENAT_NR 257
#define X86_64_OPENAT2_NR 437

typedef struct GuestOpenHow {
    uint64_t flags;
    uint64_t mode;
} GuestOpenHow;

typedef enum CallbackQsortPhase {
    CALLBACK_QSORT_PHASE_IDLE,
    CALLBACK_QSORT_PHASE_NEED_CALLBACK,
    CALLBACK_QSORT_PHASE_FINISHED,
} CallbackQsortPhase;

typedef struct CallbackQsortState {
    uint64_t sort_base;
    uint64_t guest_cmp_fn;
    uint64_t guest_trampoline;
    size_t nmemb;
    size_t elem_size;
    void *worker_stack;
    uint64_t pending_guest_a;
    uint64_t pending_guest_b;
    int cmp_result;
    unsigned int guest_callback_count;
    CallbackQsortPhase phase;
    ucontext_t plugin_ctx;
    ucontext_t worker_ctx;
} CallbackQsortState;

typedef struct X86Registers {
    struct qemu_plugin_register *rsp;
    struct qemu_plugin_register *rdi;
    struct qemu_plugin_register *rsi;
} X86Registers;

typedef struct VcpuState {
    X86Registers regs;
    GByteArray *buf;
    CallbackQsortState *active_call;
} VcpuState;

static GPtrArray *vcpu_states;
static __thread CallbackQsortState *tls_active_call;

static VcpuState *get_vcpu_state(unsigned int vcpu_index)
{
    while (vcpu_states->len <= vcpu_index) {
        g_ptr_array_add(vcpu_states, NULL);
    }

    if (g_ptr_array_index(vcpu_states, vcpu_index) == NULL) {
        VcpuState *state = g_new0(VcpuState, 1);
        state->buf = g_byte_array_sized_new(16);
        g_ptr_array_index(vcpu_states, vcpu_index) = state;
    }

    return g_ptr_array_index(vcpu_states, vcpu_index);
}

static struct qemu_plugin_register *find_register(const char *name)
{
    g_autoptr(GArray) regs = qemu_plugin_get_registers();

    for (guint i = 0; i < regs->len; i++) {
        qemu_plugin_reg_descriptor *reg =
            &g_array_index(regs, qemu_plugin_reg_descriptor, i);

        if (strcmp(reg->name, name) == 0) {
            return reg->handle;
        }
    }

    return NULL;
}

static uint64_t read_reg64(VcpuState *vcpu, struct qemu_plugin_register *reg)
{
    bool success;

    g_byte_array_set_size(vcpu->buf, 0);
    success = qemu_plugin_read_register(reg, vcpu->buf);
    g_assert(success);
    g_assert(vcpu->buf->len == 8);
    return *(uint64_t *)vcpu->buf->data;
}

static void write_reg64(VcpuState *vcpu, struct qemu_plugin_register *reg,
                        uint64_t value)
{
    bool success;

    g_byte_array_set_size(vcpu->buf, 8);
    memcpy(vcpu->buf->data, &value, sizeof(value));
    success = qemu_plugin_write_register(reg, vcpu->buf);
    g_assert(success);
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

static bool write_guest_u64(uint64_t addr, uint64_t value)
{
    GByteArray data = {
        .data = (guint8 *)&value,
        .len = sizeof(value),
    };

    return qemu_plugin_write_memory_vaddr(addr, &data);
}

static void read_guest_open_how(uint64_t addr, uint64_t guest_size,
                                GuestOpenHow *how)
{
    g_assert(guest_size >= sizeof(*how));
    read_guest_buffer(addr, how, sizeof(*how));
}

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

static bool guest_path_matches_bridge(const char *path)
{
    g_autofree char *basename = g_path_get_basename(path);

    return strcmp(basename, CALLBACK_QSORT_LIBRARY) == 0;
}

static char *build_thunk_path(const char *path)
{
    g_autofree char *dirname = g_path_get_dirname(path);

    if (strcmp(dirname, ".") == 0) {
        return g_strdup(CALLBACK_QSORT_THUNK_LIBRARY);
    }

    return g_build_filename(dirname, CALLBACK_QSORT_THUNK_LIBRARY, NULL);
}

static int host_compare(const void *lhs, const void *rhs)
{
    CallbackQsortState *state = tls_active_call;
    state->pending_guest_a = (uint64_t)(uintptr_t)lhs;
    state->pending_guest_b = (uint64_t)(uintptr_t)rhs;

    state->phase = CALLBACK_QSORT_PHASE_NEED_CALLBACK;
    swapcontext(&state->worker_ctx, &state->plugin_ctx);
    return state->cmp_result;
}

static void callback_qsort_worker(void)
{
    CallbackQsortState *state = tls_active_call;

    qsort((void *)(uintptr_t)state->sort_base, state->nmemb, state->elem_size,
          host_compare);
    state->phase = CALLBACK_QSORT_PHASE_FINISHED;
    swapcontext(&state->worker_ctx, &state->plugin_ctx);
    g_assert_not_reached();
}

static void free_callback_state(CallbackQsortState *state)
{
    if (state == NULL) {
        return;
    }

    g_free(state->worker_stack);
    g_free(state);
}

static bool finalize_if_finished(unsigned int vcpu_index, VcpuState *vcpu,
                                 uint64_t *sysret)
{
    CallbackQsortState *state = vcpu->active_call;
    g_autofree char *out = NULL;

    if (state->phase != CALLBACK_QSORT_PHASE_FINISHED) {
        return false;
    }

    *sysret = 0;

    out = g_strdup_printf(
        "syscall_filter_callback_qsort: vcpu %u completed qsort "
        "with %u guest callbacks\n",
                          vcpu_index, state->guest_callback_count);
    qemu_plugin_outs(out);

    free_callback_state(state);
    vcpu->active_call = NULL;
    tls_active_call = NULL;
    return true;
}

static void jump_to_guest_callback(VcpuState *vcpu, CallbackQsortState *state)
{
    uint64_t current_rsp = read_reg64(vcpu, vcpu->regs.rsp);
    uint64_t callback_rsp = current_rsp - sizeof(uint64_t);
    uint64_t guest_return;

    g_assert((current_rsp & 0xf) == 0 || (current_rsp & 0xf) == 8);

    if ((callback_rsp & 0xf) != 8) {
        /*
         * Rebuild the stack so the guest comparator sees a normal SysV
         * function entry: %rsp points at its return address and
         * %rsp % 16 == 8.
         */
        callback_rsp -= sizeof(uint64_t);
        read_guest_buffer(current_rsp, &guest_return, sizeof(guest_return));
        g_assert(write_guest_u64(current_rsp - sizeof(uint64_t),
                                 guest_return));
    }

    g_assert(write_guest_u64(callback_rsp, state->guest_trampoline));

    write_reg64(vcpu, vcpu->regs.rsp, callback_rsp);
    write_reg64(vcpu, vcpu->regs.rdi, state->pending_guest_a);
    write_reg64(vcpu, vcpu->regs.rsi, state->pending_guest_b);
    state->guest_callback_count++;
    qemu_plugin_set_pc(state->guest_cmp_fn);
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
    if (path == NULL || !guest_path_matches_bridge(path)) {
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
    out = g_strdup_printf(
        "syscall_filter_callback_qsort: redirected %s -> %s (fd=%d)\n",
                          path, thunk_path, fd);
    qemu_plugin_outs(out);
    return true;
}

static bool handle_callback_start(unsigned int vcpu_index, VcpuState *vcpu,
                                  uint64_t sort_base, uint64_t nmemb,
                                  uint64_t elem_size, uint64_t cmp_fn,
                                  uint64_t trampoline, uint64_t *sysret)
{
    CallbackQsortState *state;

    g_assert(vcpu->active_call == NULL);
    g_assert(elem_size > 0);
    g_assert(nmemb > 0);
    g_assert(nmemb <= CALLBACK_QSORT_MAX_ELEMS);
    g_assert(cmp_fn != 0 && trampoline != 0);

    state = g_new0(CallbackQsortState, 1);
    state->sort_base = sort_base;
    state->guest_cmp_fn = cmp_fn;
    state->guest_trampoline = trampoline;
    state->nmemb = nmemb;
    state->elem_size = elem_size;
    state->worker_stack = g_malloc(CALLBACK_QSORT_STACK_SIZE);
    state->phase = CALLBACK_QSORT_PHASE_IDLE;

    getcontext(&state->worker_ctx);
    state->worker_ctx.uc_link = NULL;
    state->worker_ctx.uc_stack.ss_sp = state->worker_stack;
    state->worker_ctx.uc_stack.ss_size = CALLBACK_QSORT_STACK_SIZE;
    makecontext(&state->worker_ctx, callback_qsort_worker, 0);

    vcpu->active_call = state;
    tls_active_call = state;
    swapcontext(&state->plugin_ctx, &state->worker_ctx);

    if (finalize_if_finished(vcpu_index, vcpu, sysret)) {
        return true;
    }

    if (state->phase == CALLBACK_QSORT_PHASE_NEED_CALLBACK) {
        jump_to_guest_callback(vcpu, state);
        return true;
    }

    g_assert_not_reached();
}

static bool handle_callback_resume(unsigned int vcpu_index, VcpuState *vcpu,
                                   int64_t cmp_result, uint64_t *sysret)
{
    CallbackQsortState *state = vcpu->active_call;

    g_assert(state != NULL);

    state->cmp_result = (int)cmp_result;
    tls_active_call = state;
    swapcontext(&state->plugin_ctx, &state->worker_ctx);

    if (finalize_if_finished(vcpu_index, vcpu, sysret)) {
        return true;
    }

    if (state->phase == CALLBACK_QSORT_PHASE_NEED_CALLBACK) {
        jump_to_guest_callback(vcpu, state);
        return true;
    }

    g_assert_not_reached();
}

static bool vcpu_syscall_filter(qemu_plugin_id_t id, unsigned int vcpu_index,
                                int64_t num, uint64_t a1, uint64_t a2,
                                uint64_t a3, uint64_t a4, uint64_t a5,
                                uint64_t a6, uint64_t a7, uint64_t a8,
                                uint64_t *sysret)
{
    VcpuState *vcpu = get_vcpu_state(vcpu_index);

    if (handle_library_open(num, a1, a2, a3, a4, sysret)) {
        return true;
    }

    if (num != MAGIC_SYSCALL) {
        return false;
    }

    switch (a1) {
    case CALLBACK_QSORT_OP_START:
        return handle_callback_start(vcpu_index, vcpu, a2, a3, a4, a5, a6,
                                     sysret);
    case CALLBACK_QSORT_OP_RESUME:
        return handle_callback_resume(vcpu_index, vcpu, (int64_t)a2, sysret);
    default:
        return false;
    }
}

static void vcpu_init_cb(qemu_plugin_id_t id, unsigned int vcpu_index)
{
    VcpuState *vcpu = get_vcpu_state(vcpu_index);

    vcpu->regs.rsp = find_register("rsp");
    vcpu->regs.rdi = find_register("rdi");
    vcpu->regs.rsi = find_register("rsi");
    g_assert(vcpu->regs.rsp != NULL);
    g_assert(vcpu->regs.rdi != NULL);
    g_assert(vcpu->regs.rsi != NULL);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    if (argc != 0) {
        fprintf(stderr,
                "syscall_filter_callback_qsort: this prototype plugin does not take arguments\n");
        return -1;
    }

    if (strcmp(info->target_name, "x86_64") != 0) {
        fprintf(stderr,
                "syscall_filter_callback_qsort: unsupported linux-user target '%s' "
                "(this prototype currently supports only x86_64)\n",
                info->target_name);
        return -1;
    }

    vcpu_states = g_ptr_array_new();
    qemu_plugin_register_vcpu_init_cb(id, vcpu_init_cb);
    qemu_plugin_register_vcpu_syscall_filter_cb(id, vcpu_syscall_filter);
    return 0;
}
