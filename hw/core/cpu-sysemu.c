/*
 * QEMU CPU model (system emulation specific)
 *
 * Copyright (c) 2012-2014 SUSE LINUX Products GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <http://www.gnu.org/licenses/gpl-2.0.html>
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "exec/tswap.h"
#include "hw/core/sysemu-cpu-ops.h"

bool cpu_paging_enabled(const CPUState *cpu)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);

    if (cc->sysemu_ops->get_paging_enabled) {
        return cc->sysemu_ops->get_paging_enabled(cpu);
    }

    return false;
}

bool cpu_get_memory_mapping(CPUState *cpu, MemoryMappingList *list,
                            Error **errp)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);

    if (cc->sysemu_ops->get_memory_mapping) {
        return cc->sysemu_ops->get_memory_mapping(cpu, list, errp);
    }

    error_setg(errp, "Obtaining memory mappings is unsupported on this CPU.");
    return false;
}

hwaddr cpu_get_phys_page_attrs_debug(CPUState *cpu, vaddr addr,
                                     MemTxAttrs *attrs)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);

    if (cc->sysemu_ops->get_phys_page_attrs_debug) {
        return cc->sysemu_ops->get_phys_page_attrs_debug(cpu, addr, attrs);
    }
    /* Fallback for CPUs which don't implement the _attrs_ hook */
    *attrs = MEMTXATTRS_UNSPECIFIED;
    return cc->sysemu_ops->get_phys_page_debug(cpu, addr);
}

hwaddr cpu_get_phys_page_debug(CPUState *cpu, vaddr addr)
{
    MemTxAttrs attrs = {};

    return cpu_get_phys_page_attrs_debug(cpu, addr, &attrs);
}

int cpu_asidx_from_attrs(CPUState *cpu, MemTxAttrs attrs)
{
    int ret = 0;

    if (cpu->cc->sysemu_ops->asidx_from_attrs) {
        ret = cpu->cc->sysemu_ops->asidx_from_attrs(cpu, attrs);
        assert(ret < cpu->num_ases && ret >= 0);
    }
    return ret;
}

int cpu_write_elf32_qemunote(WriteCoreDumpFunction f, CPUState *cpu,
                             void *opaque)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);

    if (!cc->sysemu_ops->write_elf32_qemunote) {
        return 0;
    }
    return (*cc->sysemu_ops->write_elf32_qemunote)(f, cpu, opaque);
}

int cpu_write_elf32_note(WriteCoreDumpFunction f, CPUState *cpu,
                         int cpuid, void *opaque)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);

    if (!cc->sysemu_ops->write_elf32_note) {
        return -1;
    }
    return (*cc->sysemu_ops->write_elf32_note)(f, cpu, cpuid, opaque);
}

int cpu_write_elf64_qemunote(WriteCoreDumpFunction f, CPUState *cpu,
                             void *opaque)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);

    if (!cc->sysemu_ops->write_elf64_qemunote) {
        return 0;
    }
    return (*cc->sysemu_ops->write_elf64_qemunote)(f, cpu, opaque);
}

int cpu_write_elf64_note(WriteCoreDumpFunction f, CPUState *cpu,
                         int cpuid, void *opaque)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);

    if (!cc->sysemu_ops->write_elf64_note) {
        return -1;
    }
    return (*cc->sysemu_ops->write_elf64_note)(f, cpu, cpuid, opaque);
}

bool cpu_virtio_is_big_endian(CPUState *cpu)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);

    if (cc->sysemu_ops->virtio_is_big_endian) {
        return cc->sysemu_ops->virtio_is_big_endian(cpu);
    }
    return target_words_bigendian();
}

GuestPanicInformation *cpu_get_crash_info(CPUState *cpu)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);
    GuestPanicInformation *res = NULL;

    if (cc->sysemu_ops->get_crash_info) {
        res = cc->sysemu_ops->get_crash_info(cpu);
    }
    return res;
}

/**
 * _for_each_pte - recursive helper function
 *
 * @cs - CPU state
 * @fn(cs, data, pte, vaddr, height) - User-provided function to call on each
 *                                     pte.
 *   * @cs - pass through cs
 *   * @data - user-provided, opaque pointer
 *   * @pte - current pte
 *   * @vaddr_in - virtual address translated by pte
 *   * @height - height in the tree of pte
 * @data - user-provided, opaque pointer, passed to fn()
 * @visit_interior_nodes - if true, call fn() on page table entries in
 *                         interior nodes.  If false, only call fn() on page
 *                         table entries in leaves.
 * @visit_not_present - if true, call fn() on entries that are not present.
 *                         if false, visit only present entries.
 * @node - The physical address of the current page table radix tree node
 * @vaddr_in - The virtual address bits translated in walking the page
 *          table to node
 * @height - The height of node in the radix tree
 *
 * height starts at the max and counts down.
 * In a 4 level x86 page table, pml4e is level 4, pdpe is level 3,
 *  pde is level 2, and pte is level 1
 *
 * Returns true on success, false on error.
 */
static bool
_for_each_pte(CPUState *cs,
              int (*fn)(CPUState *cs, void *data, PTE_t *pte,
                        vaddr vaddr_in, int height, int offset),
              void *data, bool visit_interior_nodes,
              bool visit_not_present, hwaddr node,
              vaddr vaddr_in, int height)
{
    int ptes_per_node;
    int i;

    assert(height > 0);

    CPUClass *cc = CPU_GET_CLASS(cs);

    if ((!cc->sysemu_ops->page_table_entries_per_node)
        || (!cc->sysemu_ops->get_pte)
        || (!cc->sysemu_ops->pte_present)
        || (!cc->sysemu_ops->pte_leaf)
        || (!cc->sysemu_ops->pte_child)) {
        return false;
    }

    ptes_per_node = cc->sysemu_ops->page_table_entries_per_node(cs, height);

    for (i = 0; i < ptes_per_node; i++) {
        PTE_t pt_entry;
        vaddr vaddr_i;
        bool pte_present;

        cc->sysemu_ops->get_pte(cs, node, i, height, &pt_entry, vaddr_in,
                                &vaddr_i, NULL);
        pte_present = cc->sysemu_ops->pte_present(cs, &pt_entry);

        if (pte_present || visit_not_present) {
            if ((!pte_present) || cc->sysemu_ops->pte_leaf(cs, height,
                                                           &pt_entry)) {
                if (fn(cs, data, &pt_entry, vaddr_i, height, i)) {
                    /* Error */
                    return false;
                }
            } else { /* Non-leaf */
                if (visit_interior_nodes) {
                    if (fn(cs, data, &pt_entry, vaddr_i, height, i)) {
                        /* Error */
                        return false;
                    }
                }
                hwaddr child = cc->sysemu_ops->pte_child(cs, &pt_entry, height);
                assert(height > 1);
                if (!_for_each_pte(cs, fn, data, visit_interior_nodes,
                                   visit_not_present, child, vaddr_i,
                                   height - 1)) {
                    return false;
                }
            }
        }
    }

    return true;
}

/**
 * for_each_pte - iterate over a page table, and
 *                call fn on each entry
 *
 * @cs - CPU state
 * @fn(cs, data, pte, vaddr, height) - User-provided function to call on each
 *                                     pte.
 *   * @cs - pass through cs
 *   * @data - user-provided, opaque pointer
 *   * @pte - current pte
 *   * @vaddr - virtual address translated by pte
 *   * @height - height in the tree of pte
 * @data - opaque pointer; passed through to fn
 * @visit_interior_nodes - if true, call fn() on interior entries in
 *                         page table; if false, visit only leaf entries.
 * @visit_not_present - if true, call fn() on entries that are not present.
 *                         if false, visit only present entries.
 *
 * Returns true on success, false on error.
 *
 */
bool for_each_pte(CPUState *cs,
                  int (*fn)(CPUState *cs, void *data, PTE_t *pte,
                            vaddr vaddr, int height, int offset),
                  void *data, bool visit_interior_nodes,
                  bool visit_not_present)
{
    int height;
    vaddr vaddr = 0;
    hwaddr root;
    CPUClass *cc = CPU_GET_CLASS(cs);

    if (!cpu_paging_enabled(cs)) {
        /* paging is disabled */
        return true;
    }

    if (!cc->sysemu_ops->page_table_root) {
        return false;
    }

    root = cc->sysemu_ops->page_table_root(cs, &height);

    assert(height > 1);

    /* Recursively call a helper to walk the page table */
    return _for_each_pte(cs, fn, data, visit_interior_nodes, visit_not_present,
                         root, vaddr, height);
}
