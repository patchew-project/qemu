/*
 *  Copyright(c) 2019-2020 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <math.h>
#include "qemu/osdep.h"
#include "qemu.h"
#include "exec/helper-proto.h"
#include "cpu.h"
#include "internal.h"
#include "macros.h"
#include "arch.h"
#include "fma_emu.h"
#include "conv_emu.h"

/* Exceptions processing helpers */
static void QEMU_NORETURN do_raise_exception_err(CPUHexagonState *env,
                                                 uint32_t exception,
                                                 uintptr_t pc)
{
    CPUState *cs = CPU(hexagon_env_get_cpu(env));
    qemu_log_mask(CPU_LOG_INT, "%s: %d\n", __func__, exception);
    cs->exception_index = exception;
    cpu_loop_exit_restore(cs, pc);
}

void HELPER(raise_exception)(CPUHexagonState *env, uint32_t exception)
{
    do_raise_exception_err(env, exception, 0);
}

static inline void log_reg_write(CPUHexagonState *env, int rnum,
                                 target_ulong val, uint32_t slot)
{
    HEX_DEBUG_LOG("log_reg_write[%d] = " TARGET_FMT_ld " (0x" TARGET_FMT_lx ")",
                  rnum, val, val);
    if (env->slot_cancelled & (1 << slot)) {
        HEX_DEBUG_LOG(" CANCELLED");
    }
    if (val == env->gpr[rnum]) {
        HEX_DEBUG_LOG(" NO CHANGE");
    }
    HEX_DEBUG_LOG("\n");
    if (!(env->slot_cancelled & (1 << slot))) {
        env->new_value[rnum] = val;
#if HEX_DEBUG
        /* Do this so HELPER(debug_commit_end) will know */
        env->reg_written[rnum] = 1;
#endif
    }
}

static __attribute__((unused))
inline void log_reg_write_pair(CPUHexagonState *env, int rnum,
                                      int64_t val, uint32_t slot)
{
    HEX_DEBUG_LOG("log_reg_write_pair[%d:%d] = %ld\n", rnum + 1, rnum, val);
    log_reg_write(env, rnum, val & 0xFFFFFFFF, slot);
    log_reg_write(env, rnum + 1, (val >> 32) & 0xFFFFFFFF, slot);
}

static inline void log_pred_write(CPUHexagonState *env, int pnum,
                                  target_ulong val)
{
    HEX_DEBUG_LOG("log_pred_write[%d] = " TARGET_FMT_ld
                  " (0x" TARGET_FMT_lx ")\n",
                  pnum, val, val);

    /* Multiple writes to the same preg are and'ed together */
    if (env->pred_written & (1 << pnum)) {
        env->new_pred_value[pnum] &= val & 0xff;
    } else {
        env->new_pred_value[pnum] = val & 0xff;
        env->pred_written |= 1 << pnum;
    }
}

static inline void log_store32(CPUHexagonState *env, target_ulong addr,
                               int32_t val, int width, int slot)
{
    HEX_DEBUG_LOG("log_store%d(0x%x, %d [0x%x])\n", width, addr, val, val);
    env->mem_log_stores[slot].va = addr;
    env->mem_log_stores[slot].width = width;
    env->mem_log_stores[slot].data32 = val;
}

static inline void log_store64(CPUHexagonState *env, target_ulong addr,
                               int64_t val, int width, int slot)
{
    HEX_DEBUG_LOG("log_store%d(0x%x, %ld [0x%lx])\n", width, addr, val, val);
    env->mem_log_stores[slot].va = addr;
    env->mem_log_stores[slot].width = width;
    env->mem_log_stores[slot].data64 = val;
}

static inline void write_new_pc(CPUHexagonState *env, target_ulong addr)
{
    HEX_DEBUG_LOG("write_new_pc(0x" TARGET_FMT_lx ")\n", addr);

    /*
     * If more than one branch is taken in a packet, only the first one
     * is actually done.
     */
    if (env->branch_taken) {
        HEX_DEBUG_LOG("INFO: multiple branches taken in same packet, "
                      "ignoring the second one\n");
    } else {
        fCHECK_PCALIGN(addr);
        env->branch_taken = 1;
        env->next_PC = addr;
    }
}

/* Handy place to set a breakpoint */
void HELPER(debug_start_packet)(CPUHexagonState *env)
{
    HEX_DEBUG_LOG("Start packet: pc = 0x" TARGET_FMT_lx "\n",
                  env->gpr[HEX_REG_PC]);

    int i;
    for (i = 0; i < TOTAL_PER_THREAD_REGS; i++) {
        env->reg_written[i] = 0;
    }
}

static inline int32_t new_pred_value(CPUHexagonState *env, int pnum)
{
    return env->new_pred_value[pnum];
}

/* Checks for bookkeeping errors between disassembly context and runtime */
void HELPER(debug_check_store_width)(CPUHexagonState *env, int slot, int check)
{
    if (env->mem_log_stores[slot].width != check) {
        HEX_DEBUG_LOG("ERROR: %d != %d\n",
                      env->mem_log_stores[slot].width, check);
        g_assert_not_reached();
    }
}

static void print_store(CPUHexagonState *env, int slot)
{
    if (!(env->slot_cancelled & (1 << slot))) {
        size1u_t width = env->mem_log_stores[slot].width;
        if (width == 1) {
            size4u_t data = env->mem_log_stores[slot].data32 & 0xff;
            HEX_DEBUG_LOG("\tmemb[0x" TARGET_FMT_lx "] = %d (0x%02x)\n",
                          env->mem_log_stores[slot].va, data, data);
        } else if (width == 2) {
            size4u_t data = env->mem_log_stores[slot].data32 & 0xffff;
            HEX_DEBUG_LOG("\tmemh[0x" TARGET_FMT_lx "] = %d (0x%04x)\n",
                          env->mem_log_stores[slot].va, data, data);
        } else if (width == 4) {
            size4u_t data = env->mem_log_stores[slot].data32;
            HEX_DEBUG_LOG("\tmemw[0x" TARGET_FMT_lx "] = %d (0x%08x)\n",
                          env->mem_log_stores[slot].va, data, data);
        } else if (width == 8) {
            HEX_DEBUG_LOG("\tmemd[0x" TARGET_FMT_lx "] = %lu (0x%016lx)\n",
                          env->mem_log_stores[slot].va,
                          env->mem_log_stores[slot].data64,
                          env->mem_log_stores[slot].data64);
        } else {
            HEX_DEBUG_LOG("\tBad store width %d\n", width);
            g_assert_not_reached();
        }
    }
}

/* This function is a handy place to set a breakpoint */
void HELPER(debug_commit_end)(CPUHexagonState *env, int has_st0, int has_st1)
{
    bool reg_printed = false;
    bool pred_printed = false;
    int i;

    HEX_DEBUG_LOG("Packet committed: pc = 0x" TARGET_FMT_lx "\n",
                  env->this_PC);
    HEX_DEBUG_LOG("slot_cancelled = %d\n", env->slot_cancelled);

    for (i = 0; i < TOTAL_PER_THREAD_REGS; i++) {
        if (env->reg_written[i]) {
            if (!reg_printed) {
                HEX_DEBUG_LOG("Regs written\n");
                reg_printed = true;
            }
            HEX_DEBUG_LOG("\tr%d = " TARGET_FMT_ld " (0x" TARGET_FMT_lx ")\n",
                          i, env->new_value[i], env->new_value[i]);
        }
    }

    for (i = 0; i < NUM_PREGS; i++) {
        if (env->pred_written & (1 << i)) {
            if (!pred_printed) {
                HEX_DEBUG_LOG("Predicates written\n");
                pred_printed = true;
            }
            HEX_DEBUG_LOG("\tp%d = 0x" TARGET_FMT_lx "\n",
                          i, env->new_pred_value[i]);
        }
    }

    if (has_st0 || has_st1) {
        HEX_DEBUG_LOG("Stores\n");
        if (has_st0) {
            print_store(env, 0);
        }
        if (has_st1) {
            print_store(env, 1);
        }
    }

    HEX_DEBUG_LOG("Next PC = 0x%x\n", env->next_PC);
    HEX_DEBUG_LOG("Exec counters: pkt = " TARGET_FMT_lx
                  ", insn = " TARGET_FMT_lx
                  "\n",
                  env->gpr[HEX_REG_QEMU_PKT_CNT],
                  env->gpr[HEX_REG_QEMU_INSN_CNT]);

}

/*
 * Handle mem_noshuf
 *
 * This occurs when there is a load that might need data forwarded
 * from an inflight store in slot 1.  Note that the load and store
 * might have different sizes, so we can't simply compare the
 * addresses.  We merge only the bytes that overlap (if any).
 */
static int64_t merge_bytes(CPUHexagonState *env, target_ulong load_addr,
                           int64_t load_data, uint32_t load_width)
{
    /* Don't do anything if slot 1 was cancelled */
    const int store_slot = 1;
    if (env->slot_cancelled & (1 << store_slot)) {
        return load_data;
    }

    int store_width = env->mem_log_stores[store_slot].width;
    target_ulong store_addr = env->mem_log_stores[store_slot].va;
    union {
        uint8_t bytes[8];
        uint32_t data32;
        uint64_t data64;
    } retdata, storedata;
    int bigmask = ((-load_width) & (-store_width));
    if ((load_addr & bigmask) != (store_addr & bigmask)) {
        /* no overlap */
        return load_data;
    }
    retdata.data64 = load_data;

    if (store_width == 1 || store_width == 2 || store_width == 4) {
        storedata.data32 = env->mem_log_stores[store_slot].data32;
    } else if (store_width == 8) {
        storedata.data64 = env->mem_log_stores[store_slot].data64;
    } else {
        g_assert_not_reached();
    }
    int i, j;
    i = store_addr & (load_width - 1);
    j = load_addr & (store_width - 1);
    while ((i < load_width) && (j < store_width)) {
        retdata.bytes[i] = storedata.bytes[j];
        i++;
        j++;
    }
    return retdata.data64;
}

#define MERGE_INFLIGHT(NAME, RET, IN_TYPE, OUT_TYPE, SIZE) \
RET HELPER(NAME)(CPUHexagonState *env, int32_t addr, IN_TYPE data) \
{ \
    return (OUT_TYPE)merge_bytes(env, addr, data, SIZE); \
}

MERGE_INFLIGHT(merge_inflight_store1s, int32_t, int32_t,  int8_t,  1)
MERGE_INFLIGHT(merge_inflight_store1u, int32_t, int32_t, uint8_t,  1)
MERGE_INFLIGHT(merge_inflight_store2s, int32_t, int32_t,  int16_t, 2)
MERGE_INFLIGHT(merge_inflight_store2u, int32_t, int32_t, uint16_t, 2)
MERGE_INFLIGHT(merge_inflight_store4s, int32_t, int32_t,  int32_t, 4)
MERGE_INFLIGHT(merge_inflight_store4u, int32_t, int32_t, uint32_t, 4)
MERGE_INFLIGHT(merge_inflight_store8u, int64_t, int64_t,  int64_t, 8)

#define CHECK_NOSHUF(DST, VA, SZ, SIGN) \
    do { \
        if (slot == 0 && env->pkt_has_store_s1) { \
            DST = HELPER(merge_inflight_store##SZ##SIGN)(env, VA, DST); \
        } \
    } while (0)

static inline uint8_t mem_load1(CPUHexagonState *env, uint32_t slot,
                                target_ulong vaddr)
{
    uint8_t retval;
    get_user_u8(retval, vaddr);
    CHECK_NOSHUF(retval, vaddr, 1, u);
    return retval;
}

static inline uint16_t mem_load2(CPUHexagonState *env, uint32_t slot,
                                 target_ulong vaddr)
{
    uint16_t retval;
    get_user_u16(retval, vaddr);
    CHECK_NOSHUF(retval, vaddr, 2, u);
    return retval;
}

static inline uint32_t mem_load4(CPUHexagonState *env, uint32_t slot,
                                 target_ulong vaddr)
{
    uint32_t retval;
    get_user_u32(retval, vaddr);
    CHECK_NOSHUF(retval, vaddr, 4, u);
    return retval;
}

static inline uint64_t mem_load8(CPUHexagonState *env, uint32_t slot,
                                 target_ulong vaddr)
{
    uint64_t retval;
    get_user_u64(retval, vaddr);
    CHECK_NOSHUF(retval, vaddr, 8, u);
    return retval;
}

/* Helpful for printing intermediate values within instructions */
void HELPER(debug_value)(CPUHexagonState *env, int32_t value)
{
    HEX_DEBUG_LOG("value = 0x%x\n", value);
}

void HELPER(debug_value_i64)(CPUHexagonState *env, int64_t value)
{
    HEX_DEBUG_LOG("value_i64 = 0x%lx\n", value);
}

static void cancel_slot(CPUHexagonState *env, uint32_t slot)
{
    HEX_DEBUG_LOG("Slot %d cancelled\n", slot);
    env->slot_cancelled |= (1 << slot);
}

/* These macros can be referenced in the generated helper functions */
#define warn(...) /* Nothing */
#define fatal(...) g_assert_not_reached();

#define BOGUS_HELPER(tag) \
    printf("ERROR: bogus helper: " #tag "\n")

#include "helper_funcs_generated.h"

