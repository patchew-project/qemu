/* SPDX-License-Identifier: GPL-2.0-or-later */
/* RISC-V instruction length classification. */

#ifndef TARGET_RISCV_INSN_LENGTH_H
#define TARGET_RISCV_INSN_LENGTH_H

static inline int insn_len(uint16_t first_word)
{
    return (first_word & 3) == 3 ? 4 : 2;
}

#endif
