/*
 * CPU operations specific to system emulation
 *
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef SYSEMU_CPU_OPS_H
#define SYSEMU_CPU_OPS_H

#include "hw/core/cpu.h"

/* Maximum supported page table height - currently x86 at 5 */
#define MAX_HEIGHT 5

/*
 * struct mem_print_state: Used by monitor in walking page tables.
 */
struct mem_print_state {
    Monitor *mon;
    CPUArchState *env;
    int vaw, paw; /* VA and PA width in characters */
    int max_height;
    bool (*flusher)(CPUState *cs, struct mem_print_state *state);
    bool flush_interior; /* If false, only call flusher() on leaves */
    bool require_physical_contiguity;
    /*
     * The height at which we started accumulating ranges, i.e., the
     * next height we need to print once we hit the end of a
     * contiguous range.
     */
    int start_height;
    /*
     * For compressing contiguous ranges, track the
     * start and end of the range
     */
    hwaddr vstart[MAX_HEIGHT + 1]; /* Starting virt. addr. of open pte range */
    hwaddr vend[MAX_HEIGHT + 1]; /* Ending virtual address of open pte range */
    hwaddr pstart; /* Starting physical address of open pte range */
    hwaddr pend; /* Ending physical address of open pte range */
    int64_t ent[MAX_HEIGHT + 1]; /* PTE contents on current root->leaf path */
    int offset[MAX_HEIGHT + 1]; /* PTE range starting offsets */
    int last_offset[MAX_HEIGHT + 1]; /* PTE range ending offsets */
};

/*
 * struct SysemuCPUOps: System operations specific to a CPU class
 */
typedef struct SysemuCPUOps {
    /**
     * @get_memory_mapping: Callback for obtaining the memory mappings.
     */
    bool (*get_memory_mapping)(CPUState *cpu, MemoryMappingList *list,
                               Error **errp);
    /**
     * @get_paging_enabled: Callback for inquiring whether paging is enabled.
     */
    bool (*get_paging_enabled)(const CPUState *cpu);
    /**
     * @get_phys_page_debug: Callback for obtaining a physical address.
     */
    hwaddr (*get_phys_page_debug)(CPUState *cpu, vaddr addr);
    /**
     * @get_phys_page_attrs_debug: Callback for obtaining a physical address
     *       and the associated memory transaction attributes to use for the
     *       access.
     * CPUs which use memory transaction attributes should implement this
     * instead of get_phys_page_debug.
     */
    hwaddr (*get_phys_page_attrs_debug)(CPUState *cpu, vaddr addr,
                                        MemTxAttrs *attrs);
    /**
     * @asidx_from_attrs: Callback to return the CPU AddressSpace to use for
     *       a memory access with the specified memory transaction attributes.
     */
    int (*asidx_from_attrs)(CPUState *cpu, MemTxAttrs attrs);
    /**
     * @get_crash_info: Callback for reporting guest crash information in
     * GUEST_PANICKED events.
     */
    GuestPanicInformation* (*get_crash_info)(CPUState *cpu);
    /**
     * @write_elf32_note: Callback for writing a CPU-specific ELF note to a
     * 32-bit VM coredump.
     */
    int (*write_elf32_note)(WriteCoreDumpFunction f, CPUState *cpu,
                            int cpuid, DumpState *s);
    /**
     * @write_elf64_note: Callback for writing a CPU-specific ELF note to a
     * 64-bit VM coredump.
     */
    int (*write_elf64_note)(WriteCoreDumpFunction f, CPUState *cpu,
                            int cpuid, DumpState *s);
    /**
     * @write_elf32_qemunote: Callback for writing a CPU- and QEMU-specific ELF
     * note to a 32-bit VM coredump.
     */
    int (*write_elf32_qemunote)(WriteCoreDumpFunction f, CPUState *cpu,
                                DumpState *s);
    /**
     * @write_elf64_qemunote: Callback for writing a CPU- and QEMU-specific ELF
     * note to a 64-bit VM coredump.
     */
    int (*write_elf64_qemunote)(WriteCoreDumpFunction f, CPUState *cpu,
                                DumpState *s);
    /**
     * @virtio_is_big_endian: Callback to return %true if a CPU which supports
     * runtime configurable endianness is currently big-endian.
     * Non-configurable CPUs can use the default implementation of this method.
     * This method should not be used by any callers other than the pre-1.0
     * virtio devices.
     */
    bool (*virtio_is_big_endian)(CPUState *cpu);

    /**
     * @legacy_vmsd: Legacy state for migration.
     *               Do not use in new targets, use #DeviceClass::vmsd instead.
     */
    const VMStateDescription *legacy_vmsd;

    /**
     * page_table_root - Given a CPUState, return the physical address
     *                    of the current page table root, as well as
     *                    write the height of the tree into *height.
     *
     * @cs - CPU state

     * @height - a pointer to an integer, to store the page table tree
     *           height
     *
     * Returns a hardware address on success.  Should not fail (i.e.,
     * caller is responsible to ensure that a page table is actually
     * present).
     */
    hwaddr (*page_table_root)(CPUState *cs, int *height);

    /**
     * page_table_entries_per_node - Return the number of entries in a
     *                                   page table node for the CPU
     *                                   at a given height.
     *
     * @cs - CPU state
     * @height - height of the page table tree to query, where the leaves
     *          are 1.
     *
     * Returns a value greater than zero on success, -1 on error.
     */
    int (*page_table_entries_per_node)(CPUState *cs, int height);

    /**
     * get_pte - Copy the contents of the page table entry at node[i]
     *           into pt_entry.  Optionally, add the relevant bits to
     *           the virtual address in vaddr_pte.
     *
     * @cs - CPU state
     * @node - physical address of the current page table node
     * @i - index (in page table entries, not bytes) of the page table
     *      entry, within node
     * @height - height of node within the tree (leaves are 1, not 0)
     * @pt_entry - Pointer to a PTE_t, stores the contents of the page
     *             table entry
     * @vaddr_parent - The virtual address bits already translated in
     *                 walking the page table to node.  Optional: only
     *                 used if vaddr_pte is set.
     * @vaddr_pte - Optional pointer to a variable storing the virtual
     *              address bits translated by node[i].
     * @pte_paddr - Pointer to the physical address of the PTE within node.
     *              Optional parameter.
     */

    void (*get_pte)(CPUState *cs, hwaddr node, int i, int height,
                    PTE_t *pt_entry, vaddr vaddr_parent, vaddr *vaddr_pte,
                    hwaddr *pte_paddr);

    /**
     * pte_present - Return true if the pte is marked 'present'
     */
    bool (*pte_present)(CPUState *cs, PTE_t *pte);

    /**
     * pte_leaf - Return true if the pte is a page table leaf, false
     *                if the pte points to another node in the radix
     *                tree.
     */
    bool (*pte_leaf)(CPUState *cs, int height, PTE_t *pte);

    /**
     * pte_child - Returns the physical address of a radix tree node
     *                 pointed to by pte.
     *
     * @cs - CPU state
     * @pte - The page table entry
     * @height - The height in the tree of pte
     *
     * Returns the physical address stored in pte on success, -1 on
     * error.
     */
    hwaddr (*pte_child)(CPUState *cs, PTE_t *pte, int height);

    /**
     * pte_leaf_page_size - Return the page size of a leaf entry,
     *                          given the height and CPU state
     *
     * @cs - CPU state
     * @height - height of the page table tree to query, where the leaves
     *          are 1.
     *
     * Returns a value greater than zero on success, -1 on error.
     */
    uint64_t (*pte_leaf_page_size)(CPUState *cs, int height);

    /**
     * pte_flags - Return the flag bits of the page table entry.
     *
     * @pte - the contents of the PTE, not the address.
     *
     * Returns pte with the non-flag bits masked out.
     */
    uint64_t (*pte_flags)(uint64_t pte);

    /**
     * @mon_init_page_table_iterator: Callback to configure a page table
     * iterator for use by a monitor function.
     * Returns true on success, false if not supported (e.g., no paging disabled
     * or not implemented on this CPU).
     */
    bool (*mon_init_page_table_iterator)(Monitor *mon,
                                         struct mem_print_state *state);

    /**
     * @mon_info_pg_print_header: Prints the header line for 'info pg'.
     */
    void (*mon_info_pg_print_header)(Monitor *mon,
                                     struct mem_print_state *state);

    /**
     * @flush_page_table_iterator_state: Prints the last entry,
     * if one is present.  Useful for iterators that aggregate information
     * across page table entries.
     */
    bool (*mon_flush_page_print_state)(CPUState *cs,
                                       struct mem_print_state *state);

    /**
     * @mon_print_pte: Hook called by the monitor to print a page
     * table entry at address addr, with contents pte.
     */
    void (*mon_print_pte) (Monitor *mon, CPUArchState *env, hwaddr addr,
                           hwaddr pte);

    /**
     * @mon_print_mem: Hook called by the monitor to print a range
     * of memory mappings in 'info mem'
     */
    bool (*mon_print_mem)(CPUState *cs, struct mem_print_state *state);

} SysemuCPUOps;

#endif /* SYSEMU_CPU_OPS_H */
