/*
 * RISC-V translation routines for the Zvkb Extension.
 *
 * Copyright (C) 2023 SiFive, Inc.
 * Written by Codethink Ltd and SiFive.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define GEN_VV_MASKED_TRANS(NAME, CHECK)                 \
static bool trans_##NAME(DisasContext *s, arg_rmrr *a)   \
{                                                        \
    if (CHECK(s, a)) {                                   \
        return opivv_trans(a->rd, a->rs1, a->rs2, a->vm, \
                           gen_helper_##NAME, s);        \
    }                                                    \
    return false;                                        \
}

static bool zvkb_vv_check(DisasContext *s, arg_rmrr *a)
{
    return opivv_check(s, a) && s->cfg_ptr->ext_zvkb == true;
}

static bool vclmul_vv_check(DisasContext *s, arg_rmrr *a)
{
    return zvkb_vv_check(s, a) && s->sew == MO_64;
}

GEN_VV_MASKED_TRANS(vclmul_vv, vclmul_vv_check)
