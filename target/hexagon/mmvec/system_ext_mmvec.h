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

#ifndef HEXAGON_SYSTEM_EXT_MMVEC_H
#define HEXAGON_SYSTEM_EXT_MMVEC_H

void mem_load_vector(CPUHexagonState *env, target_ulong vaddr,
                     int size, uint8_t *data);
void mem_gather_store(CPUHexagonState *env, target_ulong vaddr,
                      int slot, uint8_t *data);
void mem_store_vector(CPUHexagonState *env, target_ulong vaddr,
                      int slot, int size,
                      uint8_t *data, uint8_t *mask, bool invert);
void mem_vector_scatter_init(CPUHexagonState *env, int slot,
                             target_ulong base_vaddr, int length,
                             int element_size);
void mem_vector_gather_init(CPUHexagonState *env, int slot,
                            target_ulong base_vaddr, int length,
                            int element_size);

#endif
