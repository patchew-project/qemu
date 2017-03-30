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

#include "hw/arm/smmu-common.h"

#ifdef ARM_SMMU_DEBUG
uint32_t dbg_bits =                             \
    DBG_BIT(PANIC) | DBG_BIT(CRIT) | DBG_BIT(WARN) | DBG_BIT(IRQ);
#else
const uint32_t dbg_bits;
#endif

inline MemTxResult smmu_read_sysmem(hwaddr addr, void *buf, int len,
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
smmu_write_sysmem(hwaddr addr, void *buf, int len, bool secure)
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

static SMMUTransErr
smmu_translate_64(SMMUTransCfg *cfg, uint32_t *pagesize,
                  uint32_t *perm, bool is_write)
{
    int     ret, level;
    int     stage      = cfg->stage;
    int     granule_sz = cfg->granule_sz[stage];
    int     va_size    = cfg->va_size[stage];
    hwaddr  va, addr, mask;
    hwaddr *outaddr;


    va = addr = cfg->va;        /* or ipa in Stage2 */
    SMMU_DPRINTF(TT_1, "stage:%d\n", stage);
    assert(va_size == 64);      /* We dont support 32-bit yet */
    /* same location, for clearity */
    outaddr = &cfg->pa;

    level = 4 - (va_size - cfg->tsz[stage] - 4) / granule_sz;

    mask = (1ULL << (granule_sz + 3)) - 1;

    addr = extract64(cfg->ttbr[stage], 0, 48);
    addr &= ~((1ULL << (va_size - cfg->tsz[stage] -
                        (granule_sz * (4 - level)))) - 1);

    for (;;) {
        uint64_t desc;
#ifdef ARM_SMMU_DEBUG
        uint64_t ored = (va >> (granule_sz * (4 - level))) & mask;
        SMMU_DPRINTF(TT_1,
                     "Level: %d va:%lx addr:%lx ored:%lx\n",
                     level, va, addr, ored);
#endif
        addr |= (va >> (granule_sz * (4 - level))) & mask;
        addr &= ~7ULL;

        if (smmu_read_sysmem(addr, &desc, sizeof(desc), false)) {
            ret = SMMU_TRANS_ERR_WALK_EXT_ABRT;
            SMMU_DPRINTF(CRIT, "Translation table read error lvl:%d\n", level);
            break;
        }

        SMMU_DPRINTF(TT_1,
                     "Level: %d gran_sz:%d mask:%lx addr:%lx desc:%lx\n",
                     level, granule_sz, mask, addr, desc);

        if (!(desc & 1) ||
            (!(desc & 2) && (level == 3))) {
            ret = SMMU_TRANS_ERR_TRANS;
            break;
        }

        /* We call again to resolve address at this 'level' */
        if (cfg->s2_needed) {
            uint32_t perm_s2, pagesize_s2;
            SMMUTransCfg s2cfg = *cfg;

            s2cfg.stage++;
            s2cfg.va = desc;
            s2cfg.s2_needed = false;

            ret = smmu_translate_64(&s2cfg, &pagesize_s2,
                                    &perm_s2, is_write);
            if (ret) {
                break;
            }

            desc = (uint64_t)s2cfg.pa;
            SMMU_DPRINTF(TT_2, "addr:%lx pagesize:%x\n", addr, *pagesize);
        }

        addr = desc & 0xffffffff000ULL;
        if ((desc & 2) && (level < 3)) {
            level++;
            continue;
        }
        *pagesize = (1ULL << ((granule_sz * (4 - level)) + 3));
        addr |= (va & (*pagesize - 1));
        SMMU_DPRINTF(TT_1, "addr:%lx pagesize:%x\n", addr, *pagesize);
        break;
    }

    if (ret == 0) {
        *outaddr = addr;
    }

    return ret;
}

static void smmu_base_instance_init(Object *obj)
{
     /* Nothing much to do here as of now */
}

static void smmu_base_class_init(ObjectClass *klass, void *data)
{
    SMMUBaseClass *sbc = SMMU_DEVICE_CLASS(klass);

    sbc->translate_64 = smmu_translate_64;
    sbc->translate_32 = NULL; /* not yet implemented */
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

