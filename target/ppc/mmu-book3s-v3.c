/*
 *  PowerPC ISAV3 BookS emulation generic mmu helpers for qemu.
 *
 *  Copyright (c) 2017 Suraj Jitindar Singh, IBM Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "system/memory.h"
#include "cpu.h"
#include "mmu-hash64.h"
#include "mmu-book3s-v3.h"

static bool ppc64_v3_get_pate_from_size(PowerPCCPU *cpu, target_ulong lpid,
                                        ppc_v3_pate_t *entry,
                                        uint64_t table_size)
{
    uint64_t patb = cpu->env.spr[SPR_PTCR] & PTCR_PATB;
    uint64_t entries;

    /* Check if partition table is properly aligned */
    if (patb & (table_size - 1)) {
        return false;
    }

    /* Calculate number of entries */
    entries = table_size / sizeof(*entry);
    if (entries <= lpid) {
        return false;
    }

    /* Grab entry */
    patb += 16 * lpid;
    entry->dw0 = ldq_phys(CPU(cpu)->as, patb);
    entry->dw1 = ldq_phys(CPU(cpu)->as, patb + 8);
    return true;
}

bool ppc64_v3_get_pate(PowerPCCPU *cpu, target_ulong lpid, ppc_v3_pate_t *entry)
{
    uint64_t pats = cpu->env.spr[SPR_PTCR] & PTCR_PATS;

    /*
     * Keep the existing ISA v3.0 PATS interpretation first.  OpenBSD/powernv
     * uses the PATSIZE value it writes to PTCR as one exponent smaller, and it
     * only needs that interpretation for the bare metal LPID 0 table.
     */
    if (ppc64_v3_get_pate_from_size(cpu, lpid, entry, 1ull << (pats + 12))) {
        return true;
    }

    if (lpid == 0) {
        return ppc64_v3_get_pate_from_size(cpu, lpid, entry,
                                           1ull << (pats + 11));
    }

    return false;
}
