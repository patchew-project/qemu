/*
 * Copyright (C) 2014-2016 Broadcom Corporation
 * Copyright (c) 2017 Red Hat, Inc.
 * Written by Prem Mallappa, Eric Auger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_ARM_SMMUV3_H
#define HW_ARM_SMMUV3_H

#include "hw/arm/smmu-common.h"

#define TYPE_SMMUV3_IOMMU_MEMORY_REGION "smmuv3-iommu-memory-region"

#define SMMU_NREGS            0x200

typedef struct SMMUQueue {
     hwaddr base;
     uint32_t prod;
     uint32_t cons;
     union {
          struct {
               uint8_t prod:1;
               uint8_t cons:1;
          };
          uint8_t unused;
     } wrap;

     uint16_t entries;           /* Number of entries */
     uint8_t  ent_size;          /* Size of entry in bytes */
     uint8_t  shift;             /* Size in log2 */
} SMMUQueue;

typedef struct SMMUV3State {
    SMMUState     smmu_state;

    /* Local cache of most-frequently used registers */
#define SMMU_FEATURE_2LVL_STE (1 << 0)
    uint32_t     features;
    uint16_t     sid_size;
    uint16_t     sid_split;
    uint64_t     strtab_base;

    uint32_t    regs[SMMU_NREGS];

    qemu_irq     irq[4];
    SMMUQueue    cmdq, evtq;

} SMMUV3State;

typedef enum {
    SMMU_IRQ_EVTQ,
    SMMU_IRQ_PRIQ,
    SMMU_IRQ_CMD_SYNC,
    SMMU_IRQ_GERROR,
} SMMUIrq;

typedef struct {
    SMMUBaseClass smmu_base_class;
} SMMUV3Class;

#define TYPE_SMMU_V3_DEV   "smmuv3"
#define SMMU_V3_DEV(obj) OBJECT_CHECK(SMMUV3State, (obj), TYPE_SMMU_V3_DEV)
#define SMMU_V3_DEVICE_GET_CLASS(obj)                              \
    OBJECT_GET_CLASS(SMMUBaseClass, (obj), TYPE_SMMU_V3_DEV)

#endif
