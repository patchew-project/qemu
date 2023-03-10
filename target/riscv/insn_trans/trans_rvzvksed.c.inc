/*
 * RISC-V translation routines for the Zvksed Extension.
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

#define ZVKSED_EGS 4

static bool zvksed_check(DisasContext *s)
{
    return s->cfg_ptr->ext_zvksed == true &&
           require_rvv(s) &&
           vext_check_isa_ill(s) &&
           MAXSZ(s) >= (128 / 8) && /* EGW in bytes */
           s->vstart % ZVKSED_EGS == 0 &&
           s->sew == MO_32;
}

static bool vsm4k_vi_check(DisasContext *s, arg_rmrr *a)
{
    return zvksed_check(s) &&
           require_align(a->rd, s->lmul) &&
           require_align(a->rs2, s->lmul);
}

GEN_VI_UNMASKED_TRANS(vsm4k_vi, vsm4k_vi_check, ZVKSED_EGS)

static bool vsm4r_vv_check(DisasContext *s, arg_rmr *a)
{
    return zvksed_check(s) &&
           require_align(a->rd, s->lmul) &&
           require_align(a->rs2, s->lmul);
}

GEN_V_UNMASKED_TRANS(vsm4r_vv, vsm4r_vv_check)

static bool vsm4r_vs_check(DisasContext *s, arg_rmr *a)
{
    return zvksed_check(s) &&
           !is_overlapped(a->rd, 1 << MAX(s->lmul, 0), a->rs2, 1) &&
           require_align(a->rd, s->lmul);
}

GEN_V_UNMASKED_TRANS(vsm4r_vs, vsm4r_vs_check)
