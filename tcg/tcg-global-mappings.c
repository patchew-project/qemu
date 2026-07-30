/*
 *  Copyright(c) 2024 rev.ng Labs Srl. All Rights Reserved.
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
#include "tcg/tcg-global-mappings.h"
#include "tcg/tcg-op-common.h"
#include "tcg/tcg.h"

void init_cpu_tcg_mappings(cpu_tcg_mapping *mappings, size_t size)
{
    uintptr_t tcg_addr;
    size_t cpu_offset;
    const char *name;
    cpu_tcg_mapping m;

    /*
     * Paranoid assertion, this should always hold since
     * they're typedef'd to pointers. But you never know!
     */
    g_assert(sizeof(TCGv_i32) == sizeof(TCGv_i64));

    /*
     * Loop over entries in tcg_global_mappings and
     * create the `mapped to` TCGv's.
     */
    for (int i = 0; i < size; ++i) {
        m = mappings[i];

        for (int j = 0; j < m.number_of_elements; ++j) {
            /*
             * Here we are using the fact that
             * sizeof(TCGv_i32) == sizeof(TCGv_i64) == sizeof(TCGv)
             */
            assert(sizeof(TCGv_i32) == sizeof(TCGv_i64));
            tcg_addr = (uintptr_t)m.tcg_var_base_address + j * sizeof(TCGv_i32);
            cpu_offset = m.cpu_var_base_offset + j * m.cpu_var_stride;
            name = m.cpu_var_names[j];

            if (m.cpu_var_size < 8) {
                *(TCGv_i32 *)tcg_addr =
                    tcg_global_mem_new_i32(tcg_env, cpu_offset, name);
            } else {
                *(TCGv_i64 *)tcg_addr =
                    tcg_global_mem_new_i64(tcg_env, cpu_offset, name);
            }
        }
    }
}
