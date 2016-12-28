/*
 * Generic intermediate code generation.
 *
 * Copyright (C) 2016 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef EXEC__TRANSLATE_ALL_TEMPLATE_H
#define EXEC__TRANSLATE_ALL_TEMPLATE_H

/*
 * Include this header from a target-specific file, and add a
 *
 *     DisasContextBase base;
 *
 * member in your target-specific DisasContext.
 */


#include "exec/exec-all.h"


/**
 * BreakpointHitType:
 * @BH_MISS: No hit
 * @BH_HIT_INSN: Hit, but continue translating instruction
 * @BH_HIT_TB: Hit, stop translating TB
 *
 * How to react to a breakpoint hit.
 */
typedef enum BreakpointHitType {
    BH_MISS,
    BH_HIT_INSN,
    BH_HIT_TB,
} BreakpointHitType;

/**
 * DisasJumpType:
 * @DJ_NEXT: Next instruction in program order
 * @DJ_TOO_MANY: Too many instructions executed
 * @DJ_TARGET: Start of target-specific conditions
 *
 * What instruction to disassemble next.
 */
typedef enum DisasJumpType {
    DJ_NEXT,
    DJ_TOO_MANY,
    DJ_TARGET,
} DisasJumpType;

/**
 * DisasContextBase:
 * @tb: Translation block for this disassembly.
 * @singlestep_enabled: "Hardware" single stepping enabled.
 * @pc_first: Address of first guest instruction in this TB.
 * @pc_next: Address of next guest instruction in this TB (current during
 *           disassembly).
 * @num_insns: Number of translated instructions (including current).
 *
 * Architecture-agnostic disassembly context.
 */
typedef struct DisasContextBase {
    TranslationBlock *tb;
    bool singlestep_enabled;
    target_ulong pc_first;
    target_ulong pc_next;
    DisasJumpType jmp_type;
    unsigned int num_insns;
} DisasContextBase;

#endif  /* EXEC__TRANSLATE_ALL_TEMPLATE_H */
