/*
 * Hexagon Linux ELF core register layout.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HEXAGON_CORE_DUMP_H
#define HEXAGON_CORE_DUMP_H

#include "cpu.h"
#include "qemu/bswap.h"

/* Linux arch/hexagon/include/uapi/asm/user.h: struct user_regs_struct. */
typedef struct target_elf_gregset_t {
    uint32_t gpr[32];
    uint32_t sa0;
    uint32_t lc0;
    uint32_t sa1;
    uint32_t lc1;
    uint32_t m0;
    uint32_t m1;
    uint32_t usr;
    uint32_t p3_0;
    uint32_t gp;
    uint32_t ugp;
    uint32_t pc;
    uint32_t cause;
    uint32_t badva;
    uint32_t cs0;
    uint32_t cs1;
    uint32_t pad;
} HexagonUserRegs;

QEMU_BUILD_BUG_ON(sizeof(HexagonUserRegs) != 48 * sizeof(uint32_t));

#define HEXAGON_CORE_NOTE_OWNER "QEMU"
#define HEXAGON_CORE_NOTE_VERSION 1

enum {
    HEXAGON_NT_CTRL = 0x100,
    HEXAGON_NT_HVX = 0x101,
    HEXAGON_NT_HLX = 0x102,
    HEXAGON_NT_SYSREGS = 0x103,
    HEXAGON_NT_THREAD = 0x104,
    HEXAGON_NT_TLB = 0x105,
};

size_t hexagon_core_notes_size(const CPUHexagonState *env);

void hexagon_core_fill_notes(void *buf, const CPUHexagonState *env);

static inline void hexagon_core_copy_regs(HexagonUserRegs *regs,
                                          const CPUHexagonState *env)
{
    uint32_t preds = 0;

    for (int i = 0; i < ARRAY_SIZE(regs->gpr); i++) {
        regs->gpr[i] = cpu_to_le32(env->gpr[i]);
    }
    for (int i = 0; i < NUM_PREGS; i++) {
        preds |= (env->pred[i] & 0xff) << (i * 8);
    }
    regs->sa0 = cpu_to_le32(env->gpr[HEX_REG_SA0]);
    regs->lc0 = cpu_to_le32(env->gpr[HEX_REG_LC0]);
    regs->sa1 = cpu_to_le32(env->gpr[HEX_REG_SA1]);
    regs->lc1 = cpu_to_le32(env->gpr[HEX_REG_LC1]);
    regs->m0 = cpu_to_le32(env->gpr[HEX_REG_M0]);
    regs->m1 = cpu_to_le32(env->gpr[HEX_REG_M1]);
    regs->usr = cpu_to_le32(env->gpr[HEX_REG_USR]);
    regs->p3_0 = cpu_to_le32(preds);
    regs->gp = cpu_to_le32(env->gpr[HEX_REG_GP]);
    regs->ugp = cpu_to_le32(env->gpr[HEX_REG_UGP]);
    regs->pc = cpu_to_le32(env->gpr[HEX_REG_PC]);
#ifdef CONFIG_USER_ONLY
    regs->cause = cpu_to_le32(env->cause_code);
    regs->badva = 0;
#else
    regs->cause = cpu_to_le32(extract32(env->t_sreg[HEX_SREG_SSR],
                                      reg_field_info[SSR_CAUSE].offset,
                                      reg_field_info[SSR_CAUSE].width));
    regs->badva = cpu_to_le32(env->t_sreg[HEX_SREG_BADVA]);
#endif
    regs->cs0 = cpu_to_le32(env->gpr[HEX_REG_CS0]);
    regs->cs1 = cpu_to_le32(env->gpr[HEX_REG_CS1]);
    regs->pad = 0;
}

#endif /* HEXAGON_CORE_DUMP_H */
