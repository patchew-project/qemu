/*
 * Copyright (C) 2014-2016 Broadcom Corporation
 * Copyright (c) 2017 Red Hat, Inc.
 * Written by Prem Mallappa, Eric Auger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Author: Prem Mallappa <pmallapp@broadcom.com>
 *
 */

#include "qemu/osdep.h"
#include "sysemu/sysemu.h"
#include "exec/address-spaces.h"
#include "trace.h"
#include "qemu/error-report.h"
#include "hw/arm/smmu-common.h"
#include "smmu-internal.h"

inline MemTxResult smmu_read_sysmem(dma_addr_t addr, void *buf, dma_addr_t len,
                                    bool secure)
{
    MemTxAttrs attrs = {.unspecified = 1, .secure = secure};

    switch (len) {
    case 4:
        *(uint32_t *)buf = ldl_le_phys(&address_space_memory, addr);
        break;
    case 8:
        *(uint64_t *)buf = ldq_le_phys(&address_space_memory, addr);
        break;
    default:
        return address_space_rw(&address_space_memory, addr,
                                attrs, buf, len, false);
    }
    return MEMTX_OK;
}

inline void
smmu_write_sysmem(dma_addr_t addr, void *buf, dma_addr_t len, bool secure)
{
    MemTxAttrs attrs = {.unspecified = 1, .secure = secure};

    switch (len) {
    case 4:
        stl_le_phys(&address_space_memory, addr, *(uint32_t *)buf);
        break;
    case 8:
        stq_le_phys(&address_space_memory, addr, *(uint64_t *)buf);
        break;
    default:
        address_space_rw(&address_space_memory, addr,
                         attrs, buf, len, true);
    }
}

/*************************/
/* VMSAv8-64 Translation */
/*************************/

/**
 * get_pte - Get the content of a page table entry located in
 * @base_addr[@index]
 */
static uint64_t get_pte(dma_addr_t baseaddr, uint32_t index)
{
    uint64_t pte;

    if (smmu_read_sysmem(baseaddr + index * sizeof(pte),
                         &pte, sizeof(pte), false)) {
        error_report("can't read pte at address=0x%"PRIx64,
                     baseaddr + index * sizeof(pte));
        pte = (uint64_t)-1;
        return pte;
    }
    trace_smmu_get_pte(baseaddr, index, baseaddr + index * sizeof(pte), pte);
    /* TODO: handle endianness */
    return pte;
}

/* VMSAv8-64 Translation Table Format Descriptor Decoding */

#define PTE_ADDRESS(pte, shift) (extract64(pte, shift, 47 - shift) << shift)

/**
 * get_page_pte_address - returns the L3 descriptor output address,
 * ie. the page frame
 * ARM ARM spec: Figure D4-17 VMSAv8-64 level 3 descriptor format
 */
static inline hwaddr get_page_pte_address(uint64_t pte, int granule_sz)
{
    return PTE_ADDRESS(pte, granule_sz);
}

/**
 * get_table_pte_address - return table descriptor output address,
 * ie. address of next level table
 * ARM ARM Figure D4-16 VMSAv8-64 level0, level1, and level 2 descriptor formats
 */
static inline hwaddr get_table_pte_address(uint64_t pte, int granule_sz)
{
    return PTE_ADDRESS(pte, granule_sz);
}

/**
 * get_block_pte_address - return block descriptor output address
 * ARM ARM Figure D4-16 VMSAv8-64 level0, level1, and level 2 descriptor formats
 */
static hwaddr get_block_pte_address(uint64_t pte, int level, int granule_sz)
{
    int n;

    switch (granule_sz) {
    case 12:
        if (level == 1) {
            n = 30;
        } else if (level == 2) {
            n = 21;
        } else {
            goto error_out;
        }
        break;
    case 14:
        if (level == 2) {
            n = 25;
        } else {
            goto error_out;
        }
        break;
    case 16:
        if (level == 2) {
            n = 29;
        } else {
            goto error_out;
        }
        break;
    default:
            goto error_out;
    }
    return PTE_ADDRESS(pte, n);

error_out:

    error_report("unexpected granule_sz=%d/level=%d for block pte",
                 granule_sz, level);
    return (hwaddr)-1;
}

/**
 * smmu_page_walk_level_64 - Walk an IOVA range from a specific level
 * @baseaddr: table base address corresponding to @level
 * @level: level
 * @cfg: translation config
 * @start: end of the IOVA range
 * @end: end of the IOVA range
 * @hook_fn: the hook that to be called for each detected area
 * @private: private data for the hook function
 * @read: whether parent level has read permission
 * @write: whether parent level has write permission
 * @must_translate: indicates whether each iova of the range
 *  must be translated or whether failure is allowed
 * @notify_unmap: whether we should notify invalid entries
 *
 * Return 0 on success, < 0 on errors not related to translation
 * process, > 1 on errors related to translation process (only
 * if must_translate is set)
 */
static int
smmu_page_walk_level_64(dma_addr_t baseaddr, int level,
                        SMMUTransCfg *cfg, uint64_t start, uint64_t end,
                        smmu_page_walk_hook hook_fn, void *private,
                        bool read, bool write, bool must_translate,
                        bool notify_unmap)
{
    uint64_t subpage_size, subpage_mask, pte, iova = start;
    bool read_cur, write_cur, entry_valid;
    int ret, granule_sz;
    IOMMUTLBEntry entry;

    granule_sz = cfg->granule_sz;

    subpage_size = 1ULL << level_shift(level, granule_sz);
    subpage_mask = level_page_mask(level, granule_sz);

    trace_smmu_page_walk_level_in(level, baseaddr, granule_sz,
                                  start, end, subpage_size);

    while (iova < end) {
        dma_addr_t next_table_baseaddr;
        uint64_t iova_next;
        uint32_t offset;

        iova_next = (iova & subpage_mask) + subpage_size;

        offset = iova_level_offset(iova, level, granule_sz);
        pte = get_pte(baseaddr, offset);

        trace_smmu_page_walk_level(level, iova, baseaddr, offset, pte);

        if (pte == (uint64_t)-1) {
            if (must_translate) {
                return SMMU_TRANS_ERR_WALK_EXT_ABRT;
            }
            goto next;
        }
        if (is_invalid_pte(pte) || is_reserved_pte(pte, level)) {
            trace_smmu_page_walk_level_res_invalid_pte(baseaddr, offset, pte);
            if (must_translate) {
                return SMMU_TRANS_ERR_WALK_EXT_ABRT;
            }
            goto next;
        }

        read_cur = read; /* TODO */
        write_cur = write; /* TODO */
        entry_valid = read_cur | write_cur; /* TODO */

        if (is_page_pte(pte, level)) {
            entry.target_as = &address_space_memory;
            entry.iova = iova & subpage_mask;
            /* NOTE: this is only meaningful if entry_valid == true */
            entry.translated_addr = get_page_pte_address(pte, granule_sz);
            entry.addr_mask = ~subpage_mask;
            entry.perm = IOMMU_ACCESS_FLAG(read_cur, write_cur);
            trace_smmu_page_walk_level_page_pte(pte, entry.translated_addr);
            if (!entry_valid && !notify_unmap) {
                printf("%s entry_valid=%d notify_unmap=%d\n", __func__,
                       entry_valid, notify_unmap);
                goto next;
            }
            if (hook_fn) {
                ret = hook_fn(&entry, private);
                if (ret) {
                    return ret;
                }
            }
            goto next;
        }
        if (is_block_pte(pte, level)) {
            trace_smmu_page_walk_level_block_pte(pte,
                get_block_pte_address(pte, level, granule_sz));
            if (must_translate) {
                return SMMU_TRANS_ERR_WALK_EXT_ABRT;
            }
            printf("%s BLOCK PTE not handled yet\n", __func__);
            goto next;
        }
        /* table pte */
        next_table_baseaddr = get_table_pte_address(pte, granule_sz);
        trace_smmu_page_walk_level_table_pte(pte, next_table_baseaddr);
        ret = smmu_page_walk_level_64(next_table_baseaddr, level + 1, cfg,
                                      iova, MIN(iova_next, end),
                                      hook_fn, private, read_cur, write_cur,
                                      must_translate, notify_unmap);
        if (!ret) {
            return ret;
        }

next:
        iova = iova_next;
    }

    return SMMU_TRANS_ERR_NONE;
}

/**
 * smmu_page_walk_64 - walk a specific IOVA range from the initial
 * lookup level, and call the hook for each valid entry
 *
 * @cfg: translation config
 * @start: start of the IOVA range
 * @end: end of the IOVA range
 * @must_translate: indicates whether each iova of the range
 *  must be translated or whether failure is allowed
 * @hook_fn: the hook that to be called for each detected area
 * @private: private data for the hook function
 */
static int
smmu_page_walk_64(SMMUTransCfg *cfg, uint64_t start, uint64_t end,
                  bool must_translate, smmu_page_walk_hook hook_fn,
                  void *private)
{
    dma_addr_t ttbr;
    int stage = cfg->stage;
    int initial_level;

    if (stage != 1) {
        error_report("%s stage 2 not yet supported", __func__);
        return -1; /* TODO */
    }

    /* TODO check start/end */

    ttbr = extract64(cfg->ttbr, 0, 48);
    initial_level = initial_lookup_level(cfg->tsz, cfg->granule_sz);

    trace_smmu_page_walk(stage, cfg->ttbr, initial_level, start, end);

    if (initial_level < 0) {
        return -1; /* TODO */
    }

    return smmu_page_walk_level_64(ttbr, initial_level, cfg, start, end,
                                   hook_fn, private,
                                   true /* read */, true /* write */,
                                   must_translate, false /* notify_unmap */);
}

static int set_translated_address(IOMMUTLBEntry *entry, void *private)
{
    SMMUTransCfg *cfg = (SMMUTransCfg *)private;
    size_t offset = cfg->input - entry->iova;

    cfg->output = entry->translated_addr + offset;

    trace_smmu_set_translated_address(cfg->input, cfg->output);
    return 0;
}

static int
smmu_translate_64(SMMUTransCfg *cfg, uint32_t *pagesize,
                  uint32_t *perm, bool is_write)
{
    int ret;

    ret = smmu_page_walk_64(cfg, cfg->input, cfg->input + 1,
                            true /* must_translate */,
                            set_translated_address, cfg);
    *pagesize = 1 << cfg->granule_sz;
    return ret;
}

/*************************/
/* VMSAv8-32 Translation */
/*************************/

static int
smmu_page_walk_32(SMMUTransCfg *cfg, uint64_t start, uint64_t end,
                  bool must_translate, smmu_page_walk_hook hook_fn,
                  void *private)
{
    error_report("VMSAv8-32 translation is not yet implemented");
    abort();
}

static int smmu_translate_32(SMMUTransCfg *cfg, uint32_t *pagesize,
                             uint32_t *perm, bool is_write)
{
    error_report("VMSAv8-32 translation is not yet implemented");
    abort();
}

/******************/
/* Infrastructure */
/******************/

SMMUPciBus *smmu_find_as_from_bus_num(SMMUState *s, uint8_t bus_num)
{
    SMMUPciBus *smmu_pci_bus = s->smmu_as_by_bus_num[bus_num];

    if (!smmu_pci_bus) {
        GHashTableIter iter;

        g_hash_table_iter_init(&iter, s->smmu_as_by_busptr);
        while (g_hash_table_iter_next(&iter, NULL, (void **)&smmu_pci_bus)) {
            if (pci_bus_num(smmu_pci_bus->bus) == bus_num) {
                s->smmu_as_by_bus_num[bus_num] = smmu_pci_bus;
                return smmu_pci_bus;
            }
        }
    }
    return smmu_pci_bus;
}

static void smmu_base_instance_init(Object *obj)
{
     /* Nothing much to do here as of now */
}

static void smmu_base_class_init(ObjectClass *klass, void *data)
{
    SMMUBaseClass *sbc = SMMU_DEVICE_CLASS(klass);

    sbc->translate_64 = smmu_translate_64;
    sbc->page_walk_64 = smmu_page_walk_64;

    sbc->translate_32 = smmu_translate_32;
    sbc->page_walk_32 = smmu_page_walk_32;
}

static const TypeInfo smmu_base_info = {
    .name          = TYPE_SMMU_DEV_BASE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SMMUState),
    .instance_init = smmu_base_instance_init,
    .class_data    = NULL,
    .class_size    = sizeof(SMMUBaseClass),
    .class_init    = smmu_base_class_init,
    .abstract      = true,
};

static void smmu_base_register_types(void)
{
    type_register_static(&smmu_base_info);
}

type_init(smmu_base_register_types)

