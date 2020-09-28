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

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "iclass.h"
#include "opcodes.h"
#include "genptr.h"
#include "decode.h"
#include "insn.h"
#include "printinsn.h"

#define fZXTN(N, M, VAL) ((VAL) & ((1LL << (N)) - 1))

enum {
    EXT_IDX_noext = 0,
    EXT_IDX_noext_AFTER = 4,
    EXT_IDX_mmvec = 4,
    EXT_IDX_mmvec_AFTER = 8,
    XX_LAST_EXT_IDX
};

/*
 *  Certain operand types represent a non-contiguous set of values.
 *  For example, the compound compare-and-jump instruction can only access
 *  registers R0-R7 and R16-23.
 *  This table represents the mapping from the encoding to the actual values.
 */

#define DEF_REGMAP(NAME, ELEMENTS, ...) \
    static const unsigned int DECODE_REGISTER_##NAME[ELEMENTS] = \
    { __VA_ARGS__ };
        /* Name   Num Table */
DEF_REGMAP(R_16,  16, 0, 1, 2, 3, 4, 5, 6, 7, 16, 17, 18, 19, 20, 21, 22, 23)
DEF_REGMAP(R__8,  8,  0, 2, 4, 6, 16, 18, 20, 22)
DEF_REGMAP(R__4,  4,  0, 2, 4, 6)
DEF_REGMAP(R_4,   4,  0, 1, 2, 3)
DEF_REGMAP(R_8S,  8,  0, 1, 2, 3, 16, 17, 18, 19)
DEF_REGMAP(R_8,   8,  0, 1, 2, 3, 4, 5, 6, 7)
DEF_REGMAP(V__8,  8,  0, 4, 8, 12, 16, 20, 24, 28)
DEF_REGMAP(V__16, 16, 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30)

#define DECODE_MAPPED_REG(REGNO, NAME) \
    insn->regno[REGNO] = DECODE_REGISTER_##NAME[insn->regno[REGNO]];

typedef struct {
    struct _dectree_table_struct *table_link;
    struct _dectree_table_struct *table_link_b;
    opcode_t opcode;
    enum {
        DECTREE_ENTRY_INVALID,
        DECTREE_TABLE_LINK,
        DECTREE_SUBINSNS,
        DECTREE_EXTSPACE,
        DECTREE_TERMINAL
    } type;
} dectree_entry_t;

typedef struct _dectree_table_struct {
    unsigned int (*lookup_function)(int startbit, int width, uint32_t opcode);
    unsigned int size;
    unsigned int startbit;
    unsigned int width;
    dectree_entry_t table[];
} dectree_table_t;

#define DECODE_NEW_TABLE(TAG, SIZE, WHATNOT) \
    static struct _dectree_table_struct dectree_table_##TAG;
#define TABLE_LINK(TABLE)                     /* NOTHING */
#define TERMINAL(TAG, ENC)                    /* NOTHING */
#define SUBINSNS(TAG, CLASSA, CLASSB, ENC)    /* NOTHING */
#define EXTSPACE(TAG, ENC)                    /* NOTHING */
#define INVALID()                             /* NOTHING */
#define DECODE_END_TABLE(...)                 /* NOTHING */
#define DECODE_MATCH_INFO(...)                /* NOTHING */
#define DECODE_LEGACY_MATCH_INFO(...)         /* NOTHING */
#define DECODE_OPINFO(...)                    /* NOTHING */

#include "dectree_generated.h"

#undef DECODE_OPINFO
#undef DECODE_MATCH_INFO
#undef DECODE_LEGACY_MATCH_INFO
#undef DECODE_END_TABLE
#undef INVALID
#undef TERMINAL
#undef SUBINSNS
#undef EXTSPACE
#undef TABLE_LINK
#undef DECODE_NEW_TABLE
#undef DECODE_SEPARATOR_BITS

#define DECODE_SEPARATOR_BITS(START, WIDTH) NULL, START, WIDTH
#define DECODE_NEW_TABLE_HELPER(TAG, SIZE, FN, START, WIDTH) \
    static dectree_table_t dectree_table_##TAG = { \
        .size = SIZE, \
        .lookup_function = FN, \
        .startbit = START, \
        .width = WIDTH, \
        .table = {
#define DECODE_NEW_TABLE(TAG, SIZE, WHATNOT) \
    DECODE_NEW_TABLE_HELPER(TAG, SIZE, WHATNOT)

#define TABLE_LINK(TABLE) \
    { .type = DECTREE_TABLE_LINK, .table_link = &dectree_table_##TABLE },
#define TERMINAL(TAG, ENC) \
    { .type = DECTREE_TERMINAL, .opcode = TAG  },
#define SUBINSNS(TAG, CLASSA, CLASSB, ENC) \
    { \
        .type = DECTREE_SUBINSNS, \
        .table_link = &dectree_table_DECODE_SUBINSN_##CLASSA, \
        .table_link_b = &dectree_table_DECODE_SUBINSN_##CLASSB \
    },
#define EXTSPACE(TAG, ENC) { .type = DECTREE_EXTSPACE },
#define INVALID() { .type = DECTREE_ENTRY_INVALID, .opcode = XX_LAST_OPCODE },

#define DECODE_END_TABLE(...) } };

#define DECODE_MATCH_INFO(...)                /* NOTHING */
#define DECODE_LEGACY_MATCH_INFO(...)         /* NOTHING */
#define DECODE_OPINFO(...)                    /* NOTHING */

#include "dectree_generated.h"

#undef DECODE_OPINFO
#undef DECODE_MATCH_INFO
#undef DECODE_LEGACY_MATCH_INFO
#undef DECODE_END_TABLE
#undef INVALID
#undef TERMINAL
#undef SUBINSNS
#undef EXTSPACE
#undef TABLE_LINK
#undef DECODE_NEW_TABLE
#undef DECODE_NEW_TABLE_HELPER
#undef DECODE_SEPARATOR_BITS

static dectree_table_t dectree_table_DECODE_EXT_EXT_noext = {
    .size = 1, .lookup_function = NULL, .startbit = 0, .width = 0,
    .table = {
        { .type = DECTREE_ENTRY_INVALID, .opcode = XX_LAST_OPCODE },
    }
};

static dectree_table_t *ext_trees[XX_LAST_EXT_IDX];

static void decode_ext_init(void)
{
    int i;
    for (i = EXT_IDX_noext; i < EXT_IDX_noext_AFTER; i++) {
        ext_trees[i] = &dectree_table_DECODE_EXT_EXT_noext;
    }
}

typedef struct {
    uint32_t mask;
    uint32_t match;
} decode_itable_entry_t;

#define DECODE_NEW_TABLE(TAG, SIZE, WHATNOT)  /* NOTHING */
#define TABLE_LINK(TABLE)                     /* NOTHING */
#define TERMINAL(TAG, ENC)                    /* NOTHING */
#define SUBINSNS(TAG, CLASSA, CLASSB, ENC)    /* NOTHING */
#define EXTSPACE(TAG, ENC)                    /* NOTHING */
#define INVALID()                             /* NOTHING */
#define DECODE_END_TABLE(...)                 /* NOTHING */
#define DECODE_OPINFO(...)                    /* NOTHING */

#define DECODE_MATCH_INFO_NORMAL(TAG, MASK, MATCH) \
    [TAG] = { \
        .mask = MASK, \
        .match = MATCH, \
    },

#define DECODE_MATCH_INFO_NULL(TAG, MASK, MATCH) \
    [TAG] = { .match = ~0 },

#define DECODE_MATCH_INFO(...) DECODE_MATCH_INFO_NORMAL(__VA_ARGS__)
#define DECODE_LEGACY_MATCH_INFO(...) /* NOTHING */

static const decode_itable_entry_t decode_itable[XX_LAST_OPCODE] = {
#include "dectree_generated.h"
};

#undef DECODE_MATCH_INFO
#define DECODE_MATCH_INFO(...) DECODE_MATCH_INFO_NULL(__VA_ARGS__)

#undef DECODE_LEGACY_MATCH_INFO
#define DECODE_LEGACY_MATCH_INFO(...) DECODE_MATCH_INFO_NORMAL(__VA_ARGS__)

static const decode_itable_entry_t decode_legacy_itable[XX_LAST_OPCODE] = {
#include "dectree_generated.h"
};

#undef DECODE_OPINFO
#undef DECODE_MATCH_INFO
#undef DECODE_LEGACY_MATCH_INFO
#undef DECODE_END_TABLE
#undef INVALID
#undef TERMINAL
#undef SUBINSNS
#undef EXTSPACE
#undef TABLE_LINK
#undef DECODE_NEW_TABLE
#undef DECODE_SEPARATOR_BITS

void decode_init(void)
{
    decode_ext_init();
}

void decode_send_insn_to(packet_t *packet, int start, int newloc)
{
    insn_t tmpinsn;
    int direction;
    int i;
    if (start == newloc) {
        return;
    }
    if (start < newloc) {
        /* Move towards end */
        direction = 1;
    } else {
        /* move towards beginning */
        direction = -1;
    }
    for (i = start; i != newloc; i += direction) {
        tmpinsn = packet->insn[i];
        packet->insn[i] = packet->insn[i + direction];
        packet->insn[i + direction] = tmpinsn;
    }
}

/* Fill newvalue registers with the correct regno */
static int
decode_fill_newvalue_regno(packet_t *packet)
{
    int i, use_regidx, def_idx;
    uint16_t def_opcode, use_opcode;
    char *dststr;

    for (i = 1; i < packet->num_insns; i++) {
        if (GET_ATTRIB(packet->insn[i].opcode, A_DOTNEWVALUE) &&
            !GET_ATTRIB(packet->insn[i].opcode, A_EXTENSION)) {
            use_opcode = packet->insn[i].opcode;

            /* It's a store, so we're adjusting the Nt field */
            if (GET_ATTRIB(use_opcode, A_STORE)) {
                use_regidx = strchr(opcode_reginfo[use_opcode], 't') -
                    opcode_reginfo[use_opcode];
            } else {    /* It's a Jump, so we're adjusting the Ns field */
                use_regidx = strchr(opcode_reginfo[use_opcode], 's') -
                    opcode_reginfo[use_opcode];
            }

            /*
             * What's encoded at the N-field is the offset to who's producing
             * the value.  Shift off the LSB which indicates odd/even register.
             */
            def_idx = i - ((packet->insn[i].regno[use_regidx]) >> 1);

            /*
             * Check for a badly encoded N-field which points to an instruction
             * out-of-range
             */
            if ((def_idx < 0) || (def_idx > (packet->num_insns - 1))) {
                g_assert_not_reached();
            }

            /* previous insn is the producer */
            def_opcode = packet->insn[def_idx].opcode;
            dststr = strstr(opcode_wregs[def_opcode], "Rd");
            if (dststr) {
                dststr = strchr(opcode_reginfo[def_opcode], 'd');
            } else {
                dststr = strstr(opcode_wregs[def_opcode], "Rx");
                if (dststr) {
                    dststr = strchr(opcode_reginfo[def_opcode], 'x');
                } else {
                    dststr = strstr(opcode_wregs[def_opcode], "Re");
                    if (dststr) {
                        dststr = strchr(opcode_reginfo[def_opcode], 'e');
                    } else {
                        dststr = strstr(opcode_wregs[def_opcode], "Ry");
                        if (dststr) {
                            dststr = strchr(opcode_reginfo[def_opcode], 'y');
                        } else {
                            g_assert_not_reached();
                        }
                    }
                }
            }
            g_assert(dststr != NULL);

            /* Now patch up the consumer with the register number */
            packet->insn[i].regno[use_regidx] =
                packet->insn[def_idx].regno[dststr -
                    opcode_reginfo[def_opcode]];
            /*
             * We need to remember who produces this value to later
             * check if it was dynamically cancelled
             */
            packet->insn[i].new_value_producer_slot =
                packet->insn[def_idx].slot;
        }
    }
    return 0;
}

/* Split CJ into a compare and a jump */
static int decode_split_cmpjump(packet_t *pkt)
{
    int last, i;
    int numinsns = pkt->num_insns;

    /*
     * First, split all compare-jumps.
     * The compare is sent to the end as a new instruction.
     * Do it this way so we don't reorder dual jumps. Those need to stay in
     * original order.
     */
    for (i = 0; i < numinsns; i++) {
        /* It's a cmp-jump */
        if (GET_ATTRIB(pkt->insn[i].opcode, A_NEWCMPJUMP)) {
            last = pkt->num_insns;
            pkt->insn[last] = pkt->insn[i];    /* copy the instruction */
            pkt->insn[last].part1 = 1;    /* last instruction does the CMP */
            pkt->insn[i].part1 = 0;    /* existing instruction does the JUMP */
        pkt->num_insns++;
        }
    }

    /* Now re-shuffle all the compares back to the beginning */
    for (i = 0; i < pkt->num_insns; i++) {
        if (pkt->insn[i].part1) {
            decode_send_insn_to(pkt, i, 0);
        }
    }
    return 0;
}

static inline int decode_opcode_can_jump(int opcode)
{
    if ((GET_ATTRIB(opcode, A_JUMP)) ||
        (GET_ATTRIB(opcode, A_CALL)) ||
        (opcode == J2_trap0)) {
        /* Exception to A_JUMP attribute */
        if (opcode == J4_hintjumpr) {
            return 0;
        }
        return 1;
    }

    return 0;
}

static inline int decode_opcode_ends_loop(int opcode)
{
    return GET_ATTRIB(opcode, A_HWLOOP0_END) ||
           GET_ATTRIB(opcode, A_HWLOOP1_END);
}

/* Set the is_* fields in each instruction */
static int decode_set_insn_attr_fields(packet_t *pkt)
{
    int i;
    int numinsns = pkt->num_insns;
    uint16_t opcode;
    int canjump;

    pkt->pkt_has_cof = 0;
    pkt->pkt_has_endloop = 0;
    pkt->pkt_has_dczeroa = 0;

    for (i = 0; i < numinsns; i++) {
        opcode = pkt->insn[i].opcode;
        if (pkt->insn[i].part1) {
            continue;    /* Skip compare of cmp-jumps */
        }

        if (GET_ATTRIB(opcode, A_DCZEROA)) {
            pkt->pkt_has_dczeroa = 1;
        }

        if (GET_ATTRIB(opcode, A_STORE)) {
            if (pkt->insn[i].slot == 0) {
                pkt->pkt_has_store_s0 = 1;
            } else {
                pkt->pkt_has_store_s1 = 1;
            }
        }

        canjump = decode_opcode_can_jump(opcode);
        pkt->pkt_has_cof |= canjump;

        pkt->insn[i].is_endloop = decode_opcode_ends_loop(opcode);

        pkt->pkt_has_endloop |= pkt->insn[i].is_endloop;

        pkt->pkt_has_cof |= pkt->pkt_has_endloop;
    }

    return 0;
}

/*
 * Shuffle for execution
 * Move stores to end (in same order as encoding)
 * Move compares to beginning (for use by .new insns)
 */
static int decode_shuffle_for_execution(packet_t *packet)
{
    int changed = 0;
    int i;
    int flag;    /* flag means we've seen a non-memory instruction */
    int n_mems;
    int last_insn = packet->num_insns - 1;

    /*
     * Skip end loops, somehow an end loop is getting in and messing
     * up the order
     */
    if (decode_opcode_ends_loop(packet->insn[last_insn].opcode)) {
        last_insn--;
    }

    do {
        changed = 0;
        /*
         * Stores go last, must not reorder.
         * Cannot shuffle stores past loads, either.
         * Iterate backwards.  If we see a non-memory instruction,
         * then a store, shuffle the store to the front.  Don't shuffle
         *  stores wrt each other or a load.
         */
        for (flag = n_mems = 0, i = last_insn; i >= 0; i--) {
            int opcode = packet->insn[i].opcode;

            if (flag && GET_ATTRIB(opcode, A_STORE)) {
                decode_send_insn_to(packet, i, last_insn - n_mems);
                n_mems++;
                changed = 1;
            } else if (GET_ATTRIB(opcode, A_STORE)) {
                n_mems++;
            } else if (GET_ATTRIB(opcode, A_LOAD)) {
                /*
                 * Don't set flag, since we don't want to shuffle a
                 * store pasta load
                 */
                n_mems++;
            } else if (GET_ATTRIB(opcode, A_DOTNEWVALUE)) {
                /*
                 * Don't set flag, since we don't want to shuffle past
                 * a .new value
                 */
            } else {
                flag = 1;
            }
        }

        if (changed) {
            continue;
        }
        /* Compares go first, may be reordered wrt each other */
        for (flag = 0, i = 0; i < last_insn + 1; i++) {
            int opcode = packet->insn[i].opcode;

            if ((strstr(opcode_wregs[opcode], "Pd4") ||
                 strstr(opcode_wregs[opcode], "Pe4")) &&
                GET_ATTRIB(opcode, A_STORE) == 0) {
                /* This should be a compare (not a store conditional) */
                if (flag) {
                    decode_send_insn_to(packet, i, 0);
                    changed = 1;
                    continue;
                }
            } else if (GET_ATTRIB(opcode, A_IMPLICIT_WRITES_P3) &&
                       !decode_opcode_ends_loop(packet->insn[i].opcode)) {
                /*
                 * spNloop instruction
                 * Don't reorder endloops; they are not valid for .new uses,
                 * and we want to match HW
                 */
                if (flag) {
                    decode_send_insn_to(packet, i, 0);
                    changed = 1;
                    continue;
                }
            } else if (GET_ATTRIB(opcode, A_IMPLICIT_WRITES_P0) &&
                       !GET_ATTRIB(opcode, A_NEWCMPJUMP)) {
                if (flag) {
                    decode_send_insn_to(packet, i, 0);
                    changed = 1;
                    continue;
                }
            } else {
                flag = 1;
            }
        }
        if (changed) {
            continue;
        }
    } while (changed);

    /*
     * If we have a .new register compare/branch, move that to the very
     * very end, past stores
     */
    for (i = 0; i < last_insn; i++) {
        if (GET_ATTRIB(packet->insn[i].opcode, A_DOTNEWVALUE)) {
            decode_send_insn_to(packet, i, last_insn);
            break;
        }
    }

    return 0;
}

static int
apply_extender(packet_t *pkt, int i, uint32_t extender)
{
    int immed_num;
    uint32_t base_immed;

    immed_num = opcode_which_immediate_is_extended(pkt->insn[i].opcode);
    base_immed = pkt->insn[i].immed[immed_num];

    pkt->insn[i].immed[immed_num] = extender | fZXTN(6, 32, base_immed);
    return 0;
}

static int decode_apply_extenders(packet_t *packet)
{
    int i;
    for (i = 0; i < packet->num_insns; i++) {
        if (GET_ATTRIB(packet->insn[i].opcode, A_IT_EXTENDER)) {
            packet->insn[i + 1].extension_valid = 1;
            apply_extender(packet, i + 1, packet->insn[i].immed[0]);
        }
    }
    return 0;
}

static int decode_remove_extenders(packet_t *packet)
{
    int i, j;
    for (i = 0; i < packet->num_insns; i++) {
        if (GET_ATTRIB(packet->insn[i].opcode, A_IT_EXTENDER)) {
            for (j = i;
                (j < packet->num_insns - 1) && (j < INSTRUCTIONS_MAX - 1);
                j++) {
                packet->insn[j] = packet->insn[j + 1];
            }
            packet->num_insns--;
        }
    }
    return 0;
}

static const char *
get_valid_slot_str(const packet_t *pkt, unsigned int slot)
{
    return find_iclass_slots(pkt->insn[slot].opcode,
                             pkt->insn[slot].iclass);
}

#include "q6v_decode.c"

packet_t *decode_this(int max_words, uint32_t *words, packet_t *decode_pkt)
{
    int ret;
    ret = do_decode_packet(max_words, words, decode_pkt);
    if (ret <= 0) {
        /* ERROR or BAD PARSE */
        return NULL;
    }
    return decode_pkt;
}

/* Used for "-d in_asm" logging */
int disassemble_hexagon(uint32_t *words, int nwords, char *buf, int bufsize)
{
    packet_t pkt;

    if (decode_this(nwords, words, &pkt)) {
        snprint_a_pkt(buf, bufsize, &pkt);
        return pkt.encod_pkt_size_in_bytes;
    } else {
        snprintf(buf, bufsize, "<invalid>");
        return 0;
    }
}
