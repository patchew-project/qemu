/*
 * i386 memory mapping
 *
 * Copyright Fujitsu, Corp. 2011, 2012
 *
 * Authors:
 *     Wen Congyang <wency@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "sysemu/memory_mapping.h"

/**
 ************** code hook implementations for x86 ***********
 */

/* PAE Paging or IA-32e Paging */
#define PML4_ADDR_MASK 0xffffffffff000ULL /* selects bits 51:12 */

/**
 * x86_page_table_root - Given a CPUState, return the physical address
 *                       of the current page table root, as well as
 *                       write the height of the tree into *height.
 *
 * @cs - CPU state
 * @height - a pointer to an integer, to store the page table tree height
 *
 * Returns a hardware address on success.  Should not fail (i.e., caller is
 * responsible to ensure that a page table is actually present).
 */
hwaddr x86_page_table_root(CPUState *cs, int *height)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    /*
     * DEP 5/15/24: Some original page table walking code sets the a20
     * mask as a 32 bit integer and checks it on each level of hte
     * page table walk; some only checks it against the final result.
     * For 64 bits, I think we need to sign extend in the common case
     * it is not set (and returns -1), or we will lose bits.
     */
    int64_t a20_mask;

    assert(cpu_paging_enabled(cs));
    a20_mask = x86_get_a20_mask(env);

    if (env->cr[4] & CR4_PAE_MASK) {
#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            if (env->cr[4] & CR4_LA57_MASK) {
                *height = 5;
            } else {
                *height = 4;
            }
            return (env->cr[3] & PML4_ADDR_MASK) & a20_mask;
        } else
#endif
        {
            *height = 3;
            return (env->cr[3] & ~0x1f) & a20_mask;
        }
    } else {
        *height = 2;
        return (env->cr[3] & ~0xfff) & a20_mask;
    }
}


/**
 * x86_page_table_entries_per_node - Return the number of entries in a
 *                                   page table node for the CPU at a
 *                                   given height.
 *
 * @cs - CPU state
 * @height - height of the page table tree to query, where the leaves
 *          are 1.
 *
 * Returns a value greater than zero on success, -1 on error.
 */
int x86_page_table_entries_per_node(CPUState *cs, int height)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    bool pae_enabled = env->cr[4] & CR4_PAE_MASK;

    assert(height < 6);
    assert(height > 0);

    switch (height) {
#ifdef TARGET_X86_64
    case 5:
        assert(env->cr[4] & CR4_LA57_MASK);
    case 4:
        assert(env->hflags & HF_LMA_MASK);
        assert(pae_enabled);
        return 512;
#endif
    case 3:
        assert(pae_enabled);
#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            return 512;
        } else
#endif
        {
            return 4;
        }
    case 2:
    case 1:
        return pae_enabled ? 512 : 1024;
    default:
        g_assert_not_reached();
    }
    return -1;
}

/**
 * x86_pte_leaf_page_size - Return the page size of a leaf entry,
 *                          given the height and CPU state
 *
 * @cs - CPU state
 * @height - height of the page table tree to query, where the leaves
 *          are 1.
 *
 * Returns a value greater than zero on success, -1 on error.
 */
uint64_t x86_pte_leaf_page_size(CPUState *cs, int height)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    bool pae_enabled = env->cr[4] & CR4_PAE_MASK;

    assert(height < 6);
    assert(height > 0);

    switch (height) {
#ifdef TARGET_X86_64
    case 5:
        assert(pae_enabled);
        assert(env->cr[4] & CR4_LA57_MASK);
        assert(env->hflags & HF_LMA_MASK);
        return 1ULL << 48;
    case 4:
        assert(pae_enabled);
        assert(env->hflags & HF_LMA_MASK);
        return 1ULL << 39;
#endif
    case 3:
        assert(pae_enabled);
        return 1 << 30;
    case 2:
        if (pae_enabled) {
            return 1 << 21;
        } else {
            return 1 << 22;
        }
    case 1:
        return 4096;
    default:
        g_assert_not_reached();
    }
    return -1;
}

/*
 * Given a CPU state and height, return the number of bits
 * to shift right/left in going from virtual to PTE index
 * and vice versa, the number of useful bits.
 */
static void _mmu_decode_va_parameters(CPUState *cs, int height,
                                      int *shift, int *width)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    int _shift = 0;
    int _width = 0;
    bool pae_enabled = env->cr[4] & CR4_PAE_MASK;

    switch (height) {
    case 5:
        _shift = 48;
        _width = 9;
        break;
    case 4:
        _shift = 39;
        _width = 9;
        break;
    case 3:
        _shift = 30;
        _width = 9;
        break;
    case 2:
        /* 64 bit page tables shift from 30->21 bits here */
        if (pae_enabled) {
            _shift = 21;
            _width = 9;
        } else {
            /* 32 bit page tables shift from 32->22 bits */
            _shift = 22;
            _width = 10;
        }
        break;
    case 1:
        _shift = 12;
        if (pae_enabled) {
            _width = 9;
        } else {
            _width = 10;
        }

        break;
    default:
        g_assert_not_reached();
    }

    if (shift) {
        *shift = _shift;
    }

    if (width) {
        *width = _width;
    }
}

/**
 * get_pte - Copy the contents of the page table entry at node[i] into pt_entry.
 *           Optionally, add the relevant bits to the virtual address in
 *           vaddr_pte.
 *
 * @cs - CPU state
 * @node - physical address of the current page table node
 * @i - index (in page table entries, not bytes) of the page table
 *      entry, within node
 * @height - height of node within the tree (leaves are 1, not 0)
 * @pt_entry - Poiter to a PTE_t, stores the contents of the page table entry
 * @vaddr_parent - The virtual address bits already translated in walking the
 *                 page table to node.  Optional: only used if vaddr_pte is set.
 * @vaddr_pte - Optional pointer to a variable storing the virtual address bits
 *              translated by node[i].
 * @pte_paddr - Pointer to the physical address of the PTE within node.
 *              Optional parameter.
 */
void
x86_get_pte(CPUState *cs, hwaddr node, int i, int height,
            PTE_t *pt_entry, vaddr vaddr_parent, vaddr *vaddr_pte,
            hwaddr *pte_paddr)

{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    int32_t a20_mask = x86_get_a20_mask(env);
    hwaddr pte;

    if (env->hflags & HF_LMA_MASK) {
        /* 64 bit */
        int pte_width = 8;
        pte = (node + (i * pte_width)) & a20_mask;
        pt_entry->pte64_t = address_space_ldq(cs->as, pte,
                                              MEMTXATTRS_UNSPECIFIED, NULL);
    } else {
        /* 32 bit */
        int pte_width = 4;
        pte = (node + (i * pte_width)) & a20_mask;
        pt_entry->pte32_t = address_space_ldl(cs->as, pte,
                                              MEMTXATTRS_UNSPECIFIED, NULL);
    }

    if (vaddr_pte) {
        int shift = 0;
        _mmu_decode_va_parameters(cs, height, &shift, NULL);
        *vaddr_pte = vaddr_parent | ((i & 0x1ffULL) << shift);
    }

    if (pte_paddr) {
        *pte_paddr = pte;
    }
}


static bool
mmu_pte_check_bits(CPUState *cs, PTE_t *pte, int64_t mask)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    if (env->hflags & HF_LMA_MASK) {
        return pte->pte64_t & mask;
    } else {
        return pte->pte32_t & mask;
    }
}

/**
 * x86_pte_present - Return true if the pte is marked 'present'
 */
bool
x86_pte_present(CPUState *cs, PTE_t *pte)
{
    return mmu_pte_check_bits(cs, pte, PG_PRESENT_MASK);
}

/**
 * x86_pte_leaf - Return true if the pte is
 *                a page table leaf, false if
 *                the pte points to another
 *                node in the radix tree.
 */
bool
x86_pte_leaf(CPUState *cs, int height, PTE_t *pte)
{
    return height == 1 || mmu_pte_check_bits(cs, pte, PG_PSE_MASK);
}

/**
 * x86_pte_child - Returns the physical address
 *                 of a radix tree node pointed to by pte.
 *
 * @cs - CPU state
 * @pte - The page table entry
 * @height - The height in the tree of pte
 *
 * Returns the physical address stored in pte on success,
 *     -1 on error.
 */
hwaddr
x86_pte_child(CPUState *cs, PTE_t *pte, int height)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    bool pae_enabled = env->cr[4] & CR4_PAE_MASK;
    int32_t a20_mask = x86_get_a20_mask(env);

    switch (height) {
#ifdef TARGET_X86_64
    case 5:
        assert(env->cr[4] & CR4_LA57_MASK);
    case 4:
        assert(env->hflags & HF_LMA_MASK);
        /* assert(pae_enabled); */
        /* Fall through */
#endif
    case 3:
        assert(pae_enabled);
#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            return (pte->pte64_t & PG_ADDRESS_MASK) & a20_mask;
        } else
#endif
        {
            return (pte->pte64_t & ~0xfff) & a20_mask;
        }
    case 2:
    case 1:
        if (pae_enabled) {
            return (pte->pte64_t & PG_ADDRESS_MASK) & a20_mask;
        } else {
            return (pte->pte32_t & ~0xfff) & a20_mask;
        }
    default:
        g_assert_not_reached();
    }
    return -1;
}

/**
 * Back to x86 hooks
 */
struct memory_mapping_data {
    MemoryMappingList *list;
};

static int add_memory_mapping_to_list(CPUState *cs, void *data, PTE_t *pte,
                                      vaddr vaddr_in, int height,
                                      int offset)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    struct memory_mapping_data *mm_data = (struct memory_mapping_data *) data;

    hwaddr start_paddr = 0;
    size_t pg_size = x86_pte_leaf_page_size(cs, height);
    switch (height) {
    case 1:
        start_paddr = pte->pte64_t & ~0xfff;
        if (env->cr[4] & CR4_PAE_MASK) {
            start_paddr &= ~(0x1ULL << 63);
        }
        break;
    case 2:
        if (env->cr[4] & CR4_PAE_MASK) {
            start_paddr = (pte->pte64_t & ~0x1fffff) & ~(0x1ULL << 63);
        } else {
            assert(!!(env->cr[4] & CR4_PSE_MASK));
            /*
             * 4 MB page:
             * bits 39:32 are bits 20:13 of the PDE
             * bit3 31:22 are bits 31:22 of the PDE
             */
            hwaddr high_paddr = ((hwaddr)(pte->pte64_t & 0x1fe000) << 19);
            start_paddr = (pte->pte64_t & ~0x3fffff) | high_paddr;
        }
        break;
    case 3:
        /* Select bits 30--51 */
        start_paddr = (pte->pte64_t & 0xfffffc0000000);
        break;
    default:
        g_assert_not_reached();
    }

    /* This hook skips mappings for the I/O region */
    if (cpu_physical_memory_is_io(start_paddr)) {
        /* I/O region */
        return 0;
    }

    memory_mapping_list_add_merge_sorted(mm_data->list, start_paddr,
                                         vaddr_in, pg_size);
    return 0;
}

bool x86_cpu_get_memory_mapping(CPUState *cs, MemoryMappingList *list,
                                Error **errp)
{
    return for_each_pte(cs, &add_memory_mapping_to_list, list, false, false);
}
