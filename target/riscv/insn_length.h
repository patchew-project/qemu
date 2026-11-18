/* SPDX-License-Identifier: GPL-2.0-or-later */
/* RISC-V instruction length classification. */

#ifndef TARGET_RISCV_INSN_LENGTH_H
#define TARGET_RISCV_INSN_LENGTH_H

static inline int insn_len(uint16_t first_word)
{
    return (first_word & 3) == 3 ? 4 : 2;
}

static inline bool riscv_insn_is_long(uint16_t first_parcel)
{
    return (first_parcel & 0x1f) == 0x1f;
}

/* Call only after fetching parcel C; zero means an undefined length. */
static inline unsigned riscv_zl_insn_length(uint16_t third_parcel)
{
    if ((third_parcel & 3) != 3) {
        return 6;
    }
    if ((third_parcel & 0x1f) != 0x1f) {
        return 8;
    }
    return 0;
}

#endif
