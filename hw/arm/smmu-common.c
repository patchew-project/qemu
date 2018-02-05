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
#include "exec/target_page.h"
#include "qom/cpu.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"

#include "qemu/error-report.h"
#include "hw/arm/smmu-common.h"
#include "smmu-internal.h"

/* VMSAv8-64 Translation */

/**
 * get_pte - Get the content of a page table entry located t
 * @base_addr[@index]
 */
static int get_pte(dma_addr_t baseaddr, uint32_t index, uint64_t *pte,
                   SMMUPTWEventInfo *info)
{
    int ret;
    dma_addr_t addr = baseaddr + index * sizeof(*pte);

    ret = dma_memory_read(&address_space_memory, addr,
                          (uint8_t *)pte, sizeof(*pte));

    if (ret != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Cannot fetch pte at address=0x%"PRIx64"\n", addr);
        info->type = SMMU_PTW_ERR_WALK_EABT;
        info->addr = addr;
        return -EINVAL;
    }
    trace_smmu_get_pte(baseaddr, index, addr, *pte);
    return 0;
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
 * get_block_pte_address - return block descriptor output address and block size
 * ARM ARM Figure D4-16 VMSAv8-64 level0, level1, and level 2 descriptor formats
 */
static hwaddr get_block_pte_address(uint64_t pte, int level, int granule_sz,
                                    uint64_t *bsz)
{
    int n = 0;

    switch (granule_sz) {
    case 12:
        if (level == 1) {
            n = 30;
        } else if (level == 2) {
            n = 21;
        }
        break;
    case 14:
        if (level == 2) {
            n = 25;
        }
        break;
    case 16:
        if (level == 2) {
            n = 29;
        }
        break;
    }
    if (!n) {
        error_setg(&error_fatal,
                   "wrong granule/level combination (%d/%d)",
                   granule_sz, level);
    }
    *bsz = 1 << n;
    return PTE_ADDRESS(pte, n);
}

static inline bool check_perm(int access_attrs, int mem_attrs)
{
    if (((access_attrs & IOMMU_RO) && !(mem_attrs & IOMMU_RO)) ||
        ((access_attrs & IOMMU_WO) && !(mem_attrs & IOMMU_WO))) {
        return false;
    }
    return true;
}

SMMUTransTableInfo *select_tt(SMMUTransCfg *cfg, dma_addr_t iova)
{
    if (!extract64(iova, 64 - cfg->tt[0].tsz, cfg->tt[0].tsz - cfg->tbi)) {
        return &cfg->tt[0];
    }
    return &cfg->tt[1];
}

/**
 * smmu_ptw_64 - VMSAv8-64 Walk of the page tables for a given IOVA
 * @cfg: translation config
 * @tlbe: pre-filled IOMMUTLBEntry
 * @info: handle to an error info
 *
 * Return 0 on success, < 0 on error. In case of error @info is filled
 */
static int smmu_ptw_64(SMMUTransCfg *cfg, IOMMUTLBEntry *tlbe,
                       SMMUPTWEventInfo *info)
{
    dma_addr_t baseaddr;
    int stage = cfg->stage;
    dma_addr_t iova = tlbe->iova;
    SMMUTransTableInfo *tt = select_tt(cfg, iova);
    uint8_t level;
    uint8_t granule_sz;

    if (tt->disabled) {
        info->type = SMMU_PTW_ERR_TRANSLATION;
        goto error;
    }

    level = tt->initial_level;
    granule_sz = tt->granule_sz;
    baseaddr = extract64(tt->ttb, 0, 48);

    tlbe->addr_mask = (1 << tt->granule_sz) - 1;

    while (level <= 3) {
        uint64_t subpage_size = 1ULL << level_shift(level, granule_sz);
        uint64_t mask = subpage_size - 1;
        uint32_t offset = iova_level_offset(iova, level, granule_sz);
        uint64_t pte;
        dma_addr_t pte_addr = baseaddr + offset * sizeof(pte);

        trace_smmu_page_walk_level(level, iova, subpage_size,
                                   baseaddr, offset, pte);

        if (get_pte(baseaddr, offset, &pte, info)) {
                info->type = SMMU_PTW_ERR_WALK_EABT;
                info->addr = pte_addr;
                goto error;
        }
        if (is_invalid_pte(pte) || is_reserved_pte(pte, level)) {
            trace_smmu_page_walk_level_res_invalid_pte(stage, level, baseaddr,
                                                       pte_addr, offset, pte);
            info->type = SMMU_PTW_ERR_TRANSLATION;
            goto error;
        }

        if (is_page_pte(pte, level)) {
            uint64_t gpa = get_page_pte_address(pte, granule_sz);
            if (is_fault(tlbe->perm, pte, true)) {
                info->type = SMMU_PTW_ERR_PERMISSION;
                goto error;
            }

            tlbe->translated_addr = gpa + (iova & mask);
            trace_smmu_page_walk_level_page_pte(stage, level, iova,
                                                baseaddr, pte_addr, pte, gpa);
            return 0;
        }
        if (is_block_pte(pte, level)) {
            uint64_t block_size;
            hwaddr gpa = get_block_pte_address(pte, level, granule_sz,
                                               &block_size);
            if (is_fault(tlbe->perm, pte, true)) {
                info->type = SMMU_PTW_ERR_PERMISSION;
                goto error;
            }

            trace_smmu_page_walk_level_block_pte(stage, level, baseaddr,
                                                 pte_addr, pte, iova, gpa,
                                                 (int)(block_size >> 20));

            tlbe->translated_addr = gpa + (iova & mask);
            return 0;
        }

        /* table pte */
        if (is_fault(tlbe->perm, pte, false)) {
            info->type = SMMU_PTW_ERR_PERMISSION;
            goto error;
        }
        baseaddr = get_table_pte_address(pte, granule_sz);
        level++;
    }

    info->type = SMMU_PTW_ERR_TRANSLATION;

error:
    return -EINVAL;
}

/**
 * smmu_ptw - Walk the page tables for an IOVA, according to @cfg
 *
 * @cfg: translation configuration
 * @tlbe: pre-filled entry
 * @info: ptw event handle
 *
 * return 0 on success
 */
int smmu_ptw(SMMUTransCfg *cfg, IOMMUTLBEntry *tlbe,
             SMMUPTWEventInfo *info)
{
    if (!cfg->aa64) {
        error_setg(&error_fatal,
                   "SMMUv3 model does not support VMSAv8-32 page walk yet");
    }

    return smmu_ptw_64(cfg, tlbe, info);
}

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

static AddressSpace *smmu_find_add_as(PCIBus *bus, void *opaque, int devfn)
{
    SMMUState *s = opaque;
    SMMUPciBus *sbus = g_hash_table_lookup(s->smmu_as_by_busptr, &bus);
    SMMUDevice *sdev;

    if (!sbus) {
        sbus = g_malloc0(sizeof(SMMUPciBus) +
                         sizeof(SMMUDevice *) * SMMU_PCI_DEVFN_MAX);
        sbus->bus = bus;
        g_hash_table_insert(s->smmu_as_by_busptr, bus, sbus);
    }

    sdev = sbus->pbdev[devfn];
    if (!sdev) {
        char *name = g_strdup_printf("%s-%d-%d",
                                     s->mrtypename,
                                     pci_bus_num(bus), devfn);
        sdev = sbus->pbdev[devfn] = g_new0(SMMUDevice, 1);

        sdev->smmu = s;
        sdev->bus = bus;
        sdev->devfn = devfn;

        memory_region_init_iommu(&sdev->iommu, sizeof(sdev->iommu),
                                 s->mrtypename,
                                 OBJECT(s), name, 1ULL << SMMU_MAX_VA_BITS);
        address_space_init(&sdev->as,
                           MEMORY_REGION(&sdev->iommu), name);
    }

    return &sdev->as;
}

static void smmu_base_realize(DeviceState *dev, Error **errp)
{
    SMMUState *s = ARM_SMMU(dev);

    s->smmu_as_by_busptr = g_hash_table_new(NULL, NULL);

    if (s->primary_bus) {
        pci_setup_iommu(s->primary_bus, smmu_find_add_as, s);
    } else {
        error_setg(errp, "SMMU is not attached to any PCI bus!");
    }
}

static Property smmu_dev_properties[] = {
    DEFINE_PROP_UINT8("bus_num", SMMUState, bus_num, 0),
    DEFINE_PROP_LINK("primary-bus", SMMUState, primary_bus, "PCI", PCIBus *),
    DEFINE_PROP_END_OF_LIST(),
};

static void smmu_base_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SMMUBaseClass *sbc = ARM_SMMU_CLASS(klass);

    dc->props = smmu_dev_properties;
    sbc->parent_realize = dc->realize;
    dc->realize = smmu_base_realize;
}

static const TypeInfo smmu_base_info = {
    .name          = TYPE_ARM_SMMU,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SMMUState),
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

