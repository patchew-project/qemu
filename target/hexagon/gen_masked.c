/*
 *  Copyright(c) 2022 Qualcomm Innovation Center, Inc. All Rights Reserved.
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
#include "tcg/tcg-op.h"
#include "gen_masked.h"

void gen_masked_reg_write(TCGv cur_val, TCGv in_val, TCGv out_val,
    target_ulong reg_mask) {
    TCGv set_bits = tcg_temp_new();
    TCGv cleared_bits = tcg_temp_new();

    /*
     * set_bits = in_val & reg_mask
     * cleared_bits = (~in_val) & reg_mask
     */
    tcg_gen_andi_tl(set_bits, in_val, reg_mask);
    tcg_gen_not_tl(cleared_bits, in_val);
    tcg_gen_andi_tl(cleared_bits, cleared_bits, reg_mask);

    /*
     * result = (reg_cur | set_bits) & (~cleared_bits)
     */
    tcg_gen_not_tl(cleared_bits, cleared_bits);
    tcg_gen_or_tl(set_bits, set_bits, cur_val);
    tcg_gen_and_tl(out_val, set_bits, cleared_bits);

    tcg_temp_free(set_bits);
    tcg_temp_free(cleared_bits);
}
