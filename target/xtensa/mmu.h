/*
 * Xtensa MMU/MPU helpers
 *
 * SPDX-FileCopyrightText: 2011 - 2019, Max Filippov, Open Source and Linux Lab.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef TARGET_XTENSA_MMU_H
#define TARGET_XTENSA_MMU_H

#include "cpu.h"

typedef struct xtensa_tlb_entry {
    uint32_t vaddr;
    uint32_t paddr;
    uint8_t asid;
    uint8_t attr;
    bool variable;
} xtensa_tlb_entry;

typedef struct xtensa_tlb {
    unsigned nways;
    const unsigned way_size[10];
    bool varway56;
    unsigned nrefillentries;
} xtensa_tlb;

typedef struct xtensa_mpu_entry {
    uint32_t vaddr;
    uint32_t attr;
} xtensa_mpu_entry;

#define XTENSA_MPU_SEGMENT_MASK 0x0000001f
#define XTENSA_MPU_ACC_RIGHTS_MASK 0x00000f00
#define XTENSA_MPU_ACC_RIGHTS_SHIFT 8
#define XTENSA_MPU_MEM_TYPE_MASK 0x001ff000
#define XTENSA_MPU_MEM_TYPE_SHIFT 12
#define XTENSA_MPU_ATTR_MASK 0x001fff00

#define XTENSA_MPU_PROBE_B 0x40000000
#define XTENSA_MPU_PROBE_V 0x80000000

#define XTENSA_MPU_SYSTEM_TYPE_DEVICE 0x0001
#define XTENSA_MPU_SYSTEM_TYPE_NC     0x0002
#define XTENSA_MPU_SYSTEM_TYPE_C      0x0003
#define XTENSA_MPU_SYSTEM_TYPE_MASK   0x0003

#define XTENSA_MPU_TYPE_SYS_C     0x0010
#define XTENSA_MPU_TYPE_SYS_W     0x0020
#define XTENSA_MPU_TYPE_SYS_R     0x0040
#define XTENSA_MPU_TYPE_CPU_C     0x0100
#define XTENSA_MPU_TYPE_CPU_W     0x0200
#define XTENSA_MPU_TYPE_CPU_R     0x0400
#define XTENSA_MPU_TYPE_CPU_CACHE 0x0800
#define XTENSA_MPU_TYPE_B         0x1000
#define XTENSA_MPU_TYPE_INT       0x2000

unsigned mmu_attr_to_access(uint32_t attr);
unsigned mpu_attr_to_access(uint32_t attr, unsigned ring);
unsigned mpu_attr_to_cpu_cache(uint32_t attr);
unsigned mpu_attr_to_type(uint32_t attr);

unsigned region_attr_to_access(uint32_t attr);
unsigned cacheattr_attr_to_access(uint32_t attr);

xtensa_tlb_entry *xtensa_get_tlb_entry(CPUXtensaState *env,
                                       uint32_t v, bool dtlb, uint32_t *pwi);
xtensa_tlb_entry *xtensa_tlb_get_entry(CPUXtensaState *env, bool dtlb,
                                       unsigned wi, unsigned ei);
void xtensa_tlb_set_entry(CPUXtensaState *env, bool dtlb,
                          unsigned wi, unsigned ei,
                          uint32_t vpn, uint32_t pte);

uint32_t xtensa_tlb_get_addr_mask(const CPUXtensaState *env, bool dtlb,
                                  uint32_t way);
uint32_t xtensa_get_vpn_mask(const CPUXtensaState *env, bool dtlb,
                             uint32_t way);

bool xtensa_split_tlb_entry_spec(CPUXtensaState *env, uint32_t v, bool dtlb,
                                 uint32_t *vpn, uint32_t *wi, uint32_t *ei);

int xtensa_tlb_lookup(const CPUXtensaState *env, uint32_t addr, bool dtlb,
                      uint32_t *pwi, uint32_t *pei, uint8_t *pring);
int xtensa_mpu_lookup(const xtensa_mpu_entry *entry, unsigned n,
                      uint32_t vaddr, unsigned *segment);

int xtensa_get_physical_addr(CPUXtensaState *env, bool update_tlb,
                             uint32_t vaddr, int is_write, int mmu_idx,
                             uint32_t *paddr, uint32_t *page_size,
                             unsigned *access);

void xtensa_reset_mmu(CPUXtensaState *env);
void xtensa_dump_mmu(CPUXtensaState *env);

#endif
