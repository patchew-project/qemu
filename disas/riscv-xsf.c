/*
 * QEMU RISC-V Disassembler for xsf (SiFive vendor extensions).
 *
 * Copyright (c) 2023 SiFive, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "disas/riscv.h"
#include "disas/riscv-xsf.h"

typedef enum {
    /* 0 is reserved for rv_op_illegal. */
    xsf_op_sf_vqmaccu_4x8x4 = 1,
    xsf_op_sf_vqmacc_4x8x4 = 2,
    xsf_op_sf_vqmaccus_4x8x4 = 3,
    xsf_op_sf_vqmaccsu_4x8x4 = 4,
    xsf_op_sf_vqmaccu_2x8x2 = 5,
    xsf_op_sf_vqmacc_2x8x2 = 6,
    xsf_op_sf_vqmaccus_2x8x2 = 7,
    xsf_op_sf_vqmaccsu_2x8x2 = 8,
} rv_xsf_op;

const rv_opcode_data xsf_opcode_data[] = {
    { "illegal", rv_codec_illegal, rv_fmt_none, NULL, 0, 0, 0 },
    { "sf.vqmaccu.4x8x4", rv_codec_v_r, rv_fmt_vd_vs1_vs2, NULL, 0, 0, 0 },
    { "sf.vqmacc.4x8x4", rv_codec_v_r, rv_fmt_vd_vs1_vs2, NULL, 0, 0, 0 },
    { "sf.vqmaccus.4x8x4", rv_codec_v_r, rv_fmt_vd_vs1_vs2, NULL, 0, 0, 0 },
    { "sf.vqmaccsu.4x8x4", rv_codec_v_r, rv_fmt_vd_vs1_vs2, NULL, 0, 0, 0 },
    { "sf.vqmaccu.2x8x2", rv_codec_v_r, rv_fmt_vd_vs1_vs2, NULL, 0, 0, 0 },
    { "sf.vqmacc.2x8x2", rv_codec_v_r, rv_fmt_vd_vs1_vs2, NULL, 0, 0, 0 },
    { "sf.vqmaccus.2x8x2", rv_codec_v_r, rv_fmt_vd_vs1_vs2, NULL, 0, 0, 0 },
    { "sf.vqmaccsu.2x8x2", rv_codec_v_r, rv_fmt_vd_vs1_vs2, NULL, 0, 0, 0 },
};

void decode_xsf(rv_decode *dec, rv_isa isa)
{
    rv_inst inst = dec->inst;
    rv_opcode op = rv_op_illegal;

    switch ((inst >> 0) & 0b1111111) {
    case 0b1011011:
        switch ((inst >> 12) & 0b111) {
        case 0b010:
            switch ((inst >> 26) & 0b111111) {
            case 60:
                op = xsf_op_sf_vqmaccu_4x8x4;
                break;
            case 61:
                op = xsf_op_sf_vqmacc_4x8x4;
                break;
            case 62:
                op = xsf_op_sf_vqmaccus_4x8x4;
                break;
            case 63:
                op = xsf_op_sf_vqmaccsu_4x8x4;
                break;
            case 44:
                op = xsf_op_sf_vqmaccu_2x8x2;
                break;
            case 45:
                op = xsf_op_sf_vqmacc_2x8x2;
                break;
            case 46:
                op = xsf_op_sf_vqmaccus_2x8x2;
                break;
            case 47:
                op = xsf_op_sf_vqmaccsu_2x8x2;
                break;
            }
            break;
        }
        break;
    }

    dec->op = op;
}
