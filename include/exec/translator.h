/*
 * Generic intermediate code generation.
 *
 * Copyright (C) 2016-2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef EXEC__TRANSLATOR_H
#define EXEC__TRANSLATOR_H

/*
 * Include this header from a target-specific file, and add a
 *
 *     DisasContextBase base;
 *
 * member in your target-specific DisasContext.
 */


#include "exec/exec-all.h"
#include "tcg/tcg.h"


/**
 * BreakpointCheckType:
 * @BC_MISS: No hit
 * @BC_HIT_INSN: Hit, but continue translating TB
 * @BC_HIT_TB: Hit, stop translating TB
 *
 * How to react to a breakpoint. A hit means no more breakpoints will be checked
 * for the current instruction.
 *
 * Not all breakpoints associated to an address are necessarily raised by
 * targets (e.g., due to conditions encoded in their flags), so tey can decide
 * that a breakpoint missed the address (@BP_MISS).
 */
typedef enum BreakpointCheckType {
    BC_MISS,
    BC_HIT_INSN,
    BC_HIT_TB,
} BreakpointCheckType;

/**
 * DisasJumpType:
 * @DISAS_NEXT: Next instruction in program order.
 * @DISAS_TOO_MANY: Too many instructions translated.
 * @DISAS_TARGET: Start of target-specific conditions.
 *
 * What instruction to disassemble next.
 */
typedef enum DisasJumpType {
    DISAS_NEXT,
    DISAS_TOO_MANY,
    DISAS_TARGET_0,
    DISAS_TARGET_1,
    DISAS_TARGET_2,
    DISAS_TARGET_3,
    DISAS_TARGET_4,
    DISAS_TARGET_5,
    DISAS_TARGET_6,
    DISAS_TARGET_7,
    DISAS_TARGET_8,
    DISAS_TARGET_9,
    DISAS_TARGET_10,
    DISAS_TARGET_11,
    DISAS_TARGET_12,
    DISAS_TARGET_13,
    DISAS_TARGET_14,
} DisasJumpType;

/**
 * DisasContextBase:
 * @tb: Translation block for this disassembly.
 * @pc_first: Address of first guest instruction in this TB.
 * @pc_next: Address of next guest instruction in this TB (current during
 *           disassembly).
 * @is_jmp: What instruction to disassemble next.
 * @num_insns: Number of translated instructions (including current).
 * @singlestep_enabled: "Hardware" single stepping enabled.
 *
 * Architecture-agnostic disassembly context.
 */
typedef struct DisasContextBase {
    TranslationBlock *tb;
    target_ulong pc_first;
    target_ulong pc_next;
    DisasJumpType is_jmp;
    unsigned int num_insns;
    bool singlestep_enabled;
} DisasContextBase;

/**
 * TranslatorOps:
 * @init_disas_context: Initialize a DisasContext struct (DisasContextBase has
 *                      already been initialized).
 * @init_globals: Initialize global variables.
 * @tb_start: Start translating a new TB.
 * @insn_start: Start translating a new instruction.
 * @breakpoint_check: Check if a breakpoint did hit. When called, the breakpoint
 *                    has already been checked to match the PC.
 * @disas_insn: Disassemble one instruction an return the PC for the next
 *              one. Can set db->is_jmp to DJ_TARGET or above to stop
 *              translation.
 * @tb_stop: Stop translating a TB.
 * @disas_flags: Get flags argument for log_target_disas().
 *
 * Target-specific operations for the generic translator loop.
 */
typedef struct TranslatorOps {
    void (*init_disas_context)(DisasContextBase *db, CPUState *cpu);
    void (*init_globals)(DisasContextBase *db, CPUState *cpu);
    void (*tb_start)(DisasContextBase *db, CPUState *cpu);
    void (*insn_start)(DisasContextBase *db, CPUState *cpu);
    BreakpointCheckType (*breakpoint_check)(DisasContextBase *db, CPUState *cpu,
                                            const CPUBreakpoint *bp);
    target_ulong (*translate_insn)(DisasContextBase *db, CPUState *cpu);
    void (*tb_stop)(DisasContextBase *db, CPUState *cpu);
    void (*disas_log)(const DisasContextBase *db, CPUState *cpu);
} TranslatorOps;

/**
 * translate_block:
 * @ops: Target-specific operations.
 * @db: Disassembly context.
 * @cpu: Target vCPU.
 * @tb: Translation block.
 *
 * Generic translator loop.
 *
 * Translation will stop in the following cases (in order):
 * - When set by #TranslatorOps::insn_start.
 * - When set by #TranslatorOps::translate_insn.
 * - When the TCG operation buffer is full.
 * - When single-stepping is enabled (system-wide or on the current vCPU).
 * - When too many instructions have been translated.
 */
void translate_block(const TranslatorOps *ops, DisasContextBase *db,
                     CPUState *cpu, TranslationBlock *tb);

#endif  /* EXEC__TRANSLATOR_H */
