/*
 * RISC-V translation routines for the Zvkg Extension.
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

static bool vgmul_check(DisasContext *s, arg_rmr *a)
{
    return s->cfg_ptr->ext_zvkg == true &&
            vext_check_isa_ill(s) &&
            require_rvv(s) &&
            MAXSZ(s) >= (128 / 8) && /* EGW in bytes */
            vext_check_ss(s, a->rd, a->rs2, a->vm) &&
            s->vstart % 4 == 0 && s->sew == MO_32;
}

GEN_V_UNMASKED_TRANS(vgmul_vv, vgmul_check)

static bool vghsh_check(DisasContext *s, arg_rmrr *a)
{
    return s->cfg_ptr->ext_zvkg == true &&
            opivv_check(s, a) &&
            MAXSZ(s) >= (128 / 8) && /* EGW in bytes */
            s->vstart % 4 == 0 && s->sew == MO_32;
}

GEN_VV_UNMASKED_TRANS(vghsh_vv, vghsh_check, 4)
