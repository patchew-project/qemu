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

#ifndef HEXAGON_INSN_H
#define HEXAGON_INSN_H

#include "cpu.h"
#include "hex_arch_types.h"

#define INSTRUCTIONS_MAX 7    /* 2 pairs + loopend */
#define REG_OPERANDS_MAX 5
#define IMMEDS_MAX 2

struct Instruction;
struct Packet;
struct DisasContext;

typedef void (*semantic_insn_t)(CPUHexagonState *env,
                                struct DisasContext *ctx,
                                struct Instruction *insn,
                                struct Packet *pkt);

struct Instruction {
    semantic_insn_t generate;            /* pointer to genptr routine */
    size1u_t regno[REG_OPERANDS_MAX];    /* reg operands including predicates */
    size2u_t opcode;

    size4u_t iclass:6;
    size4u_t slot:3;
    size4u_t part1:1;        /*
                              * cmp-jumps are split into two insns.
                              * set for the compare and clear for the jump
                              */
    size4u_t extension_valid:1;   /* Has a constant extender attached */
    size4u_t which_extended:1;    /* If has an extender, which immediate */
    size4u_t is_endloop:1;   /* This is an end of loop */
    size4u_t new_value_producer_slot:4;
    size4s_t immed[IMMEDS_MAX];    /* immediate field */
};

typedef struct Instruction insn_t;

struct Packet {
    size2u_t num_insns;
    size2u_t encod_pkt_size_in_bytes;

    /* Pre-decodes about COF */
    size8u_t pkt_has_cof:1;          /* Has any change-of-flow */
    size8u_t pkt_has_endloop:1;

    size8u_t pkt_has_dczeroa:1;

    size8u_t pkt_has_store_s0:1;
    size8u_t pkt_has_store_s1:1;

    insn_t insn[INSTRUCTIONS_MAX];
};

typedef struct Packet packet_t;

#endif
