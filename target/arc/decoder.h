/*
 * Decoder for the ARC.
 * Copyright 2020 Free Software Foundation, Inc.
 *
 * QEMU ARCv2 Decoder.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ARC_DECODER_H
#define ARC_DECODER_H

#include "arc-common.h"

#ifndef MAX_INSN_ARGS
#define MAX_INSN_ARGS     16
#endif

#ifndef MAX_INSN_FLGS
#define MAX_INSN_FLGS     4
#endif

/* Instruction Class. */
typedef enum {
    NADA = 0,
    ARC_ACL,
    ARITH,
    AUXREG,
    BBIT0,
    BBIT1,
    BI,
    BIH,
    BITOP,
    BITSTREAM,
    BMU,
    BRANCH,
    BRCC,
    CONTROL,
    DIVREM,
    DPI,
    DSP,
    EI,
    ENTER,
    ARC_FLOAT,
    INVALID,
    JLI,
    JUMP,
    KERNEL,
    LEAVE,
    LOAD,
    LOGICAL,
    LOOP,
    MEMORY,
    MOVE,
    MPY,
    NET,
    PROTOCOL_DECODE,
    PMU,
    POP,
    PUSH,
    SJLI,
    STORE,
    SUB,
    XY
} insn_class_t;

/* Instruction Subclass. */
typedef enum {
    NONE     = 0,
    CVT      = (1U << 1),
    BTSCN    = (1U << 2),
    CD       = (1U << 3),
    CD1      = CD,
    CD2      = CD,
    COND     = (1U << 4),
    DIV      = (1U << 5),
    DP       = (1U << 6),
    DPA      = (1U << 7),
    DPX      = (1U << 8),
    MPY1E    = (1U << 9),
    MPY6E    = (1U << 10),
    MPY7E    = (1U << 11),
    MPY8E    = (1U << 12),
    MPY9E    = (1U << 13),
    QUARKSE1 = (1U << 15),
    QUARKSE2 = (1U << 16),
    SHFT1    = (1U << 17),
    SHFT2    = (1U << 18),
    SWAP     = (1U << 19),
    SP       = (1U << 20),
    SPX      = (1U << 21)
} insn_subclass_t;

/* Flags class. */
typedef enum {
    F_CLASS_NONE = 0,

    /*
     * At most one flag from the set of flags can appear in the
     * instruction.
     */
    F_CLASS_OPTIONAL = (1 << 0),

    /*
     * Exactly one from from the set of flags must appear in the
     * instruction.
     */
    F_CLASS_REQUIRED = (1 << 1),

    /*
     * The conditional code can be extended over the standard variants
     * via .extCondCode pseudo-op.
     */
    F_CLASS_EXTEND = (1 << 2),

    /* Condition code flag. */
    F_CLASS_COND = (1 << 3),

    /* Write back mode. */
    F_CLASS_WB = (1 << 4),

    /* Data size. */
    F_CLASS_ZZ = (1 << 5),

    /* Implicit flag. */
    F_CLASS_IMPLICIT = (1 << 6),

    F_CLASS_F = (1 << 7),

    F_CLASS_DI = (1 << 8),

    F_CLASS_X = (1 << 9),
    F_CLASS_D = (1 << 10),

} flag_class_t;

/* The opcode table is an array of struct arc_opcode. */
struct arc_opcode {
    /* The opcode name. */
    const char *name;

    /*
     * The opcode itself. Those bits which will be filled in with
     * operands are zeroes.
     */
    unsigned long long opcode;

    /*
     * The opcode mask. This is used by the disassembler. This is a
     * mask containing ones indicating those bits which must match the
     * opcode field, and zeroes indicating those bits which need not
     * match (and are presumably filled in by operands).
     */
    unsigned long long mask;

    /*
     * One bit flags for the opcode. These are primarily used to
     * indicate specific processors and environments support the
     * instructions. The defined values are listed below.
     */
    unsigned cpu;

    /* The instruction class. */
    insn_class_t insn_class;

    /* The instruction subclass. */
    insn_subclass_t subclass;

    /*
     * An array of operand codes. Each code is an index into the
     * operand table. They appear in the order which the operands must
     * appear in assembly code, and are terminated by a zero.
     */
    unsigned char operands[MAX_INSN_ARGS + 1];

    /*
     * An array of flag codes. Each code is an index into the flag
     * table. They appear in the order which the flags must appear in
     * assembly code, and are terminated by a zero.
     */
    unsigned char flags[MAX_INSN_FLGS + 1];
};

/* The operands table is an array of struct arc_operand. */
struct arc_operand {
    /* The number of bits in the operand. */
    unsigned int bits;

    /* How far the operand is left shifted in the instruction. */
    unsigned int shift;

    /* One bit syntax flags. */
    unsigned int flags;

    /*
     * Extraction function. This is used by the disassembler. To
     * extract this operand type from an instruction, check this
     * field.
     *
     * If it is NULL, compute
     * op = ((i) >> o->shift) & ((1 << o->bits) - 1);
     * if ((o->flags & ARC_OPERAND_SIGNED) != 0
     * && (op & (1 << (o->bits - 1))) != 0)
     * op -= 1 << o->bits;
     * (i is the instruction, o is a pointer to this structure, and op
     * is the result; this assumes twos complement arithmetic).
     *
     * If this field is not NULL, then simply call it with the
     * instruction value. It will return the value of the operand.
     * If the INVALID argument is not NULL, *INVALID will be set to
     * TRUE if this operand type can not actually be extracted from
     * this operand (i.e., the instruction does not match). If the
     * operand is valid, *INVALID will not be changed.
     */
    long long int (*extract) (unsigned long long instruction,
                              bool *invalid);
};

extern const struct arc_operand arc_operands[];

/* Values defined for the flags field of a struct arc_operand. */

/*
 * This operand does not actually exist in the assembler input. This
 * is used to support extended mnemonics, for which two operands
 * fields are identical. The assembler should call the insert
 * function with any op value. The disassembler should call the
 * extract function, ignore the return value, and check the value
 * placed in the invalid argument.
 */
#define ARC_OPERAND_FAKE        0x0001

/* This operand names an integer register. */
#define ARC_OPERAND_IR          0x0002

/* This operand takes signed values. */
#define ARC_OPERAND_SIGNED      0x0004

/*
 * This operand takes unsigned values. This exists primarily so that
 * a flags value of 0 can be treated as end-of-arguments.
 */
#define ARC_OPERAND_UNSIGNED    0x0008

/* This operand takes short immediate values. */
#define ARC_OPERAND_SHIMM   (ARC_OPERAND_SIGNED | ARC_OPERAND_UNSIGNED)

/* This operand takes long immediate values. */
#define ARC_OPERAND_LIMM        0x0010

/* This operand is identical like the previous one. */
#define ARC_OPERAND_DUPLICATE   0x0020

/* This operand is PC relative. Used for internal relocs. */
#define ARC_OPERAND_PCREL       0x0040

/*
 * This operand is truncated. The truncation is done accordingly to
 * operand alignment attribute.
 */
#define ARC_OPERAND_TRUNCATE    0x0080

/* This operand is 16bit aligned. */
#define ARC_OPERAND_ALIGNED16   0x0100

/* This operand is 32bit aligned. */
#define ARC_OPERAND_ALIGNED32   0x0200

/*
 * This operand can be ignored by matching process if it is not
 * present.
 */
#define ARC_OPERAND_IGNORE      0x0400

/* Don't check the range when matching. */
#define ARC_OPERAND_NCHK        0x0800

/* Mark the braket possition. */
#define ARC_OPERAND_BRAKET      0x1000

/* Mask for selecting the type for typecheck purposes. */
#define ARC_OPERAND_TYPECHECK_MASK               \
    (ARC_OPERAND_IR                              \
     | ARC_OPERAND_LIMM     | ARC_OPERAND_SIGNED \
     | ARC_OPERAND_UNSIGNED | ARC_OPERAND_BRAKET)

/* Macro to determine if an operand is a fake operand. */
#define ARC_OPERAND_IS_FAKE(op)                     \
    ((operand->flags & ARC_OPERAND_FAKE)            \
     && !(operand->flags & ARC_OPERAND_BRAKET))

/* The flags structure. */
struct arc_flag_operand {
    /* The flag name. */
    const char *name;

    /* The flag code. */
    unsigned code;

    /* The number of bits in the operand. */
    unsigned int bits;

    /* How far the operand is left shifted in the instruction. */
    unsigned int shift;

    /* Available for disassembler. */
    unsigned char favail;
};

extern const struct arc_flag_operand arc_flag_operands[];

/* The flag's class structure. */
struct arc_flag_class {
    /* Flag class. */
    flag_class_t flag_class;

    /* List of valid flags (codes). */
    unsigned flags[256];
};

extern const struct arc_flag_class arc_flag_classes[];

/* Structure for special cases. */
struct arc_flag_special {
    /* Name of special case instruction. */
    const char *name;

    /* List of flags applicable for special case instruction. */
    unsigned flags[32];
};

extern const struct arc_flag_special arc_flag_special_cases[];
extern const unsigned arc_num_flag_special;

const struct arc_opcode *arc_find_format(insn_t*, uint64_t, uint8_t, uint32_t);
unsigned int arc_insn_length(uint16_t, uint16_t);

#endif
