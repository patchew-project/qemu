/*
 * Copyright(c) 2019-2020 rev.ng Srls. All Rights Reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef IDEF_PARSER_H
#define IDEF_PARSER_H

#include <inttypes.h>
#include <stdio.h>
#include <stdbool.h>

#define TCGV_NAME_SIZE 7
#define MAX_WRITTEN_REGS 32
#define OFFSET_STR_LEN 32
#define ALLOC_LIST_LEN 32
#define ALLOC_NAME_SIZE 32
#define INIT_LIST_LEN 32
#define OUT_BUF_LEN (1024 * 1024)
#define SIGNATURE_BUF_LEN (128 * 1024)
#define HEADER_BUF_LEN (128 * 1024)

/* Variadic macros to wrap the buffer printing functions */
#define EMIT(c, ...)                                                 \
    do {                                                             \
        (c)->out_c += snprintf((c)->out_buffer + (c)->out_c,         \
                               OUT_BUF_LEN - (c)->out_c,             \
                               __VA_ARGS__);                         \
    } while (0)

#define EMIT_SIG(c, ...)                                                       \
    do {                                                                       \
        (c)->signature_c += snprintf((c)->signature_buffer + (c)->signature_c, \
                                     SIGNATURE_BUF_LEN - (c)->signature_c,     \
                                     __VA_ARGS__);                             \
    } while (0)

#define EMIT_HEAD(c, ...)                                                      \
    do {                                                                       \
        (c)->header_c += snprintf((c)->header_buffer + (c)->header_c,          \
                                     SIGNATURE_BUF_LEN - (c)->header_c,        \
                                     __VA_ARGS__);                             \
    } while (0)

/**
 * Type of register, assigned to the HexReg.type field
 */
typedef enum {GENERAL_PURPOSE, CONTROL, MODIFIER, DOTNEW} RegType;

/**
 * Types of control registers, assigned to the HexReg.id field
 */
typedef enum {SP, FP, LR, GP, LC0, LC1, SA0, SA1} CregType;

/**
 * Identifier string of the control registers, indexed by the CregType enum
 */
extern const char *creg_str[];

/**
 * Semantic record of the REG tokens, identifying registers
 */
typedef struct HexReg {
    CregType id;            /**< Identifier of the register                  */
    RegType type;           /**< Type of the register                        */
    unsigned bit_width;     /**< Bit width of the reg, 32 or 64 bits         */
} HexReg;

/**
 * Data structure, identifying a TCGv temporary value
 */
typedef struct HexTmp {
    int index;              /**< Index of the TCGv temporary value    */
} HexTmp;

/**
 * Enum of the possible immediated, an immediate is a value which is known
 * at tinycode generation time, e.g. an integer value, not a TCGv
 */
enum ImmUnionTag {I, VARIABLE, VALUE, QEMU_TMP, IMM_PC, IMM_CONSTEXT};

/**
 * Semantic record of the IMM token, identifying an immediate constant
 */
typedef struct HexImm {
    union {
        char id;            /**< Identifier of the immediate                 */
        uint64_t value;     /**< Immediate value (for VALUE type immediates) */
        uint64_t index;     /**< Index of the immediate (for int temp vars)  */
    };
    enum ImmUnionTag type;  /**< Type of the immediate                      */
} HexImm;

/**
 * Semantic record of the PRE token, identifying a predicate
 */
typedef struct HexPre {
    char id;                /**< Identifier of the predicate                 */
} HexPre;

/**
 * Semantic record of the SAT token, identifying the saturate operator
 */
typedef struct HexSat {
    bool set_overflow;      /**< Set-overflow feature for the sat operator   */
    bool is_unsigned;       /**< Unsigned flag for the saturate operator     */
} HexSat;

/**
 * Semantic record of the CAST token, identifying the cast operator
 */
typedef struct HexCast {
    int bit_width;          /**< Bit width of the cast operator              */
    bool is_unsigned;       /**< Unsigned flag for the cast operator         */
} HexCast;

/**
 * Semantic record of the EXTRACT token, identifying the cast operator
 */
typedef struct HexExtract {
    int bit_width;          /**< Bit width of the extract operator           */
    int storage_bit_width;  /**< Actual bit width of the extract operator    */
    bool is_unsigned;       /**< Unsigned flag for the extract operator      */
} HexExtract;

/**
 * Semantic record of the MPY token, identifying the fMPY multiplication
 * operator
 */
typedef struct HexMpy {
    int first_bit_width;    /**< Bit width of the first operand of fMPY op   */
    int second_bit_width;   /**< Bit width of the second operand of fMPY     */
    bool first_unsigned;    /**< Unsigned flag for the first operand of fMPY */
    bool second_unsigned;   /**< Unsigned flag for second operand of fMPY    */
} HexMpy;

/**
 * Semantic record of the VARID token, identifying automatic variables
 * of the input language
 */
typedef struct HexVar {
    char *name;             /**< Name of the VARID automatic variable        */
} HexVar;

/**
 * Data structure uniquely identifying an automatic VARID variable, used for
 * keeping track of declared variable, so that any variable is declared only
 * once, and its properties are propagated through all the subsequent instances
 * of that variable
 */
typedef struct Var {
    char *name;             /**< Name of the VARID automatic variable        */
    uint8_t bit_width;      /**< Bit width of the VARID automatic variable   */
    bool is_unsigned;       /**< Unsigned flag for the VARID automatic var   */
} Var;

/**
 * Enum of the possible rvalue types, used in the HexValue.type field
 */
enum RvalueUnionTag {REGISTER, TEMP, IMMEDIATE, PREDICATE, VARID};

/**
 * Semantic record of the rvalue token, identifying any numeric value,
 * immediate or register based. The rvalue tokens are combined together
 * through the use of several operators, to encode expressions
 */
typedef struct HexValue {
    union {
        HexReg reg;      /**< rvalue of register type                     */
        HexTmp tmp;      /**< rvalue of temporary type                    */
        HexImm imm;      /**< rvalue of immediate type                    */
        HexPre pre;      /**< rvalue of predicate type                    */
        HexVar var;      /**< rvalue of automatic variable type           */
    };
    enum RvalueUnionTag type; /**< Type of the rvalue                        */
    unsigned bit_width;     /**< Bit width of the rvalue                     */
    bool is_unsigned;       /**< Unsigned flag for the rvalue                */
    bool is_dotnew;         /**< rvalue of predicate type is dotnew?         */
    bool is_manual;         /**< Opt out of automatic freeing of params      */
} HexValue;

/**
 * Operator type, used for referencing the correct operator when calling the
 * gen_bin_op() function, which in turn will generate the correct code to
 * execute the operation between the two rvalues
 */
enum OpType {ADD_OP, SUB_OP, MUL_OP, DIV_OP, ASL_OP, ASR_OP, LSR_OP, ANDB_OP,
             ORB_OP, XORB_OP, ANDL_OP, MINI_OP, MAXI_OP, MOD_OP};

/**
 * Data structure including instruction specific information, to be cleared
 * out after the compilation of each instruction
 */
typedef struct Inst {
    char *name;                   /**< Name of the compiled instruction      */
    char *code_begin;             /**< Beginning of instruction input code   */
    char *code_end;               /**< End of instruction input code         */
    int tmp_count;                /**< Index of the last declared TCGv temp  */
    int qemu_tmp_count;           /**< Index of the last declared int temp   */
    int if_count;                 /**< Index of the last declared if label   */
    int error_count;              /**< Number of generated errors            */
    Var allocated[ALLOC_LIST_LEN]; /**< Allocated VARID automatic vars       */
    int allocated_count;          /**< Elements contained in allocated[]     */
    HexValue init_list[INIT_LIST_LEN]; /**< List of initialized registers    */
    int init_count;               /**< Number of members of init_list        */
} Inst;

/**
 * Data structure representing the whole translation context, which in a
 * reentrant flex/bison parser just like ours is passed between the scanner
 * and the parser, holding all the necessary information to perform the
 * parsing, this data structure survives between the compilation of different
 * instructions
 *
 */
typedef struct Context {
    void *scanner;                /**< Reentrant parser state pointer        */
    char *input_buffer;           /**< Buffer containing the input code      */
    char *out_buffer;             /**< Buffer containing the output code     */
    int out_c;                    /**< Characters emitted into out_buffer    */
    char *signature_buffer;       /**< Buffer containing the signatures code */
    int signature_c;              /**< Characters emitted into sig..._buffer */
    char *header_buffer;          /**< Buffer containing the output code     */
    int header_c;                 /**< Characters emitted into header buffer */
    FILE *defines_file;           /**< FILE * of the generated header        */
    FILE *output_file;            /**< FILE * of the C output file           */
    FILE *enabled_file;           /**< FILE * of the list of enabled inst    */
    int total_insn;               /**< Number of instructions in input file  */
    int implemented_insn;         /**< Instruction compiled without errors   */
    Inst inst;                  /**< Parsing data of the current inst      */
} Context;

#endif /* IDEF_PARSER_H */
