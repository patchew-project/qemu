/*
 * Disassembler code for ARC.
 *
 * Copyright 2020 Synopsys Inc.
 * Contributed by Claudiu Zissulescu <claziss@synopsys.com>
 *
 * QEMU ARCv2 Disassembler.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "disas/dis-asm.h"
#include "target/arc/arc-common.h"
#include "target/arc/decoder.h"
#include "target/arc/regs.h"

/* Register names. */

static const char * const regnames[64] = {
    "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
    "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
    "r24", "r25", "r26", "fp", "sp", "ilink", "r30", "blink",

    "r32", "r33", "r34", "r35", "r36", "r37", "r38", "r39",
    "r40", "r41", "r42", "r43", "r44", "r45", "r46", "r47",
    "r48", "r49", "r50", "r51", "r52", "r53", "r54", "r55",
    "r56", "r57", "r58", "r59", "lp_count", "rezerved", "LIMM", "pcl"
};

#define ARRANGE_ENDIAN(info, buf)                                       \
    (info->endian == BFD_ENDIAN_LITTLE ? bfd_getm32(bfd_getl32(buf))    \
     : bfd_getb32(buf))

/*
 * Helper function to convert middle-endian data to something more
 * meaningful.
 */

static bfd_vma bfd_getm32(unsigned int data)
{
    bfd_vma value = 0;

    value  = (data & 0x0000ffff) << 16;
    value |= (data & 0xffff0000) >> 16;
    return value;
}

/* Helper for printing instruction flags. */

static bfd_boolean special_flag_p(const char *opname, const char *flgname)
{
    const struct arc_flag_special *flg_spec;
    unsigned i, j, flgidx;

    for (i = 0; i < arc_num_flag_special; ++i) {
        flg_spec = &arc_flag_special_cases[i];

        if (strcmp(opname, flg_spec->name) != 0) {
            continue;
        }

        /* Found potential special case instruction. */
        for (j = 0; ; ++j) {
            flgidx = flg_spec->flags[j];
            if (flgidx == 0) {
                break; /* End of the array. */
            }

            if (strcmp(flgname, arc_flag_operands[flgidx].name) == 0) {
                return TRUE;
            }
        }
    }
    return FALSE;
}

/* Print instruction flags. */

static void print_flags(const struct arc_opcode *opcode,
                        uint64_t insn,
                        struct disassemble_info *info)
{
    const unsigned char *flgidx;
    unsigned int value;

    /* Now extract and print the flags. */
    for (flgidx = opcode->flags; *flgidx; flgidx++) {
        /* Get a valid flag class. */
        const struct arc_flag_class *cl_flags = &arc_flag_classes[*flgidx];
        const unsigned *flgopridx;

        /* Check first the extensions. Not supported yet. */
        if (cl_flags->flag_class & F_CLASS_EXTEND) {
            value = insn & 0x1F;
        }

        for (flgopridx = cl_flags->flags; *flgopridx; ++flgopridx) {
            const struct arc_flag_operand *flg_operand =
                &arc_flag_operands[*flgopridx];

            /* Implicit flags are only used for the insn decoder. */
            if (cl_flags->flag_class & F_CLASS_IMPLICIT) {
                continue;
            }

            if (!flg_operand->favail) {
                continue;
            }

            value = (insn >> flg_operand->shift) &
                    ((1 << flg_operand->bits) - 1);
            if (value == flg_operand->code) {
                /* FIXME!: print correctly nt/t flag. */
                if (!special_flag_p(opcode->name, flg_operand->name)) {
                    (*info->fprintf_func)(info->stream, ".");
                }
                (*info->fprintf_func)(info->stream, "%s", flg_operand->name);
            }
        }
    }
}

/*
 * When dealing with auxiliary registers, output the proper name if we
 * have it.
 */

static const char *get_auxreg(const struct arc_opcode *opcode,
                              int value,
                              unsigned isa_mask)
{
    unsigned int i;
    const struct arc_aux_reg_detail *auxr = &arc_aux_regs_detail[0];

    if (opcode->insn_class != AUXREG) {
        return NULL;
    }

    for (i = 0; i < ARRAY_SIZE(arc_aux_regs); i++, auxr++) {
        if (!(auxr->cpu & isa_mask)) {
            continue;
        }

        if (auxr->subclass != NONE) {
            return NULL;
        }

        if (auxr->address == value) {
            return auxr->name;
        }
    }
    return NULL;
}

/* Print the operands of an instruction. */

static void print_operands(const struct arc_opcode *opcode,
                           bfd_vma memaddr,
                           uint64_t insn,
                           uint32_t isa_mask,
                           insn_t *pinsn,
                           struct disassemble_info *info)
{
    bfd_boolean need_comma  = FALSE;
    bfd_boolean open_braket = FALSE;
    int value, vpcl = 0;
    bfd_boolean rpcl = FALSE, rset = FALSE;
    const unsigned char *opidx;
    int i;

    for (i = 0, opidx = opcode->operands; *opidx; opidx++) {
        const struct arc_operand *operand = &arc_operands[*opidx];

        if (open_braket && (operand->flags & ARC_OPERAND_BRAKET)) {
            (*info->fprintf_func)(info->stream, "]");
            open_braket = FALSE;
            continue;
        }

        /* Only take input from real operands. */
        if (ARC_OPERAND_IS_FAKE(operand)) {
            continue;
        }

        if (need_comma) {
            (*info->fprintf_func)(info->stream, ",");
        }

        if (!open_braket && (operand->flags & ARC_OPERAND_BRAKET)) {
            (*info->fprintf_func)(info->stream, "[");
            open_braket = TRUE;
            need_comma  = FALSE;
            continue;
        }

        need_comma = TRUE;

        /* Get the decoded */
        value = pinsn->operands[i++].value;

        if ((operand->flags & ARC_OPERAND_IGNORE) &&
            (operand->flags & ARC_OPERAND_IR) &&
            value == -1) {
            need_comma = FALSE;
            continue;
        }

        if (operand->flags & ARC_OPERAND_PCREL) {
            rpcl = TRUE;
            vpcl = value;
            rset = TRUE;

            info->target = (bfd_vma) (memaddr & ~3) + value;
        } else if (!(operand->flags & ARC_OPERAND_IR)) {
            vpcl = value;
            rset = TRUE;
        }

        /* Print the operand as directed by the flags. */
        if (operand->flags & ARC_OPERAND_IR) {
            const char *rname;

            assert(value >= 0 && value < 64);
            rname = regnames[value];
            (*info->fprintf_func)(info->stream, "%s", rname);
            if (operand->flags & ARC_OPERAND_TRUNCATE) {
                /* Make sure we print only legal register pairs. */
                if ((value & 0x01) == 0) {
                    rname = regnames[value + 1];
                }
                (*info->fprintf_func)(info->stream, "%s", rname);
            }
            if (value == 63) {
                rpcl = TRUE;
            } else {
                rpcl = FALSE;
            }
        } else if (operand->flags & ARC_OPERAND_LIMM) {
            value = pinsn->limm;
            const char *rname = get_auxreg(opcode, value, isa_mask);

            if (rname && open_braket) {
                (*info->fprintf_func)(info->stream, "%s", rname);
            } else {
                (*info->fprintf_func)(info->stream, "%#x", value);
            }
        } else if (operand->flags & ARC_OPERAND_SIGNED) {
            const char *rname = get_auxreg(opcode, value, isa_mask);
            if (rname && open_braket) {
                (*info->fprintf_func)(info->stream, "%s", rname);
            } else {
                (*info->fprintf_func)(info->stream, "%d", value);
            }
        } else {
            if (operand->flags & ARC_OPERAND_TRUNCATE   &&
                !(operand->flags & ARC_OPERAND_ALIGNED32) &&
                !(operand->flags & ARC_OPERAND_ALIGNED16) &&
                 value >= 0 && value <= 14) {
                /* Leave/Enter mnemonics. */
                switch (value) {
                case 0:
                    need_comma = FALSE;
                    break;
                case 1:
                    (*info->fprintf_func)(info->stream, "r13");
                    break;
                default:
                    (*info->fprintf_func)(info->stream, "r13-%s",
                            regnames[13 + value - 1]);
                    break;
                }
                rpcl = FALSE;
                rset = FALSE;
            } else {
                const char *rname = get_auxreg(opcode, value, isa_mask);
                if (rname && open_braket) {
                    (*info->fprintf_func)(info->stream, "%s", rname);
                } else {
                    (*info->fprintf_func)(info->stream, "%#x", value);
                }
            }
        }
    }

    /* Pretty print extra info for pc-relative operands. */
    if (rpcl && rset) {
        if (info->flags & INSN_HAS_RELOC) {
            /*
             * If the instruction has a reloc associated with it, then
             * the offset field in the instruction will actually be
             * the addend for the reloc.  (We are using REL type
             * relocs).  In such cases, we can ignore the pc when
             * computing addresses, since the addend is not currently
             * pc-relative.
             */
            memaddr = 0;
        }

        (*info->fprintf_func)(info->stream, "\t;");
        (*info->print_address_func)((memaddr & ~3) + vpcl, info);
    }
}

/* Select the proper instructions set for the given architecture. */

static int arc_read_mem(bfd_vma memaddr,
                        uint64_t *insn,
                        uint32_t *isa_mask,
                        struct disassemble_info *info)
{
    bfd_byte buffer[8];
    unsigned int highbyte, lowbyte;
    int status;
    int insn_len = 0;

    highbyte = ((info->endian == BFD_ENDIAN_LITTLE) ? 1 : 0);
    lowbyte  = ((info->endian == BFD_ENDIAN_LITTLE) ? 0 : 1);

    switch (info->mach) {
    case bfd_mach_arc_arc700:
        *isa_mask = ARC_OPCODE_ARC700;
        break;

    case bfd_mach_arc_arc601:
    case bfd_mach_arc_arc600:
        *isa_mask = ARC_OPCODE_ARC600;
        break;

    case bfd_mach_arc_arcv2em:
    case bfd_mach_arc_arcv2:
        *isa_mask = ARC_OPCODE_ARCv2EM;
        break;
    case bfd_mach_arc_arcv2hs:
        *isa_mask = ARC_OPCODE_ARCv2HS;
        break;
    default:
        *isa_mask = ARC_OPCODE_ARCv2EM;
        break;
    }

    info->bytes_per_line  = 8;
    info->bytes_per_chunk = 2;
    info->display_endian = info->endian;

    /* Read the insn into a host word. */
    status = (*info->read_memory_func)(memaddr, buffer, 2, info);

    if (status != 0) {
        (*info->memory_error_func)(status, memaddr, info);
        return -1;
    }

    insn_len = arc_insn_length((buffer[highbyte] << 8 |
                buffer[lowbyte]), *isa_mask);

    switch (insn_len) {
    case 2:
        *insn = (buffer[highbyte] << 8) | buffer[lowbyte];
        break;

    case 4:
        /* This is a long instruction: Read the remaning 2 bytes. */
        status = (*info->read_memory_func)(memaddr + 2, &buffer[2], 2, info);
        if (status != 0) {
            (*info->memory_error_func)(status, memaddr + 2, info);
            return -1;
        }
        *insn = (uint64_t) ARRANGE_ENDIAN(info, buffer);
        break;

    case 6:
        status = (*info->read_memory_func)(memaddr + 2, &buffer[2], 4, info);
        if (status != 0) {
            (*info->memory_error_func)(status, memaddr + 2, info);
            return -1;
        }
        *insn  = (uint64_t) ARRANGE_ENDIAN(info, &buffer[2]);
        *insn |= ((uint64_t) buffer[highbyte] << 40) |
                 ((uint64_t) buffer[lowbyte]  << 32);
        break;

    case 8:
        status = (*info->read_memory_func)(memaddr + 2, &buffer[2], 6, info);
        if (status != 0) {
            (*info->memory_error_func)(status, memaddr + 2, info);
            return -1;
        }
        *insn = ((((uint64_t) ARRANGE_ENDIAN(info, buffer)) << 32) |
                  ((uint64_t) ARRANGE_ENDIAN(info, &buffer[4])));
        break;

    default:
        /* There is no instruction whose length is not 2, 4, 6, or 8. */
        g_assert_not_reached();
    }
    return insn_len;
}

/* Disassembler main entry function. */

int print_insn_arc(bfd_vma memaddr, struct disassemble_info *info)
{
    const struct arc_opcode *opcode = NULL;
    int insn_len = -1;
    uint64_t insn;
    uint32_t isa_mask;
    insn_t dis_insn;

    insn_len = arc_read_mem(memaddr, &insn, &isa_mask, info);

    if (insn_len < 2) {
        return -1;
    }

    opcode = arc_find_format(&dis_insn, insn, insn_len, isa_mask);

    /* If limm is required, read it. */
    if (dis_insn.limm_p) {
        bfd_byte buffer[4];
        int status = (*info->read_memory_func)(memaddr + insn_len, buffer,
                                               4, info);
        if (status != 0) {
            return -1;
        }
        dis_insn.limm = ARRANGE_ENDIAN(info, buffer);
        insn_len += 4;
    }

    /* Print the mnemonic. */
    (*info->fprintf_func)(info->stream, "%s", opcode->name);

    print_flags(opcode, insn, info);

    if (opcode->operands[0] != 0) {
        (*info->fprintf_func)(info->stream, "\t");
    }

    /* Now extract and print the operands. */
    print_operands(opcode, memaddr, insn, isa_mask, &dis_insn, info);

    /* Say how many bytes we consumed */
    return insn_len;
}


/*-*-indent-tabs-mode:nil;tab-width:4;indent-line-function:'insert-tab'-*-*/
/* vim: set ts=4 sw=4 et: */
