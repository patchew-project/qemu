/*
 * Hexagon ELF core dump notes owned by "QEMU".
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "hw/hexagon/hexagon_hlx.h"
#include "mmvec/mmvec_qfloat.h"
#ifndef CONFIG_USER_ONLY
#include "hw/hexagon/hexagon_globalreg.h"
#include "hw/hexagon/hexagon_tlb.h"
#include "cpu_helper.h"
#endif
#include "core-dump.h"

typedef struct NoteCursor {
    uint8_t *buf;   /* NULL to only measure */
    size_t len;
} NoteCursor;

typedef void (*NoteFill)(NoteCursor *c, const CPUHexagonState *env);

static void put_bytes(NoteCursor *c, const void *data, size_t size)
{
    if (c->buf) {
        memcpy(c->buf + c->len, data, size);
    }
    c->len += size;
}

static void put_zeros(NoteCursor *c, size_t size)
{
    if (c->buf) {
        memset(c->buf + c->len, 0, size);
    }
    c->len += size;
}

static void put_u32(NoteCursor *c, uint32_t val)
{
    uint32_t le = cpu_to_le32(val);

    put_bytes(c, &le, sizeof(le));
}

static void put_u64(NoteCursor *c, uint64_t val)
{
    uint64_t le = cpu_to_le64(val);

    put_bytes(c, &le, sizeof(le));
}

static void put_note(NoteCursor *c, uint32_t type, NoteFill fill,
                     const CPUHexagonState *env)
{
    NoteCursor desc = { };

    fill(&desc, env);

    put_u32(c, sizeof(HEXAGON_CORE_NOTE_OWNER));
    put_u32(c, desc.len);
    put_u32(c, type);
    put_bytes(c, HEXAGON_CORE_NOTE_OWNER, sizeof(HEXAGON_CORE_NOTE_OWNER));
    put_zeros(c, ROUND_UP(sizeof(HEXAGON_CORE_NOTE_OWNER), 4) -
                 sizeof(HEXAGON_CORE_NOTE_OWNER));
    fill(c, env);
    put_zeros(c, ROUND_UP(desc.len, 4) - desc.len);
}

/*
 * Layout: version, FRAMELIMIT, FRAMEKEY, VFRAMELIMIT, VSP.
 */
static void fill_ctrl(NoteCursor *c, const CPUHexagonState *env)
{
    put_u32(c, HEXAGON_CORE_NOTE_VERSION);
    put_u32(c, env->gpr[HEX_REG_FRAMELIMIT]);
    put_u32(c, env->gpr[HEX_REG_FRAMEKEY]);
    put_u32(c, env->gpr[HEX_REG_VFRAMELIMIT]);
    put_u32(c, env->gpr[HEX_REG_VSP]);
}

static const HexagonHVXContext *note_hvx_ctx(const CPUHexagonState *env)
{
#ifdef CONFIG_USER_ONLY
    return &env->hvx_ctx;
#else
    return env->hvx;
#endif
}

/*
 * Layout: version, vlen (the size of a vector register in bytes), nvregs,
 * nqregs and ext, then the nvregs vector registers, then the nqregs predicate
 * registers of vlen / 8 bytes each.  If ext is 1, which it is where qfloat
 * has extension bits, V79 and later, the vlen / 4 bytes of extension bits of
 * each vector register follow.
 */
static void fill_hvx(NoteCursor *c, const CPUHexagonState *env)
{
    const HexagonHVXContext *hvx = note_hvx_ctx(env);

    put_u32(c, HEXAGON_CORE_NOTE_VERSION);
    put_u32(c, sizeof_field(MMVector, ub));
    put_u32(c, NUM_VREGS);
    put_u32(c, NUM_QREGS);
    put_u32(c, qfloat_is_extended(env));

    for (int i = 0; i < NUM_VREGS; i++) {
        for (int j = 0; j < ARRAY_SIZE(hvx->VRegs[i].ud); j++) {
            put_u64(c, hvx->VRegs[i].ud[j]);
        }
    }
    for (int i = 0; i < NUM_QREGS; i++) {
        for (int j = 0; j < ARRAY_SIZE(hvx->QRegs[i].ud); j++) {
            put_u64(c, hvx->QRegs[i].ud[j]);
        }
    }
    if (qfloat_is_extended(env)) {
        for (int i = 0; i < NUM_VREGS; i++) {
            put_bytes(c, hvx->VRegs[i].ext, sizeof(hvx->VRegs[i].ext));
        }
    }
}

/*
 * Layout: version, vlen (the size of an HLX register in bytes), nlregs and
 * zero, then the nlregs registers.
 */
static void fill_hlx(NoteCursor *c, const CPUHexagonState *env)
{
    const HexagonHLXContext *hlx = env->hlx_ctx;

    put_u32(c, HEXAGON_CORE_NOTE_VERSION);
    put_u32(c, HLX_VEC_SIZE_BYTES);
    put_u32(c, NUM_LREGS);
    put_u32(c, 0);

    for (int i = 0; i < NUM_LREGS; i++) {
        for (int j = 0; j < ARRAY_SIZE(hlx->LRegs[i].ud); j++) {
            put_u64(c, hlx->LRegs[i].ud[j]);
        }
    }
}

#ifndef CONFIG_USER_ONLY
/*
 * Layout: version, nsregs, ngregs and zero, then the nsregs system registers
 * by register number, with the thread's own ones below HEX_SREG_GLB_START
 * and the global ones from there on, then the ngregs guest registers.
 */
static void fill_sysregs(NoteCursor *c, const CPUHexagonState *env)
{
    const HexagonCPU *cpu = env_archcpu(env);

    put_u32(c, HEXAGON_CORE_NOTE_VERSION);
    put_u32(c, NUM_SREGS);
    put_u32(c, NUM_GREGS);
    put_u32(c, 0);

    for (int i = 0; i < NUM_SREGS; i++) {
        if (i < HEX_SREG_GLB_START) {
            put_u32(c, env->t_sreg[i]);
        } else {
            put_u32(c, cpu->globalregs ?
                    hexagon_globalreg_read(cpu->globalregs, i,
                                           env->threadId) : 0);
        }
    }
    for (int i = 0; i < NUM_GREGS; i++) {
        put_u32(c, hexagon_greg_read(env, i));
    }
}

/*
 * Layout: version, the thread number, the CPU mode (HEX_CPU_MODE_*), the TLB
 * lock state and count, and the K0 lock state and count.
 */
static void fill_thread(NoteCursor *c, const CPUHexagonState *env)
{
    put_u32(c, HEXAGON_CORE_NOTE_VERSION);
    put_u32(c, env->threadId);
    put_u32(c, get_cpu_mode(env));
    put_u32(c, env->tlb_lock_state);
    put_u32(c, env->tlb_lock_count);
    put_u32(c, env->k0_lock_state);
    put_u32(c, env->k0_lock_count);
}

/*
 * Layout: version, the number of entries and two zeroes, then the entries as
 * the 64-bit values that tlbw takes.
 */
static void fill_tlb(NoteCursor *c, const CPUHexagonState *env)
{
    HexagonTLBState *tlb = env_archcpu(env)->tlb;
    uint32_t num_entries = hexagon_tlb_get_num_entries(tlb);

    put_u32(c, HEXAGON_CORE_NOTE_VERSION);
    put_u32(c, num_entries);
    put_u32(c, 0);
    put_u32(c, 0);

    for (uint32_t i = 0; i < num_entries; i++) {
        put_u64(c, hexagon_tlb_read(tlb, i));
    }
}
#endif

static void put_all_notes(NoteCursor *c, const CPUHexagonState *env)
{
    const HexagonCPU *cpu = env_archcpu(env);

    put_note(c, HEXAGON_NT_CTRL, fill_ctrl, env);
    if (note_hvx_ctx(env) && cpu->cfg.hex_def->hex_version >= HEX_VER_V60) {
        put_note(c, HEXAGON_NT_HVX, fill_hvx, env);
    }
    if (env->hlx_ctx && cpu->cfg.hex_def->hex_version >= HEX_VER_V85) {
        put_note(c, HEXAGON_NT_HLX, fill_hlx, env);
    }
#ifndef CONFIG_USER_ONLY
    put_note(c, HEXAGON_NT_SYSREGS, fill_sysregs, env);
    put_note(c, HEXAGON_NT_THREAD, fill_thread, env);
    if (cpu->tlb) {
        put_note(c, HEXAGON_NT_TLB, fill_tlb, env);
    }
#endif
}

size_t hexagon_core_notes_size(const CPUHexagonState *env)
{
    NoteCursor c = { };

    put_all_notes(&c, env);
    return c.len;
}

void hexagon_core_fill_notes(void *buf, const CPUHexagonState *env)
{
    NoteCursor c = { .buf = buf };

    put_all_notes(&c, env);
}
