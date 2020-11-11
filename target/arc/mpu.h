/*
 * QEMU ARC CPU
 *
 * Copyright (c) 2020 Synppsys Inc.
 * Contributed by Shahab Vahedi (Synopsys)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * http://www.gnu.org/licenses/lgpl-2.1.html
 */

#ifndef ARC_MPU_H
#define ARC_MPU_H

#include "target/arc/regs.h"
#include "cpu-qom.h"

/* These values are based on ARCv2 ISA PRM for ARC HS processors */
#define ARC_MPU_VERSION         0x03    /* MPU version supported          */
#define ARC_MPU_MAX_NR_REGIONS  16      /* Number of regions to protect   */
#define ARC_MPU_ECR_VEC_NUM     0x06    /* EV_ProtV: Protection Violation */
#define ARC_MPU_ECR_PARAM       0x04    /* MPU (as opposed to MMU, ...)   */

/* MPU Build Configuration Register */
typedef struct MPUBCR {
    uint8_t version; /* 0 (disabled), 0x03 */
    uint8_t regions; /* 0, 1, 2, 4, 8, 16  */
} MPUBCR;

typedef struct MPUPermissions {
    bool     KR;    /* Kernel read    */
    bool     KW;    /* Kernel write   */
    bool     KE;    /* Kernel execute */
    bool     UR;    /* User   read    */
    bool     UW;    /* User   write   */
    bool     UE;    /* User   execute */
} MPUPermissions;

/* MPU Enable Register */
typedef struct MPUEnableReg {
    bool           enabled;     /* Is MPU enabled? */
    MPUPermissions permission;  /* Default region permissions */
} MPUEnableReg;

/* Determines during which type of operation a violation occurred */
enum MPUCauseCode {
    MPU_CAUSE_FETCH = 0x00,
    MPU_CAUSE_READ  = 0x01,
    MPU_CAUSE_WRITE = 0x02,
    MPU_CAUSE_RW    = 0x03
};

/* The exception to be set */
typedef struct MPUException {
    uint8_t number;     /* Exception vector number: 0x06 -> EV_ProtV  */
    uint8_t code;       /* Cause code: fetch, read, write, read/write */
    uint8_t param;      /* Always 0x04 to represent MPU               */
} MPUException;

/* MPU Exception Cause Register */
typedef struct MPUECR {
    uint8_t region;
    uint8_t violation; /* Fetch, read, write, read/write */
} MPUECR;

/* MPU Region Descriptor Base Register */
typedef struct MPUBaseReg {
    bool     valid; /* Is this region valid? */
    uint32_t addr;  /* Minimum size is 32 bytes --> bits[4:0] are 0 */
} MPUBaseReg;

/* MPU Region Descriptor Permissions Register */
typedef struct MPUPermReg {
    /* size_bits: 00100b ... 11111b */
    uint8_t        size_bits;
    /*
     * We need normal notation of size to set qemu's tlb page size later.
     * Region's size: 32 bytes, 64 bytes,  ..., 4 gigabytes
     */
    uint64_t       size;   /* 2 << size_bits */
    /*
     * Region offset: 0x1f, 0x3f, ..., 0xffffffff
     * Hence region mask: 0xffffffe0, 0xfffffc0, ..., 0x00000000
     */
    uint32_t       mask;
    MPUPermissions permission; /* region's permissions */
} MPUPermReg;

typedef struct ARCMPU {
    bool         enabled;

    MPUBCR       reg_bcr;
    MPUEnableReg reg_enable;
    MPUECR       reg_ecr;
    /* Base and permission registers are paired */
    MPUBaseReg   reg_base[ARC_MPU_MAX_NR_REGIONS];
    MPUPermReg   reg_perm[ARC_MPU_MAX_NR_REGIONS];

    MPUException exception;
} ARCMPU;

enum ARCMPUVerifyRet {
  MPU_SUCCESS,
  MPU_FAULT
};

struct ARCCPU;
struct CPUARCState;

/* Used during a reset */
extern void arc_mpu_init(struct ARCCPU *cpu);

/* Get auxiliary MPU registers */
extern uint32_t
arc_mpu_aux_get(const struct arc_aux_reg_detail *aux_reg_detail, void *data);

/* Set auxiliary MPU registers */
extern void
arc_mpu_aux_set(const struct arc_aux_reg_detail *aux_reg_detail,
                const uint32_t val, void *data);

/*
 * Verifies if 'access' to 'addr' is allowed or not.
 * possible return values:
 * MPU_SUCCESS - allowed; 'prot' holds permissions
 * MPU_FAULT   - not allowed; corresponding exception parameters are set
 */
extern int
arc_mpu_translate(struct CPUARCState *env, uint32_t addr,
                  MMUAccessType access, int mmu_idx);

#endif /* ARC_MPU_H */
