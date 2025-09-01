/*
 * RISC-V translation routines for the Zvqdotq vector dot-product extension
 *
 * Copyright (c) 2025 Max Chou, max.chou@sifive.com
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

static bool vext_zvqdotq_base_check(DisasContext *s)
{
    return s->cfg_ptr->ext_zvqdotq && s->sew == MO_32;
}

static bool vext_vqdotq_opivv_check(DisasContext *s, arg_rmrr *a)
{
    return vext_zvqdotq_base_check(s) && opivv_check(s, a);
}

#define GEN_VQDOTQ_OPIVV_TRANS(NAME, CHECK)              \
static bool trans_##NAME(DisasContext *s, arg_rmrr *a)   \
{                                                        \
    if (CHECK(s, a)) {                                   \
        return opivv_trans(a->rd, a->rs1, a->rs2, a->vm, \
                           gen_helper_##NAME, s);        \
    }                                                    \
    return false;                                        \
}

GEN_VQDOTQ_OPIVV_TRANS(vqdot_vv, vext_vqdotq_opivv_check)
GEN_VQDOTQ_OPIVV_TRANS(vqdotu_vv, vext_vqdotq_opivv_check)
GEN_VQDOTQ_OPIVV_TRANS(vqdotsu_vv, vext_vqdotq_opivv_check)

static bool vext_vqdotq_opivx_check(DisasContext *s, arg_rmrr *a)
{
    return vext_zvqdotq_base_check(s) && opivx_check(s, a);
}

#define GEN_VQDOTQ_OPIVX_TRANS(NAME, CHECK)              \
static bool trans_##NAME(DisasContext *s, arg_rmrr *a)   \
{                                                        \
    if (CHECK(s, a)) {                                   \
        return opivx_trans(a->rd, a->rs1, a->rs2, a->vm, \
                           gen_helper_##NAME, s);        \
    }                                                    \
    return false;                                        \
}

GEN_VQDOTQ_OPIVX_TRANS(vqdot_vx, vext_vqdotq_opivx_check)
GEN_VQDOTQ_OPIVX_TRANS(vqdotu_vx, vext_vqdotq_opivx_check)
GEN_VQDOTQ_OPIVX_TRANS(vqdotsu_vx, vext_vqdotq_opivx_check)
GEN_VQDOTQ_OPIVX_TRANS(vqdotus_vx, vext_vqdotq_opivx_check)
