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
#include "opcodes.h"
#include "printinsn.h"
#include "insn.h"
#include "reg_fields.h"
#include "internal.h"

static const char *sreg2str(unsigned int reg)
{
    if (reg < TOTAL_PER_THREAD_REGS) {
        return hexagon_regnames[reg];
    } else {
        return "???";
    }
}

static const char *creg2str(unsigned int reg)
{
    return sreg2str(reg + HEX_REG_SA0);
}

static void snprintinsn(char *buf, int n, Insn * insn)
{
    switch (insn->opcode) {
#define DEF_VECX_PRINTINFO(TAG, FMT, ...) DEF_PRINTINFO(TAG, FMT, __VA_ARGS__)
#define DEF_PRINTINFO(TAG, FMT, ...) \
    case TAG: \
        snprintf(buf, n, FMT, __VA_ARGS__);\
        break;
#include "printinsn_generated.h"
#undef DEF_VECX_PRINTINFO
#undef DEF_PRINTINFO
    }
}

void snprint_a_pkt_disas(char *buf, int n, Packet *pkt, uint32_t *words,
                         target_ulong pc)
{
    char tmpbuf[128];
    buf[0] = '\0';
    bool has_endloop0 = false;
    bool has_endloop1 = false;
    bool has_endloop01 = false;

    for (int i = 0; i < pkt->num_insns; i++) {
        if (pkt->insn[i].part1) {
            continue;
        }

        /* We'll print the endloop's at the end of the packet */
        if (pkt->insn[i].opcode == J2_endloop0) {
            has_endloop0 = true;
            continue;
        }
        if (pkt->insn[i].opcode == J2_endloop1) {
            has_endloop1 = true;
            continue;
        }
        if (pkt->insn[i].opcode == J2_endloop01) {
            has_endloop01 = true;
            continue;
        }

        snprintf(tmpbuf, 127, "0x" TARGET_FMT_lx "\t", words[i]);
        strncat(buf, tmpbuf, n);

        if (i == 0) {
            strncat(buf, "{", n);
        }

        snprintinsn(tmpbuf, 127, &(pkt->insn[i]));
        strncat(buf, "\t", n);
        strncat(buf, tmpbuf, n);

        if (i < pkt->num_insns - 1) {
            /*
             * Subinstructions are two instructions encoded
             * in the same word. Print them on the same line.
             */
            if (GET_ATTRIB(pkt->insn[i].opcode, A_SUBINSN)) {
                strncat(buf, "; ", n);
                snprintinsn(tmpbuf, 127, &(pkt->insn[i + 1]));
                strncat(buf, tmpbuf, n);
                i++;
            } else if (pkt->insn[i + 1].opcode != J2_endloop0 &&
                       pkt->insn[i + 1].opcode != J2_endloop1 &&
                       pkt->insn[i + 1].opcode != J2_endloop01) {
                pc += 4;
                snprintf(tmpbuf, 127, "\n0x" TARGET_FMT_lx ":  ", pc);
                strncat(buf, tmpbuf, n);
            }
        }
    }
    strncat(buf, " }", n);
    if (has_endloop0) {
        strncat(buf, "  :endloop0", n);
    }
    if (has_endloop1) {
        strncat(buf, "  :endloop1", n);
    }
    if (has_endloop01) {
        strncat(buf, "  :endloop01", n);
    }
    strncat(buf, "\n", n);
}

void snprint_a_pkt_debug(char *buf, int n, Packet *pkt)
{
    char tmpbuf[128];
    buf[0] = '\0';
    int slot, opcode;

    if (pkt->num_insns > 1) {
        strncat(buf, "\n{\n", n);
    }

    for (int i = 0; i < pkt->num_insns; i++) {
        if (pkt->insn[i].part1) {
            continue;
        }
        snprintinsn(tmpbuf, 127, &(pkt->insn[i]));
        strncat(buf, "\t", n);
        strncat(buf, tmpbuf, n);

        if (GET_ATTRIB(pkt->insn[i].opcode, A_SUBINSN)) {
            strncat(buf, " //subinsn", n);
        }
        if (pkt->insn[i].extension_valid) {
            strncat(buf, " //constant extended", n);
        }
        slot = pkt->insn[i].slot;
        opcode = pkt->insn[i].opcode;
        snprintf(tmpbuf, 127, " //slot=%d:tag=%s", slot, opcode_names[opcode]);
        strncat(buf, tmpbuf, n);

        strncat(buf, "\n", n);
    }
    if (pkt->num_insns > 1) {
        strncat(buf, "}\n", n);
    }
}
