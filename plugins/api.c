/*
 * QEMU Plugin API
 *
 * This provides the API that is available to the plugins to interact
 * with QEMU. We have to be careful not to expose internal details of
 * how QEMU works so we abstract out things like translation and
 * instructions to anonymous data types:
 *
 *  qemu_plugin_tb
 *  qemu_plugin_insn
 *
 * Which can then be passed back into the API to do additional things.
 * As such all the public functions in here are exported in
 * qemu-plugin.h.
 *
 * The general life-cycle of a plugin is:
 *
 *  - plugin is loaded, public qemu_plugin_install called
 *    - the install func registers callbacks for events
 *    - usually an atexit_cb is registered to dump info at the end
 *  - when a registered event occurs the plugin is called
 *     - some events pass additional info
 *     - during translation the plugin can decide to instrument any
 *       instruction
 *  - when QEMU exits all the registered atexit callbacks are called
 *
 * Copyright (C) 2017, Emilio G. Cota <cota@braap.org>
 * Copyright (C) 2019, Linaro
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "qemu/osdep.h"
#include "qemu/plugin.h"
#include "qemu/log.h"
#include "tcg/tcg.h"
#include "exec/exec-all.h"
#include "exec/gdbstub.h"
#include "exec/log.h"
#include "exec/ram_addr.h"
#include "disas/disas.h"
#include "plugin.h"
#include "sysemu/hw_accel.h"
#ifndef CONFIG_USER_ONLY
#include "qemu/plugin-memory.h"
#include "hw/boards.h"
#else
#include "qemu.h"
#ifdef CONFIG_LINUX
#include "loader.h"
#endif
#endif

/* Uninstall and Reset handlers */

void qemu_plugin_uninstall(qemu_plugin_id_t id, qemu_plugin_simple_cb_t cb)
{
    plugin_reset_uninstall(id, cb, false);
}

void qemu_plugin_reset(qemu_plugin_id_t id, qemu_plugin_simple_cb_t cb)
{
    plugin_reset_uninstall(id, cb, true);
}

/*
 * Plugin Register Functions
 *
 * This allows the plugin to register callbacks for various events
 * during the translation.
 */

void qemu_plugin_register_vcpu_init_cb(qemu_plugin_id_t id,
                                       qemu_plugin_vcpu_simple_cb_t cb)
{
    plugin_register_cb(id, QEMU_PLUGIN_EV_VCPU_INIT, cb);
}

void qemu_plugin_register_vcpu_exit_cb(qemu_plugin_id_t id,
                                       qemu_plugin_vcpu_simple_cb_t cb)
{
    plugin_register_cb(id, QEMU_PLUGIN_EV_VCPU_EXIT, cb);
}

void qemu_plugin_register_vcpu_tb_exec_cb(struct qemu_plugin_tb *tb,
                                          qemu_plugin_vcpu_udata_cb_t cb,
                                          enum qemu_plugin_cb_flags flags,
                                          void *udata)
{
    if (!tb->mem_only) {
        plugin_register_dyn_cb__udata(&tb->cbs[PLUGIN_CB_REGULAR],
                                      cb, flags, udata);
    }
}

void qemu_plugin_register_vcpu_tb_exec_inline(struct qemu_plugin_tb *tb,
                                              enum qemu_plugin_op op,
                                              void *ptr, uint64_t imm)
{
    if (!tb->mem_only) {
        plugin_register_inline_op(&tb->cbs[PLUGIN_CB_INLINE], 0, op, ptr, imm);
    }
}

void qemu_plugin_register_vcpu_insn_exec_cb(struct qemu_plugin_insn *insn,
                                            qemu_plugin_vcpu_udata_cb_t cb,
                                            enum qemu_plugin_cb_flags flags,
                                            void *udata)
{
    if (!insn->mem_only) {
        plugin_register_dyn_cb__udata(&insn->cbs[PLUGIN_CB_INSN][PLUGIN_CB_REGULAR],
                                      cb, flags, udata);
    }
}

void qemu_plugin_register_vcpu_insn_exec_inline(struct qemu_plugin_insn *insn,
                                                enum qemu_plugin_op op,
                                                void *ptr, uint64_t imm)
{
    if (!insn->mem_only) {
        plugin_register_inline_op(&insn->cbs[PLUGIN_CB_INSN][PLUGIN_CB_INLINE],
                                  0, op, ptr, imm);
    }
}


/*
 * We always plant memory instrumentation because they don't finalise until
 * after the operation has complete.
 */
void qemu_plugin_register_vcpu_mem_cb(struct qemu_plugin_insn *insn,
                                      qemu_plugin_vcpu_mem_cb_t cb,
                                      enum qemu_plugin_cb_flags flags,
                                      enum qemu_plugin_mem_rw rw,
                                      void *udata)
{
    plugin_register_vcpu_mem_cb(&insn->cbs[PLUGIN_CB_MEM][PLUGIN_CB_REGULAR],
                                    cb, flags, rw, udata);
}

void qemu_plugin_register_vcpu_mem_inline(struct qemu_plugin_insn *insn,
                                          enum qemu_plugin_mem_rw rw,
                                          enum qemu_plugin_op op, void *ptr,
                                          uint64_t imm)
{
    plugin_register_inline_op(&insn->cbs[PLUGIN_CB_MEM][PLUGIN_CB_INLINE],
                              rw, op, ptr, imm);
}

void qemu_plugin_register_vcpu_tb_trans_cb(qemu_plugin_id_t id,
                                           qemu_plugin_vcpu_tb_trans_cb_t cb)
{
    plugin_register_cb(id, QEMU_PLUGIN_EV_VCPU_TB_TRANS, cb);
}

void qemu_plugin_register_vcpu_syscall_cb(qemu_plugin_id_t id,
                                          qemu_plugin_vcpu_syscall_cb_t cb)
{
    plugin_register_cb(id, QEMU_PLUGIN_EV_VCPU_SYSCALL, cb);
}

void
qemu_plugin_register_vcpu_syscall_ret_cb(qemu_plugin_id_t id,
                                         qemu_plugin_vcpu_syscall_ret_cb_t cb)
{
    plugin_register_cb(id, QEMU_PLUGIN_EV_VCPU_SYSCALL_RET, cb);
}

/*
 * Plugin Queries
 *
 * These are queries that the plugin can make to gauge information
 * from our opaque data types. We do not want to leak internal details
 * here just information useful to the plugin.
 */

/*
 * Translation block information:
 *
 * A plugin can query the virtual address of the start of the block
 * and the number of instructions in it. It can also get access to
 * each translated instruction.
 */

size_t qemu_plugin_tb_n_insns(const struct qemu_plugin_tb *tb)
{
    return tb->n;
}

uint64_t qemu_plugin_tb_vaddr(const struct qemu_plugin_tb *tb)
{
    return tb->vaddr;
}

struct qemu_plugin_insn *
qemu_plugin_tb_get_insn(const struct qemu_plugin_tb *tb, size_t idx)
{
    struct qemu_plugin_insn *insn;
    if (unlikely(idx >= tb->n)) {
        return NULL;
    }
    insn = g_ptr_array_index(tb->insns, idx);
    insn->mem_only = tb->mem_only;
    return insn;
}

/*
 * Instruction information
 *
 * These queries allow the plugin to retrieve information about each
 * instruction being translated.
 */

const void *qemu_plugin_insn_data(const struct qemu_plugin_insn *insn)
{
    return insn->data->data;
}

size_t qemu_plugin_insn_size(const struct qemu_plugin_insn *insn)
{
    return insn->data->len;
}

uint64_t qemu_plugin_insn_vaddr(const struct qemu_plugin_insn *insn)
{
    return insn->vaddr;
}

void *qemu_plugin_insn_haddr(const struct qemu_plugin_insn *insn)
{
    return insn->haddr;
}

char *qemu_plugin_insn_disas(const struct qemu_plugin_insn *insn)
{
    CPUState *cpu = current_cpu;
    return plugin_disas(cpu, insn->vaddr, insn->data->len);
}

const char *qemu_plugin_insn_symbol(const struct qemu_plugin_insn *insn)
{
    const char *sym = lookup_symbol(insn->vaddr);
    return sym[0] != 0 ? sym : NULL;
}

/*
 * CPU registers
 *
 * These queries allow the plugin to retrieve information about current
 * CPU registers
 */

static void check_reg_architecture_support(void) {
    if (strcmp(TARGET_NAME, "x86_64") != 0 && strcmp(TARGET_NAME, "riscv64") != 0 &&
        strcmp(TARGET_NAME, "aarch64") != 0) {
        error_report("Unsupported architecture: %s", TARGET_NAME);
        abort();
    }
}

bool qemu_plugin_find_reg(const char *name, int *regnum)
{
    CPUState *cpu = current_cpu;
    if (name == NULL || cpu == NULL)
        return false;

    check_reg_architecture_support();

    int num = 0, bitsize = 0;
    bool found = gdb_find_register_num_and_bitsize(cpu, name,
                                                   &num, &bitsize);
    if (regnum)
        *regnum = num;
    return found;
}

size_t qemu_plugin_get_available_reg_names(char *buf, size_t buf_size)
{
    check_reg_architecture_support();
    return gdb_get_available_reg_names(current_cpu, buf, buf_size);
}

const void *qemu_plugin_read_reg(int regnum, size_t *size)
{
    CPUState *cpu = current_cpu;
    if (cpu == NULL)
        return NULL;

    check_reg_architecture_support();

    cpu_synchronize_state(cpu);
    GByteArray *arr = g_byte_array_new();
    gdb_read_register(cpu, arr, regnum);
    if (size)
        *size = arr->len;
    return g_byte_array_free(arr, false);
}

struct qemu_plugin_reg_ctx {
    CPUState *cpu;

    size_t *regnums;
    size_t *bitsizes;
    gchar **names;

    /* cache the initial position of the register data
    in the general data array */
    size_t *offsets;

    /* the actual number of registers in the context.
       This number may be less than requested if any of the registers
       was not found */
    size_t n_regs;

    /* contains registers one by one */
    GByteArray *data;

    /* remember how much memory was actually allocated for the data.
       This value is used to check that the length of the array has not changed
       after reading the registers. it mustn't happen */
    size_t alloc_data_len;
};

size_t qemu_plugin_n_regs(const struct qemu_plugin_reg_ctx *ctx)
{
    return (ctx) ? ctx->n_regs : 0;
}

struct qemu_plugin_reg_ctx *
qemu_plugin_reg_create_context(const char *const *names, size_t len)
{
    size_t reqested_len, actual_len, total_bitsize, i;
    struct qemu_plugin_reg_ctx *ctx;
    CPUState *cpu = current_cpu;
    if (cpu == NULL)
        return NULL;

    check_reg_architecture_support();

    reqested_len = len;
    ctx = g_new0(struct qemu_plugin_reg_ctx, 1);
    ctx->cpu = cpu;
    ctx->regnums = g_new0(size_t, reqested_len);
    ctx->bitsizes = g_new0(size_t, reqested_len);
    ctx->names = g_new0(gchar*, reqested_len);
    ctx->offsets = g_new0(size_t, reqested_len);

    actual_len = 0;
    total_bitsize = 0;
    for (i = 0; i < reqested_len; i++) {
        int reg = 0, bitsize = 0;
        bool found = gdb_find_register_num_and_bitsize(ctx->cpu, names[i],
                                                       &reg, &bitsize);
        if (!found)
            continue;

        ctx->regnums[actual_len] = reg;
        ctx->bitsizes[actual_len] = bitsize;
        ctx->names[actual_len] = g_strdup(names[i]);
        ctx->offsets[actual_len] = total_bitsize;
        actual_len++;
        total_bitsize += bitsize;
    }
    ctx->n_regs = actual_len;

    if (actual_len == 0) {
        qemu_plugin_reg_free_context(ctx);
        return NULL;
    }

    if ((total_bitsize % CHAR_BIT) != 0) {
        error_report("Unexpected register bitsize: %ld", total_bitsize);
        abort();
    }
    ctx->alloc_data_len = total_bitsize / 8;
    ctx->data = g_byte_array_sized_new(ctx->alloc_data_len);

    return ctx;
}

void qemu_plugin_reg_free_context(struct qemu_plugin_reg_ctx *ctx)
{
    int i;
    if (ctx == NULL)
        return;

    if (ctx->data)
        g_byte_array_free(ctx->data, true);

    g_free(ctx->offsets);
    for (i = 0; i < ctx->n_regs; i++) {
        g_free(ctx->names[i]);
    }
    g_free(ctx->names);
    g_free(ctx->bitsizes);
    g_free(ctx->regnums);
    g_free(ctx);
    ctx = NULL;
}

static inline bool reg_context_is_valid(const struct qemu_plugin_reg_ctx *ctx)
{
    return ctx && ctx->data;
}

static inline bool reg_index_is_valid(const struct qemu_plugin_reg_ctx *ctx,
                                      size_t idx)
{
    return idx < ctx->n_regs && idx <= INT_MAX;
}

const char *qemu_plugin_reg_name(const struct qemu_plugin_reg_ctx *ctx,
                                 size_t idx)
{
    if (!reg_context_is_valid(ctx) || !reg_index_is_valid(ctx, idx))
        return NULL;

    return ctx->names[idx];
}

const void *qemu_plugin_reg_ptr(const struct qemu_plugin_reg_ctx *ctx,
                                size_t idx)
{
    if (!reg_context_is_valid(ctx) || !reg_index_is_valid(ctx, idx))
        return NULL;

    size_t offset = ctx->offsets[idx] / CHAR_BIT;
    return (uint8_t *)ctx->data->data + offset;
}

size_t qemu_plugin_reg_size(const struct qemu_plugin_reg_ctx *ctx,
                            size_t idx)
{
    if (!reg_context_is_valid(ctx) || !reg_index_is_valid(ctx, idx))
        return 0;

    if ((ctx->bitsizes[idx] % CHAR_BIT) != 0) {
        error_report("Unexpected register bitsize: %ld", ctx->bitsizes[idx]);
        abort();
    }

    return ctx->bitsizes[idx] / CHAR_BIT;
}

void qemu_plugin_regs_load(struct qemu_plugin_reg_ctx *ctx)
{
    g_byte_array_set_size(ctx->data, 0);
    cpu_synchronize_state(ctx->cpu);
    size_t i;
    for (i = 0; i < ctx->n_regs; i++) {
        int size = gdb_read_register(ctx->cpu, ctx->data, ctx->regnums[i]);
        int bitsize = size * 8;
        if (bitsize != ctx->bitsizes[i]) {
            error_report("Expected data size after reading register %s: %ld, got %u",
                     ctx->names[i], ctx->bitsizes[i], bitsize);
            abort();
        }
    }
    if (ctx->data->len != ctx->alloc_data_len) {
        error_report("Expected data size after reading registers: %ld, got %u",
                     ctx->alloc_data_len, ctx->data->len);
        abort();
    }
}

/*
 * The memory queries allow the plugin to query information about a
 * memory access.
 */

unsigned qemu_plugin_mem_size_shift(qemu_plugin_meminfo_t info)
{
    MemOp op = get_memop(info);
    return op & MO_SIZE;
}

bool qemu_plugin_mem_is_sign_extended(qemu_plugin_meminfo_t info)
{
    MemOp op = get_memop(info);
    return op & MO_SIGN;
}

bool qemu_plugin_mem_is_big_endian(qemu_plugin_meminfo_t info)
{
    MemOp op = get_memop(info);
    return (op & MO_BSWAP) == MO_BE;
}

bool qemu_plugin_mem_is_store(qemu_plugin_meminfo_t info)
{
    return get_plugin_meminfo_rw(info) & QEMU_PLUGIN_MEM_W;
}

/*
 * Virtual Memory queries
 */

#ifdef CONFIG_SOFTMMU
static __thread struct qemu_plugin_hwaddr hwaddr_info;
#endif

struct qemu_plugin_hwaddr *qemu_plugin_get_hwaddr(qemu_plugin_meminfo_t info,
                                                  uint64_t vaddr)
{
#ifdef CONFIG_SOFTMMU
    CPUState *cpu = current_cpu;
    unsigned int mmu_idx = get_mmuidx(info);
    enum qemu_plugin_mem_rw rw = get_plugin_meminfo_rw(info);
    hwaddr_info.is_store = (rw & QEMU_PLUGIN_MEM_W) != 0;

    assert(mmu_idx < NB_MMU_MODES);

    if (!tlb_plugin_lookup(cpu, vaddr, mmu_idx,
                           hwaddr_info.is_store, &hwaddr_info)) {
        error_report("invalid use of qemu_plugin_get_hwaddr");
        return NULL;
    }

    return &hwaddr_info;
#else
    return NULL;
#endif
}

bool qemu_plugin_hwaddr_is_io(const struct qemu_plugin_hwaddr *haddr)
{
#ifdef CONFIG_SOFTMMU
    return haddr->is_io;
#else
    return false;
#endif
}

uint64_t qemu_plugin_hwaddr_phys_addr(const struct qemu_plugin_hwaddr *haddr)
{
#ifdef CONFIG_SOFTMMU
    if (haddr) {
        if (!haddr->is_io) {
            RAMBlock *block;
            ram_addr_t offset;
            void *hostaddr = haddr->v.ram.hostaddr;

            block = qemu_ram_block_from_host(hostaddr, false, &offset);
            if (!block) {
                error_report("Bad host ram pointer %p", haddr->v.ram.hostaddr);
                abort();
            }

            return block->offset + offset + block->mr->addr;
        } else {
            MemoryRegionSection *mrs = haddr->v.io.section;
            return mrs->offset_within_address_space + haddr->v.io.offset;
        }
    }
#endif
    return 0;
}

const char *qemu_plugin_hwaddr_device_name(const struct qemu_plugin_hwaddr *h)
{
#ifdef CONFIG_SOFTMMU
    if (h && h->is_io) {
        MemoryRegionSection *mrs = h->v.io.section;
        if (!mrs->mr->name) {
            unsigned long maddr = 0xffffffff & (uintptr_t) mrs->mr;
            g_autofree char *temp = g_strdup_printf("anon%08lx", maddr);
            return g_intern_string(temp);
        } else {
            return g_intern_string(mrs->mr->name);
        }
    } else {
        return g_intern_static_string("RAM");
    }
#else
    return g_intern_static_string("Invalid");
#endif
}

/*
 * Queries to the number and potential maximum number of vCPUs there
 * will be. This helps the plugin dimension per-vcpu arrays.
 */

#ifndef CONFIG_USER_ONLY
static MachineState * get_ms(void)
{
    return MACHINE(qdev_get_machine());
}
#endif

int qemu_plugin_n_vcpus(void)
{
#ifdef CONFIG_USER_ONLY
    return -1;
#else
    return get_ms()->smp.cpus;
#endif
}

int qemu_plugin_n_max_vcpus(void)
{
#ifdef CONFIG_USER_ONLY
    return -1;
#else
    return get_ms()->smp.max_cpus;
#endif
}

/*
 * Plugin output
 */
void qemu_plugin_outs(const char *string)
{
    qemu_log_mask(CPU_LOG_PLUGIN, "%s", string);
}

bool qemu_plugin_bool_parse(const char *name, const char *value, bool *ret)
{
    return name && value && qapi_bool_parse(name, value, ret, NULL);
}

/*
 * Binary path, start and end locations
 */
const char *qemu_plugin_path_to_binary(void)
{
    char *path = NULL;
#ifdef CONFIG_USER_ONLY
    TaskState *ts = (TaskState *) current_cpu->opaque;
    path = g_strdup(ts->bprm->filename);
#endif
    return path;
}

uint64_t qemu_plugin_start_code(void)
{
    uint64_t start = 0;
#ifdef CONFIG_USER_ONLY
    TaskState *ts = (TaskState *) current_cpu->opaque;
    start = ts->info->start_code;
#endif
    return start;
}

uint64_t qemu_plugin_end_code(void)
{
    uint64_t end = 0;
#ifdef CONFIG_USER_ONLY
    TaskState *ts = (TaskState *) current_cpu->opaque;
    end = ts->info->end_code;
#endif
    return end;
}

uint64_t qemu_plugin_entry_code(void)
{
    uint64_t entry = 0;
#ifdef CONFIG_USER_ONLY
    TaskState *ts = (TaskState *) current_cpu->opaque;
    entry = ts->info->entry;
#endif
    return entry;
}
