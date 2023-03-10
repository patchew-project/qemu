/*
 * RISC-V translation routines for the Zvksh Extension.
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

static inline bool vsm3_check(DisasContext *s, arg_rmrr *a)
{
    int mult = 1 << MAX(s->lmul, 0);
    return s->cfg_ptr->ext_zvksh == true &&
           require_rvv(s) &&
           vext_check_isa_ill(s) &&
           !is_overlapped(a->rd, mult, a->rs2, mult) &&
           MAXSZ(s) >= (256 / 8) && /* EGW in bytes */
           s->vstart % 8 == 0 &&
           s->sew == MO_32;
}

static inline bool vsm3me_check(DisasContext *s, arg_rmrr *a)
{
    return vsm3_check(s, a) && vext_check_sss(s, a->rd, a->rs1, a->rs2, a->vm);
}

GEN_VV_UNMASKED_TRANS(vsm3me_vv, vsm3me_check, 8)
