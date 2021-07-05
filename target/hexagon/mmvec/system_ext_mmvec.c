/*
 *  Copyright(c) 2019-2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
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
#include "qemu.h"
#include "cpu.h"
#include "mmvec/system_ext_mmvec.h"

void mem_gather_store(CPUHexagonState *env, target_ulong vaddr,
                      int slot, uint8_t *data)
{
    size_t size = sizeof(MMVector);

    /*
     * If it's a gather store update store data from temporary register
     * and clear flag
     */
    memcpy(data, &env->tmp_VRegs[0].ub[0], size);
    env->VRegs_updated_tmp = 0;
    env->gather_issued = false;

    env->vstore_pending[slot] = 1;
    env->vstore[slot].va   = vaddr;
    env->vstore[slot].size = size;
    memcpy(&env->vstore[slot].data.ub[0], data, size);

    /* On a gather store, overwrite the store mask to emulate dropped gathers */
    memcpy(&env->vstore[slot].mask.ub[0], &env->vtcm_log.mask.ub[0], size);
}

void mem_store_vector(CPUHexagonState *env, target_ulong vaddr, int slot,
                      int size, uint8_t *data, uint8_t *mask, bool invert)
{
    if (!size) {
        return;
    }

    if (env->is_gather_store_insn) {
        mem_gather_store(env, vaddr, slot, data);
        return;
    }

    env->vstore_pending[slot] = 1;
    env->vstore[slot].va   = vaddr;
    env->vstore[slot].size = size;
    memcpy(&env->vstore[slot].data.ub[0], data, size);
    if (!mask) {
        memset(&env->vstore[slot].mask.ub[0], invert ? 0 : -1, size);
    } else if (invert) {
        for (int i = 0; i < size; i++) {
            env->vstore[slot].mask.ub[i] = !mask[i];
        }
    } else {
        memcpy(&env->vstore[slot].mask.ub[0], mask, size);
    }
}

void mem_load_vector(CPUHexagonState *env, target_ulong vaddr,
                     int size, uint8_t *data)
{
    for (int i = 0; i < size; i++) {
        get_user_u8(data[i], vaddr);
        vaddr++;
    }
}

void mem_vector_scatter_init(CPUHexagonState *env, int slot,
                             target_ulong base_vaddr,
                             int length, int element_size)
{
    int i;

    for (i = 0; i < sizeof(MMVector); i++) {
        env->vtcm_log.data.ub[i] = 0;
        env->vtcm_log.mask.ub[i] = 0;
    }

    env->vtcm_pending = true;
    env->vtcm_log.op = false;
    env->vtcm_log.op_size = 0;
    env->vtcm_log.size = sizeof(MMVector);
}

void mem_vector_gather_init(CPUHexagonState *env, int slot,
                            target_ulong base_vaddr,
                            int length, int element_size)
{
    int i;

    for (i = 0; i < sizeof(MMVector); i++) {
        env->vtcm_log.data.ub[i] = 0;
        env->vtcm_log.mask.ub[i] = 0;
        env->vtcm_log.va[i] = 0;
        env->tmp_VRegs[0].ub[i] = 0;
    }
    env->vtcm_log.op = false;
    env->vtcm_log.op_size = 0;

    /*
     * Temp reg gets updated
     * This allows store .new to grab the correct result
     */
    env->VRegs_updated_tmp = 1;
    env->gather_issued = true;
}
