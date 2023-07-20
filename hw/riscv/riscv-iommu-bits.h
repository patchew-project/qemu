// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright © 2022-2023 Rivos Inc.
 * Copyright © 2023 FORTH-ICS/CARV
 * Copyright © 2023 RISC-V IOMMU Task Group
 *
 * RISC-V Ziommu - Register Layout and Data Structures.
 *
 * Based on the 'RISC-V IOMMU Architecture Specification', Version 1.0
 * Published at  https://github.com/riscv-non-isa/riscv-iommu
 *
 */

#ifndef HW_RISCV_IOMMU_BITS_H
#define HW_RISCV_IOMMU_BITS_H

/*
 * This file is based on Linux RISC-V IOMMU file
 * located at 'drivers/iommu/riscv/iommu-bits.h'
 */

#include "qemu/osdep.h"

#define RISCV_IOMMU_SPEC_DOT_VER 0x010

#ifndef GENMASK_ULL
#define GENMASK_ULL(h, l) (((~0ULL) >> (63 - (h) + (l))) << (l))
#endif

/*
 * Chapter 5: Memory Mapped register interface
 */

/* Common field positions */
#define RISCV_IOMMU_PPN_FIELD           GENMASK_ULL(53, 10)
#define RISCV_IOMMU_QUEUE_LOGSZ_FIELD   GENMASK_ULL(4, 0)
#define RISCV_IOMMU_QUEUE_INDEX_FIELD   GENMASK_ULL(31, 0)
#define RISCV_IOMMU_QUEUE_ENABLE        BIT(0)
#define RISCV_IOMMU_QUEUE_INTR_ENABLE   BIT(1)
#define RISCV_IOMMU_QUEUE_MEM_FAULT     BIT(8)
#define RISCV_IOMMU_QUEUE_OVERFLOW      BIT(9)
#define RISCV_IOMMU_QUEUE_ACTIVE        BIT(16)
#define RISCV_IOMMU_QUEUE_BUSY          BIT(17)
#define RISCV_IOMMU_ATP_PPN_FIELD       GENMASK_ULL(43, 0)
#define RISCV_IOMMU_ATP_MODE_FIELD      GENMASK_ULL(63, 60)

/* 5.3 IOMMU Capabilities (64bits) */
#define RISCV_IOMMU_REG_CAP             0x0000
#define RISCV_IOMMU_CAP_VERSION         GENMASK_ULL(7, 0)
#define RISCV_IOMMU_CAP_S_SV32          BIT_ULL(8)
#define RISCV_IOMMU_CAP_S_SV39          BIT_ULL(9)
#define RISCV_IOMMU_CAP_S_SV48          BIT_ULL(10)
#define RISCV_IOMMU_CAP_S_SV57          BIT_ULL(11)
#define RISCV_IOMMU_CAP_SVPBMT          BIT_ULL(15)
#define RISCV_IOMMU_CAP_G_SV32          BIT_ULL(16)
#define RISCV_IOMMU_CAP_G_SV39          BIT_ULL(17)
#define RISCV_IOMMU_CAP_G_SV48          BIT_ULL(18)
#define RISCV_IOMMU_CAP_G_SV57          BIT_ULL(19)
#define RISCV_IOMMU_CAP_MSI_FLAT        BIT_ULL(22)
#define RISCV_IOMMU_CAP_MSI_MRIF        BIT_ULL(23)
#define RISCV_IOMMU_CAP_AMO             BIT_ULL(24)
#define RISCV_IOMMU_CAP_ATS             BIT_ULL(25)
#define RISCV_IOMMU_CAP_T2GPA           BIT_ULL(26)
#define RISCV_IOMMU_CAP_END             BIT_ULL(27)
#define RISCV_IOMMU_CAP_IGS             GENMASK_ULL(29, 28)
#define RISCV_IOMMU_CAP_HPM             BIT_ULL(30)
#define RISCV_IOMMU_CAP_DBG             BIT_ULL(31)
#define RISCV_IOMMU_CAP_PAS             GENMASK_ULL(37, 32)
#define RISCV_IOMMU_CAP_PD8             BIT_ULL(38)
#define RISCV_IOMMU_CAP_PD17            BIT_ULL(39)
#define RISCV_IOMMU_CAP_PD20            BIT_ULL(40)

#define RISCV_IOMMU_CAP_VERSION_VER_MASK      0xF0
#define RISCV_IOMMU_CAP_VERSION_REV_MASK      0x0F

/**
 * enum riscv_iommu_igs_settings - Interrupt Generation Support Settings
 * @RISCV_IOMMU_CAP_IGS_MSI: I/O MMU supports only MSI generation
 * @RISCV_IOMMU_CAP_IGS_WSI: I/O MMU supports only Wired-Signaled interrupt
 * @RISCV_IOMMU_CAP_IGS_BOTH: I/O MMU supports both MSI and WSI generation
 * @RISCV_IOMMU_CAP_IGS_RSRV: Reserved for standard use
 */
enum riscv_iommu_igs_settings {
      RISCV_IOMMU_CAP_IGS_MSI  = 0,
      RISCV_IOMMU_CAP_IGS_WSI  = 1,
      RISCV_IOMMU_CAP_IGS_BOTH = 2,
      RISCV_IOMMU_CAP_IGS_RSRV = 3
};


/* 5.4 Features control register (32bits) */
#define RISCV_IOMMU_REG_FCTL            0x0008
#define RISCV_IOMMU_FCTL_BE             BIT(0)
#define RISCV_IOMMU_FCTL_WSI            BIT(1)
#define RISCV_IOMMU_FCTL_GXL            BIT(2)


/* 5.5 Device-directory-table pointer (64bits) */
#define RISCV_IOMMU_REG_DDTP            0x0010
#define RISCV_IOMMU_DDTP_MODE           GENMASK_ULL(3, 0)
#define RISCV_IOMMU_DDTP_BUSY           BIT_ULL(4)
#define RISCV_IOMMU_DDTP_PPN            RISCV_IOMMU_PPN_FIELD

/**
 * enum riscv_iommu_ddtp_modes - I/O MMU translation modes
 * @RISCV_IOMMU_DDTP_MODE_OFF: No inbound transactions allowed
 * @RISCV_IOMMU_DDTP_MODE_BARE: Pass-through mode
 * @RISCV_IOMMU_DDTP_MODE_1LVL: One-level DDT
 * @RISCV_IOMMU_DDTP_MODE_2LVL: Two-level DDT
 * @RISCV_IOMMU_DDTP_MODE_3LVL: Three-level DDT
 */
enum riscv_iommu_ddtp_modes {
      RISCV_IOMMU_DDTP_MODE_OFF = 0,
      RISCV_IOMMU_DDTP_MODE_BARE = 1,
      RISCV_IOMMU_DDTP_MODE_1LVL = 2,
      RISCV_IOMMU_DDTP_MODE_2LVL = 3,
      RISCV_IOMMU_DDTP_MODE_3LVL = 4,
      RISCV_IOMMU_DDTP_MODE_MAX = 4
};


/* 5.6 Command Queue Base (64bits) */
#define RISCV_IOMMU_REG_CQB             0x0018
#define RISCV_IOMMU_CQB_LOG2SZ          RISCV_IOMMU_QUEUE_LOGSZ_FIELD
#define RISCV_IOMMU_CQB_PPN             RISCV_IOMMU_PPN_FIELD

/* 5.7 Command Queue head (32bits) */
#define RISCV_IOMMU_REG_CQH             0x0020
#define RISCV_IOMMU_CQH_INDEX           RISCV_IOMMU_QUEUE_INDEX_FIELD

/* 5.8 Command Queue tail (32bits) */
#define RISCV_IOMMU_REG_CQT             0x0024
#define RISCV_IOMMU_CQT_INDEX           RISCV_IOMMU_QUEUE_INDEX_FIELD


/* 5.9 Fault Queue Base (64bits) */
#define RISCV_IOMMU_REG_FQB             0x0028
#define RISCV_IOMMU_FQB_LOG2SZ          RISCV_IOMMU_QUEUE_LOGSZ_FIELD
#define RISCV_IOMMU_FQB_PPN             RISCV_IOMMU_PPN_FIELD

/* 5.10 Fault Queue Head (32bits) */
#define RISCV_IOMMU_REG_FQH             0x0030
#define RISCV_IOMMU_FQH_INDEX           RISCV_IOMMU_QUEUE_INDEX_FIELD

/* 5.11 Fault Queue tail (32bits) */
#define RISCV_IOMMU_REG_FQT             0x0034
#define RISCV_IOMMU_FQT_INDEX           RISCV_IOMMU_QUEUE_INDEX_FIELD


/* 5.12 Page Request Queue base (64bits) */
#define RISCV_IOMMU_REG_PQB             0x0038
#define RISCV_IOMMU_PQB_LOG2SZ          RISCV_IOMMU_QUEUE_LOGSZ_FIELD
#define RISCV_IOMMU_PQB_PPN             RISCV_IOMMU_PPN_FIELD

/* 5.13 Page Request Queue head (32bits) */
#define RISCV_IOMMU_REG_PQH             0x0040
#define RISCV_IOMMU_PQH_INDEX           RISCV_IOMMU_QUEUE_INDEX_FIELD

/* 5.14 Page Request Queue tail (32bits) */
#define RISCV_IOMMU_REG_PQT             0x0044
#define RISCV_IOMMU_PQT_INDEX_MASK      RISCV_IOMMU_QUEUE_INDEX_FIELD

/* 5.15 Command Queue CSR (32bits) */
#define RISCV_IOMMU_REG_CQCSR           0x0048
#define RISCV_IOMMU_CQCSR_CQEN          RISCV_IOMMU_QUEUE_ENABLE
#define RISCV_IOMMU_CQCSR_CIE           RISCV_IOMMU_QUEUE_INTR_ENABLE
#define RISCV_IOMMU_CQCSR_CQMF          RISCV_IOMMU_QUEUE_MEM_FAULT
#define RISCV_IOMMU_CQCSR_CMD_TO        BIT(9)
#define RISCV_IOMMU_CQCSR_CMD_ILL       BIT(10)
#define RISCV_IOMMU_CQCSR_FENCE_W_IP    BIT(11)
#define RISCV_IOMMU_CQCSR_CQON          RISCV_IOMMU_QUEUE_ACTIVE
#define RISCV_IOMMU_CQCSR_BUSY          RISCV_IOMMU_QUEUE_BUSY


/* 5.16 Fault Queue CSR (32bits) */
#define RISCV_IOMMU_REG_FQCSR           0x004C
#define RISCV_IOMMU_FQCSR_FQEN          RISCV_IOMMU_QUEUE_ENABLE
#define RISCV_IOMMU_FQCSR_FIE           RISCV_IOMMU_QUEUE_INTR_ENABLE
#define RISCV_IOMMU_FQCSR_FQMF          RISCV_IOMMU_QUEUE_MEM_FAULT
#define RISCV_IOMMU_FQCSR_FQOF          RISCV_IOMMU_QUEUE_OVERFLOW
#define RISCV_IOMMU_FQCSR_FQON          RISCV_IOMMU_QUEUE_ACTIVE
#define RISCV_IOMMU_FQCSR_BUSY          RISCV_IOMMU_QUEUE_BUSY


/* 5.17 Page Request Queue CSR (32bits) */
#define RISCV_IOMMU_REG_PQCSR           0x0050
#define RISCV_IOMMU_PQCSR_PQEN          RISCV_IOMMU_QUEUE_ENABLE
#define RISCV_IOMMU_PQCSR_PIE           RISCV_IOMMU_QUEUE_INTR_ENABLE
#define RISCV_IOMMU_PQCSR_PQMF          RISCV_IOMMU_QUEUE_MEM_FAULT
#define RISCV_IOMMU_PQCSR_PQOF          RISCV_IOMMU_QUEUE_OVERFLOW
#define RISCV_IOMMU_PQCSR_PQON          RISCV_IOMMU_QUEUE_ACTIVE
#define RISCV_IOMMU_PQCSR_BUSY          RISCV_IOMMU_QUEUE_BUSY


/* 5.18 Interrupt Pending Status (32bits) */
#define RISCV_IOMMU_REG_IPSR            0x0054

#define RISCV_IOMMU_INTR_CQ             0
#define RISCV_IOMMU_INTR_FQ             1
#define RISCV_IOMMU_INTR_PM             2
#define RISCV_IOMMU_INTR_PQ             3
#define RISCV_IOMMU_INTR_COUNT          4

#define RISCV_IOMMU_IPSR_CIP            BIT(RISCV_IOMMU_INTR_CQ)
#define RISCV_IOMMU_IPSR_FIP            BIT(RISCV_IOMMU_INTR_FQ)
#define RISCV_IOMMU_IPSR_PMIP           BIT(RISCV_IOMMU_INTR_PM)
#define RISCV_IOMMU_IPSR_PIP            BIT(RISCV_IOMMU_INTR_PQ)

#define RISCV_IOMMU_IOCOUNT_NUM         31

/* 5.19 Performance monitoring counter overflow status (32bits) */
#define RISCV_IOMMU_REG_IOCOUNTOVF      0x0058
#define RISCV_IOMMU_IOCOUNTOVF_CY       BIT(0)
#define RISCV_IOMMU_IOCOUNTOVF_HPM      GENMASK(31, 1)

/* 5.20 Performance monitoring counter inhibits (32bits) */
#define RISCV_IOMMU_REG_IOCOUNTINH      0x005C
#define RISCV_IOMMU_IOCOUNTINH_CY       BIT(0)
#define RISCV_IOMMU_IOCOUNTINH_HPM      GENMASK(31, 1)

/* 5.21 Performance monitoring cycles counter (64bits) */
#define RISCV_IOMMU_REG_IOHPMCYCLES     0x0060
#define RISCV_IOMMU_IOHPMCYCLES_COUNTER GENMASK_ULL(62, 0)
#define RISCV_IOMMU_IOHPMCYCLES_OVF     BIT_ULL(63)

/* 5.22 Performance monitoring event counters (31 * 64bits) */
#define RISCV_IOMMU_REG_IOHPMCTR_BASE   0x0068
#define RISCV_IOMMU_REG_IOHPMCTR(_n)    \
    (RISCV_IOMMU_REG_IOHPMCTR_BASE + (_n * 0x8))

/* 5.23 Performance monitoring event selectors (31 * 64bits) */
#define RISCV_IOMMU_REG_IOHPMEVT_BASE   0x0160
#define RISCV_IOMMU_REG_IOHPMEVT(_n)    \
    (RISCV_IOMMU_REG_IOHPMEVT_BASE + (_n * 0x8))
#define RISCV_IOMMU_IOHPMEVT_EVENT_ID   GENMASK_ULL(14, 0)
#define RISCV_IOMMU_IOHPMEVT_DMASK      BIT_ULL(15)
#define RISCV_IOMMU_IOHPMEVT_PID_PSCID  GENMASK_ULL(35, 16)
#define RISCV_IOMMU_IOHPMEVT_DID_GSCID  GENMASK_ULL(59, 36)
#define RISCV_IOMMU_IOHPMEVT_PV_PSCV    BIT_ULL(60)
#define RISCV_IOMMU_IOHPMEVT_DV_GSCV    BIT_ULL(61)
#define RISCV_IOMMU_IOHPMEVT_IDT        BIT_ULL(62)
#define RISCV_IOMMU_IOHPMEVT_OF         BIT_ULL(63)

/**
 * enum RISCV_IOMMU_HPMEVENT_id - Performance-monitoring event identifier
 *
 * @RISCV_IOMMU_HPMEVENT_INVALID: Invalid event, do not count
 * @RISCV_IOMMU_HPMEVENT_URQ: Untranslated requests
 * @RISCV_IOMMU_HPMEVENT_TRQ: Translated requests
 * @RISCV_IOMMU_HPMEVENT_ATS_RQ: ATS translation requests
 * @RISCV_IOMMU_HPMEVENT_TLB_MISS: TLB misses
 * @RISCV_IOMMU_HPMEVENT_DD_WALK: Device directory walks
 * @RISCV_IOMMU_HPMEVENT_PD_WALK: Process directory walks
 * @RISCV_IOMMU_HPMEVENT_S_VS_WALKS: S/VS-Stage page table walks
 * @RISCV_IOMMU_HPMEVENT_G_WALKS: G-Stage page table walks
 * @RISCV_IOMMU_HPMEVENT_MAX: Value to denote maximum Event IDs
 */
enum RISCV_IOMMU_HPMEVENT_id {
    RISCV_IOMMU_HPMEVENT_INVALID    = 0,
    RISCV_IOMMU_HPMEVENT_URQ        = 1,
    RISCV_IOMMU_HPMEVENT_TRQ        = 2,
    RISCV_IOMMU_HPMEVENT_ATS_RQ     = 3,
    RISCV_IOMMU_HPMEVENT_TLB_MISS   = 4,
    RISCV_IOMMU_HPMEVENT_DD_WALK    = 5,
    RISCV_IOMMU_HPMEVENT_PD_WALK    = 6,
    RISCV_IOMMU_HPMEVENT_S_VS_WALKS = 7,
    RISCV_IOMMU_HPMEVENT_G_WALKS    = 8,
    RISCV_IOMMU_HPMEVENT_MAX        = 9
};

/* 5.24 Translation request IOVA (64bits) */
#define RISCV_IOMMU_REG_TR_REQ_IOVA     0x0258
#define RISCV_IOMMU_TR_REQ_IOVA_VPN     GENMASK_ULL(63, 12)

/* 5.25 Translation request control (64bits) */
#define RISCV_IOMMU_REG_TR_REQ_CTL      0x0260
#define RISCV_IOMMU_TR_REQ_CTL_GO_BUSY  BIT_ULL(0)
#define RISCV_IOMMU_TR_REQ_CTL_PRIV     BIT_ULL(1)
#define RISCV_IOMMU_TR_REQ_CTL_EXE      BIT_ULL(2)
#define RISCV_IOMMU_TR_REQ_CTL_NW       BIT_ULL(3)
#define RISCV_IOMMU_TR_REQ_CTL_PID      GENMASK_ULL(31, 12)
#define RISCV_IOMMU_TR_REQ_CTL_PV       BIT_ULL(32)
#define RISCV_IOMMU_TR_REQ_CTL_DID      GENMASK_ULL(63, 40)

/* 5.26 Translation request response (64bits) */
#define RISCV_IOMMU_REG_TR_RESPONSE     0x0268
#define RISCV_IOMMU_TR_RESPONSE_FAULT   BIT_ULL(0)
#define RISCV_IOMMU_TR_RESPONSE_PBMT    GENMASK_ULL(8, 7)
#define RISCV_IOMMU_TR_RESPONSE_SZ      BIT_ULL(9)
#define RISCV_IOMMU_TR_RESPONSE_PPN     RISCV_IOMMU_PPN_FIELD


/* 5.27 Interrupt cause to vector (64bits) */
#define RISCV_IOMMU_REG_IVEC            0x02F8
#define RISCV_IOMMU_IVEC_CIV            GENMASK_ULL(3, 0)
#define RISCV_IOMMU_IVEC_FIV            GENMASK_ULL(7, 4)
#define RISCV_IOMMU_IVEC_PMIV           GENMASK_ULL(11, 8)
#define RISCV_IOMMU_IVEC_PIV            GENMASK_ULL(15, 12)


/* 5.28 MSI Configuration table (32 * 64bits) */
#define RISCV_IOMMU_REG_MSI_CONFIG      0x0300
#define RISCV_IOMMU_REG_MSI_ADDR(_n)    \
    (RISCV_IOMMU_REG_MSI_CONFIG + (_n * 0x10))
#define RISCV_IOMMU_MSI_ADDR            GENMASK_ULL(55, 2)
#define RISCV_IOMMU_REG_MSI_DATA(_n)    \
    (RISCV_IOMMU_REG_MSI_CONFIG + (_n * 0x10) + 0x08)
#define RISCV_IOMMU_MSI_DATA            GENMASK_ULL(31, 0)
#define RISCV_IOMMU_REG_MSI_VEC_CTL(_n) \
    (RISCV_IOMMU_REG_MSI_CONFIG + (_n * 0x10) + 0x0C)
#define RISCV_IOMMU_MSI_VEC_CTL_M      BIT_ULL(0)


#define RISCV_IOMMU_REG_SIZE           0x1000

/*
 * Chapter 2: Data structures
 */

/*
 * Device Directory Table macros for non-leaf nodes
 */
#define RISCV_IOMMU_DDTE_VALID          BIT_ULL(0)
#define RISCV_IOMMU_DDTE_PPN            RISCV_IOMMU_PPN_FIELD

/**
 * struct riscv_iommu_dc - Device Context
 * @tc: Translation Control
 * @iohgatp: I/O Hypervisor guest address translation and protection
 *           (Second stage context)
 * @ta: Translation Attributes
 * @fsc: First stage context
 * @msiptpt: MSI page table pointer
 * @msi_addr_mask: MSI address mask
 * @msi_addr_pattern: MSI address pattern
 *
 * This structure is used for leaf nodes on the Device Directory Table,
 * in case RISCV_IOMMU_CAP_MSI_FLAT is not set, the bottom 4 fields are
 * not present and are skipped with pointer arithmetic to avoid
 * casting, check out riscv_iommu_get_dc().
 * See section 2.1 for more details
 */
struct riscv_iommu_dc {
      uint64_t tc;
      uint64_t iohgatp;
      uint64_t ta;
      uint64_t fsc;
      uint64_t msiptp;
      uint64_t msi_addr_mask;
      uint64_t msi_addr_pattern;
      uint64_t _reserved;
};

/* Translation control fields */
#define RISCV_IOMMU_DC_TC_V             BIT_ULL(0)
#define RISCV_IOMMU_DC_TC_EN_ATS        BIT_ULL(1)
#define RISCV_IOMMU_DC_TC_EN_PRI        BIT_ULL(2)
#define RISCV_IOMMU_DC_TC_T2GPA         BIT_ULL(3)
#define RISCV_IOMMU_DC_TC_DTF           BIT_ULL(4)
#define RISCV_IOMMU_DC_TC_PDTV          BIT_ULL(5)
#define RISCV_IOMMU_DC_TC_PRPR          BIT_ULL(6)
#define RISCV_IOMMU_DC_TC_GADE          BIT_ULL(7)
#define RISCV_IOMMU_DC_TC_SADE          BIT_ULL(8)
#define RISCV_IOMMU_DC_TC_DPE           BIT_ULL(9)
#define RISCV_IOMMU_DC_TC_SBE           BIT_ULL(10)
#define RISCV_IOMMU_DC_TC_SXL           BIT_ULL(11)

/* Second-stage (aka G-stage) context fields */
#define RISCV_IOMMU_DC_IOHGATP_PPN      RISCV_IOMMU_ATP_PPN_FIELD
#define RISCV_IOMMU_DC_IOHGATP_GSCID    GENMASK_ULL(59, 44)
#define RISCV_IOMMU_DC_IOHGATP_MODE     RISCV_IOMMU_ATP_MODE_FIELD

/**
 * enum riscv_iommu_dc_iohgatp_modes - Guest address
 * translation/protection modes
 *
 * @RISCV_IOMMU_DC_IOHGATP_MODE_BARE:
 *      No translation/protection
 * @RISCV_IOMMU_DC_IOHGATP_MODE_SV32X4:
 *      Sv32x4 (2-bit extension of Sv32), when fctl.GXL == 1
 * @RISCV_IOMMU_DC_IOHGATP_MODE_SV39X4:
 *      Sv39x4 (2-bit extension of Sv39), when fctl.GXL == 0
 * @RISCV_IOMMU_DC_IOHGATP_MODE_SV48X4:
 *      Sv48x4 (2-bit extension of Sv48), when fctl.GXL == 0
 * @RISCV_IOMMU_DC_IOHGATP_MODE_SV57X4:
 *      Sv57x4 (2-bit extension of Sv57), when fctl.GXL == 0
 */
enum riscv_iommu_dc_iohgatp_modes {
      RISCV_IOMMU_DC_IOHGATP_MODE_BARE = 0,
      RISCV_IOMMU_DC_IOHGATP_MODE_SV32X4 = 8,
      RISCV_IOMMU_DC_IOHGATP_MODE_SV39X4 = 8,
      RISCV_IOMMU_DC_IOHGATP_MODE_SV48X4 = 9,
      RISCV_IOMMU_DC_IOHGATP_MODE_SV57X4 = 10
};

/* Translation attributes fields */
#define RISCV_IOMMU_DC_TA_PSCID         GENMASK_ULL(31, 12)

/* First-stage context fields */
#define RISCV_IOMMU_DC_FSC_PPN          RISCV_IOMMU_ATP_PPN_FIELD
#define RISCV_IOMMU_DC_FSC_MODE         RISCV_IOMMU_ATP_MODE_FIELD

/**
 * enum riscv_iommu_dc_fsc_atp_modes - First stage address
 * translation/protection modes
 *
 * @RISCV_IOMMU_DC_FSC_MODE_BARE: No translation/protection
 * @RISCV_IOMMU_DC_FSC_IOSATP_MODE_SV32: Sv32, when dc.tc.SXL == 1
 * @RISCV_IOMMU_DC_FSC_IOSATP_MODE_SV39: Sv39, when dc.tc.SXL == 0
 * @RISCV_IOMMU_DC_FSC_IOSATP_MODE_SV48: Sv48, when dc.tc.SXL == 0
 * @RISCV_IOMMU_DC_FSC_IOSATP_MODE_SV57: Sv57, when dc.tc.SXL == 0
 * @RISCV_IOMMU_DC_FSC_PDTP_MODE_PD8: 1lvl PDT, 8bit process ids
 * @RISCV_IOMMU_DC_FSC_PDTP_MODE_PD17: 2lvl PDT, 17bit process ids
 * @RISCV_IOMMU_DC_FSC_PDTP_MODE_PD20: 3lvl PDT, 20bit process ids
 *
 * FSC holds IOSATP when RISCV_IOMMU_DC_TC_PDTV is 0 and PDTP otherwise.
 * IOSATP controls the first stage address translation (same as the satp
 * register on the RISC-V MMU), and PDTP holds the process directory table,
 * used to select a first stage page table based on a process id (for devices
 * that support multiple process ids).
 */
enum riscv_iommu_dc_fsc_atp_modes {
      RISCV_IOMMU_DC_FSC_MODE_BARE = 0,
      RISCV_IOMMU_DC_FSC_IOSATP_MODE_SV32 = 8,
      RISCV_IOMMU_DC_FSC_IOSATP_MODE_SV39 = 8,
      RISCV_IOMMU_DC_FSC_IOSATP_MODE_SV48 = 9,
      RISCV_IOMMU_DC_FSC_IOSATP_MODE_SV57 = 10,
      RISCV_IOMMU_DC_FSC_PDTP_MODE_PD8 = 1,
      RISCV_IOMMU_DC_FSC_PDTP_MODE_PD17 = 2,
      RISCV_IOMMU_DC_FSC_PDTP_MODE_PD20 = 3
};

/* MSI page table pointer */
#define RISCV_IOMMU_DC_MSIPTP_PPN       RISCV_IOMMU_ATP_PPN_FIELD
#define RISCV_IOMMU_DC_MSIPTP_MODE      RISCV_IOMMU_ATP_MODE_FIELD
#define RISCV_IOMMU_DC_MSIPTP_MODE_OFF  0
#define RISCV_IOMMU_DC_MSIPTP_MODE_FLAT 1

/* MSI address mask */
#define RISCV_IOMMU_DC_MSI_ADDR_MASK    GENMASK_ULL(51, 0)

/* MSI address pattern */
#define RISCV_IOMMU_DC_MSI_PATTERN      GENMASK_ULL(51, 0)


/**
 * struct riscv_iommu_pc - Process Context
 * @ta: Translation Attributes
 * @fsc: First stage context
 *
 * This structure is used for leaf nodes on the Process Directory Table
 * See section 2.3 for more details
 */
struct riscv_iommu_pc {
      uint64_t ta;
      uint64_t fsc;
};

/* Translation attributes fields */
#define RISCV_IOMMU_PC_TA_V             BIT_ULL(0)
#define RISCV_IOMMU_PC_TA_ENS           BIT_ULL(1)
#define RISCV_IOMMU_PC_TA_SUM           BIT_ULL(2)
#define RISCV_IOMMU_PC_TA_PSCID         GENMASK_ULL(31, 12)

/* First stage context fields */
#define RISCV_IOMMU_PC_FSC_PPN          GENMASK_ULL(43, 0)
#define RISCV_IOMMU_PC_FSC_MODE         GENMASK_ULL(63, 60)


/*
 * Chapter 3: In-memory queue interface
 */

/**
 * struct riscv_iommu_cmd - Generic I/O MMU command structure
 * @dword0: Includes the opcode and the function identifier
 * @dword1: Opcode specific data
 *
 * The commands are interpreted as two 64bit fields, where the first
 * 7bits of the first field are the opcode which also defines the
 * command's format, followed by a 3bit field that specifies the
 * function invoked by that command, and the rest is opcode-specific.
 * This is a generic struct which will be populated differently
 * according to each command. For more infos on the commands and
 * the command queue check section 3.1.
 */
struct riscv_iommu_command {
      uint64_t dword0;
      uint64_t dword1;
};

/* Fields on dword0, common for all commands */
#define RISCV_IOMMU_CMD_OPCODE          GENMASK_ULL(6, 0)
#define RISCV_IOMMU_CMD_FUNC            GENMASK_ULL(9, 7)

/* 3.1.1 I/O MMU Page-table cache invalidation */
/* Fields on dword0 */
#define RISCV_IOMMU_CMD_IOTINVAL_OPCODE         1
#define RISCV_IOMMU_CMD_IOTINVAL_FUNC_VMA       0
#define RISCV_IOMMU_CMD_IOTINVAL_FUNC_GVMA      1
#define RISCV_IOMMU_CMD_IOTINVAL_AV     BIT_ULL(10)
#define RISCV_IOMMU_CMD_IOTINVAL_PSCID  GENMASK_ULL(31, 12)
#define RISCV_IOMMU_CMD_IOTINVAL_PSCV   BIT_ULL(32)
#define RISCV_IOMMU_CMD_IOTINVAL_GV     BIT_ULL(33)
#define RISCV_IOMMU_CMD_IOTINVAL_GSCID  GENMASK_ULL(59, 44)
/* dword1 is the address, 4K-alligned and shifted to the right by two bits. */

/* 3.1.2 I/O MMU Command Queue Fences */
/* Fields on dword0 */
#define RISCV_IOMMU_CMD_IOFENCE_OPCODE          2
#define RISCV_IOMMU_CMD_IOFENCE_FUNC_C          0
#define RISCV_IOMMU_CMD_IOFENCE_AV      BIT_ULL(10)
#define RISCV_IOMMU_CMD_IOFENCE_WSI     BIT_ULL(11)
#define RISCV_IOMMU_CMD_IOFENCE_PR      BIT_ULL(12)
#define RISCV_IOMMU_CMD_IOFENCE_PW      BIT_ULL(13)
#define RISCV_IOMMU_CMD_IOFENCE_DATA    GENMASK_ULL(63, 32)
/* dword1 is the address, word-size alligned and shifted to the right by two bits. */

/* 3.1.3 I/O MMU Directory cache invalidation */
/* Fields on dword0 */
#define RISCV_IOMMU_CMD_IODIR_OPCODE            3
#define RISCV_IOMMU_CMD_IODIR_FUNC_INVAL_DDT    0
#define RISCV_IOMMU_CMD_IODIR_FUNC_INVAL_PDT    1
#define RISCV_IOMMU_CMD_IODIR_PID       GENMASK_ULL(31, 12)
#define RISCV_IOMMU_CMD_IODIR_DV        BIT_ULL(33)
#define RISCV_IOMMU_CMD_IODIR_DID       GENMASK_ULL(63, 40)
/* dword1 is reserved for standard use */

/* 3.1.4 I/O MMU PCIe ATS */
/* Fields on dword0 */
#define RISCV_IOMMU_CMD_ATS_OPCODE              4
#define RISCV_IOMMU_CMD_ATS_FUNC_INVAL          0
#define RISCV_IOMMU_CMD_ATS_FUNC_PRGR           1
#define RISCV_IOMMU_CMD_ATS_PID         GENMASK_ULL(31, 12)
#define RISCV_IOMMU_CMD_ATS_PV          BIT_ULL(32)
#define RISCV_IOMMU_CMD_ATS_DSV         BIT_ULL(33)
#define RISCV_IOMMU_CMD_ATS_RID         GENMASK_ULL(55, 40)
#define RISCV_IOMMU_CMD_ATS_DSEG        GENMASK_ULL(63, 56)
/* dword1 is the ATS payload, two different payload types for INVAL and PRGR */

/* ATS.INVAL payload*/
#define RISCV_IOMMU_CMD_ATS_INVAL_G     BIT_ULL(0)
/* Bits 1 - 10 are zeroed */
#define RISCV_IOMMU_CMD_ATS_INVAL_S     BIT_ULL(11)
#define RISCV_IOMMU_CMD_ATS_INVAL_UADDR GENMASK_ULL(63, 12)

/* ATS.PRGR payload */
/* Bits 0 - 31 are zeroed */
#define RISCV_IOMMU_CMD_ATS_PRGR_PRG_INDEX      GENMASK_ULL(40, 32)
/* Bits 41 - 43 are zeroed */
#define RISCV_IOMMU_CMD_ATS_PRGR_RESP_CODE      GENMASK_ULL(47, 44)
#define RISCV_IOMMU_CMD_ATS_PRGR_DST_ID         GENMASK_ULL(63, 48)


/**
 * struct riscv_iommu_fq_record - Fault/Event Queue Record
 * @hdr: Header, includes fault/event cause, PID/DID, transaction type etc
 * @_reserved: Low 32bits for custom use, high 32bits for standard use
 * @iotval: Transaction-type/cause specific format
 * @iotval2: Cause specific format
 *
 * The fault/event queue reports events and failures raised when
 * processing transactions. Each record is a 32byte structure where
 * the first dword has a fixed format for providing generic infos
 * regarding the fault/event, and two more dwords are there for
 * fault/event-specific information. For more details see section
 * 3.2.
 */
struct riscv_iommu_fq_record {
      uint64_t hdr;
      uint64_t _reserved;
      uint64_t iotval;
      uint64_t iotval2;
};

/* Fields on header */
#define RISCV_IOMMU_FQ_HDR_CAUSE        GENMASK_ULL(11, 0)
#define RISCV_IOMMU_FQ_HDR_PID          GENMASK_ULL(31, 12)
#define RISCV_IOMMU_FQ_HDR_PV           BIT_ULL(32)
#define RISCV_IOMMU_FQ_HDR_PRIV         BIT_ULL(33)
#define RISCV_IOMMU_FQ_HDR_TTYPE        GENMASK_ULL(39, 34)
#define RISCV_IOMMU_FQ_HDR_DID          GENMASK_ULL(63, 40)

/**
 * enum riscv_iommu_fq_causes - Fault/event cause values
 * @RISCV_IOMMU_FQ_CAUSE_INST_FAULT: Instruction access fault
 * @RISCV_IOMMU_FQ_CAUSE_RD_ADDR_MISALIGNED: Read address misaligned
 * @RISCV_IOMMU_FQ_CAUSE_RD_FAULT: Read load fault
 * @RISCV_IOMMU_FQ_CAUSE_WR_ADDR_MISALIGNED: Write/AMO address misaligned
 * @RISCV_IOMMU_FQ_CAUSE_WR_FAULT: Write/AMO access fault
 * @RISCV_IOMMU_FQ_CAUSE_INST_FAULT_S: Instruction page fault
 * @RISCV_IOMMU_FQ_CAUSE_RD_FAULT_S: Read page fault
 * @RISCV_IOMMU_FQ_CAUSE_WR_FAULT_S: Write/AMO page fault
 * @RISCV_IOMMU_FQ_CAUSE_INST_FAULT_VS: Instruction guest page fault
 * @RISCV_IOMMU_FQ_CAUSE_RD_FAULT_VS: Read guest page fault
 * @RISCV_IOMMU_FQ_CAUSE_WR_FAULT_VS: Write/AMO guest page fault
 * @RISCV_IOMMU_FQ_CAUSE_DMA_DISABLED: All inbound transactions disallowed
 * @RISCV_IOMMU_FQ_CAUSE_DDT_LOAD_FAULT: DDT entry load access fault
 * @RISCV_IOMMU_FQ_CAUSE_DDT_INVALID: DDT entry invalid
 * @RISCV_IOMMU_FQ_CAUSE_DDT_MISCONFIGURED: DDT entry misconfigured
 * @RISCV_IOMMU_FQ_CAUSE_TTYPE_BLOCKED: Transaction type disallowed
 * @RISCV_IOMMU_FQ_CAUSE_MSI_LOAD_FAULT: MSI PTE load access fault
 * @RISCV_IOMMU_FQ_CAUSE_MSI_INVALID: MSI PTE invalid
 * @RISCV_IOMMU_FQ_CAUSE_MSI_MISCONFIGURED: MSI PTE misconfigured
 * @RISCV_IOMMU_FQ_CAUSE_MRIF_FAULT: MRIF access fault
 * @RISCV_IOMMU_FQ_CAUSE_PDT_LOAD_FAULT: PDT entry load access fault
 * @RISCV_IOMMU_FQ_CAUSE_PDT_INVALID: PDT entry invalid
 * @RISCV_IOMMU_FQ_CAUSE_PDT_MISCONFIGURED: PDT entry misconfigured
 * @RISCV_IOMMU_FQ_CAUSE_DDT_CORRUPTED: DDT data corruption
 * @RISCV_IOMMU_FQ_CAUSE_PDT_CORRUPTED: PDT data corruption
 * @RISCV_IOMMU_FQ_CAUSE_MSI_PT_CORRUPTED: MSI page table data corruption
 * @RISCV_IOMMU_FQ_CAUSE_MRIF_CORRUIPTED: MRIF data corruption
 * @RISCV_IOMMU_FQ_CAUSE_INTERNAL_DP_ERROR: Internal data path error
 * @RISCV_IOMMU_FQ_CAUSE_MSI_WR_FAULT: IOMMU MSI write access fault
 * @RISCV_IOMMU_FQ_CAUSE_PT_CORRUPTED: First/second stage page table data corruption
 *
 * Values are on table 11 of the spec, encodings 275 - 2047 are reserved for
 * standard use, and 2048 - 4095 for custom use.
 */
enum riscv_iommu_fq_causes {
      RISCV_IOMMU_FQ_CAUSE_INST_FAULT           = 1,
      RISCV_IOMMU_FQ_CAUSE_RD_ADDR_MISALIGNED   = 4,
      RISCV_IOMMU_FQ_CAUSE_RD_FAULT             = 5,
      RISCV_IOMMU_FQ_CAUSE_WR_ADDR_MISALIGNED   = 6,
      RISCV_IOMMU_FQ_CAUSE_WR_FAULT             = 7,
      RISCV_IOMMU_FQ_CAUSE_INST_FAULT_S         = 12,
      RISCV_IOMMU_FQ_CAUSE_RD_FAULT_S           = 13,
      RISCV_IOMMU_FQ_CAUSE_WR_FAULT_S           = 15,
      RISCV_IOMMU_FQ_CAUSE_INST_FAULT_VS        = 20,
      RISCV_IOMMU_FQ_CAUSE_RD_FAULT_VS          = 21,
      RISCV_IOMMU_FQ_CAUSE_WR_FAULT_VS          = 23,
      RISCV_IOMMU_FQ_CAUSE_DMA_DISABLED         = 256,
      RISCV_IOMMU_FQ_CAUSE_DDT_LOAD_FAULT       = 257,
      RISCV_IOMMU_FQ_CAUSE_DDT_INVALID          = 258,
      RISCV_IOMMU_FQ_CAUSE_DDT_MISCONFIGURED    = 259,
      RISCV_IOMMU_FQ_CAUSE_TTYPE_BLOCKED        = 260,
      RISCV_IOMMU_FQ_CAUSE_MSI_LOAD_FAULT       = 261,
      RISCV_IOMMU_FQ_CAUSE_MSI_INVALID          = 262,
      RISCV_IOMMU_FQ_CAUSE_MSI_MISCONFIGURED    = 263,
      RISCV_IOMMU_FQ_CAUSE_MRIF_FAULT           = 264,
      RISCV_IOMMU_FQ_CAUSE_PDT_LOAD_FAULT       = 265,
      RISCV_IOMMU_FQ_CAUSE_PDT_INVALID          = 266,
      RISCV_IOMMU_FQ_CAUSE_PDT_MISCONFIGURED    = 267,
      RISCV_IOMMU_FQ_CAUSE_DDT_CORRUPTED        = 268,
      RISCV_IOMMU_FQ_CAUSE_PDT_CORRUPTED        = 269,
      RISCV_IOMMU_FQ_CAUSE_MSI_PT_CORRUPTED     = 270,
      RISCV_IOMMU_FQ_CAUSE_MRIF_CORRUIPTED      = 271,
      RISCV_IOMMU_FQ_CAUSE_INTERNAL_DP_ERROR    = 272,
      RISCV_IOMMU_FQ_CAUSE_MSI_WR_FAULT         = 273,
      RISCV_IOMMU_FQ_CAUSE_PT_CORRUPTED         = 274
};

/**
 * enum riscv_iommu_fq_ttypes: Fault/event transaction types
 * @RISCV_IOMMU_FQ_TTYPE_NONE: None. Fault not caused by an inbound transaction.
 * @RISCV_IOMMU_FQ_TTYPE_UADDR_INST_FETCH: Instruction fetch from untranslated address
 * @RISCV_IOMMU_FQ_TTYPE_UADDR_RD: Read from untranslated address
 * @RISCV_IOMMU_FQ_TTYPE_UADDR_WR: Write/AMO to untranslated address
 * @RISCV_IOMMU_FQ_TTYPE_TADDR_INST_FETCH: Instruction fetch from translated address
 * @RISCV_IOMMU_FQ_TTYPE_TADDR_RD: Read from translated address
 * @RISCV_IOMMU_FQ_TTYPE_TADDR_WR: Write/AMO to translated address
 * @RISCV_IOMMU_FQ_TTYPE_PCIE_ATS_REQ: PCIe ATS translation request
 * @RISCV_IOMMU_FW_TTYPE_PCIE_MSG_REQ: PCIe message request
 *
 * Values are on table 12 of the spec, type 4 and 10 - 31 are reserved for
 * standard use and 31 - 63 for custom use.
 */
enum riscv_iommu_fq_ttypes {
      RISCV_IOMMU_FQ_TTYPE_NONE = 0,
      RISCV_IOMMU_FQ_TTYPE_UADDR_INST_FETCH = 1,
      RISCV_IOMMU_FQ_TTYPE_UADDR_RD = 2,
      RISCV_IOMMU_FQ_TTYPE_UADDR_WR = 3,
      RISCV_IOMMU_FQ_TTYPE_TADDR_INST_FETCH = 5,
      RISCV_IOMMU_FQ_TTYPE_TADDR_RD = 6,
      RISCV_IOMMU_FQ_TTYPE_TADDR_WR = 7,
      RISCV_IOMMU_FQ_TTYPE_PCIE_ATS_REQ = 8,
      RISCV_IOMMU_FW_TTYPE_PCIE_MSG_REQ = 9,
};


/**
 * struct riscv_iommu_pq_record - PCIe Page Request record
 * @hdr: Header, includes PID, DID etc
 * @payload: Holds the page address, request group and permission bits
 *
 * For more infos on the PCIe Page Request queue see chapter 3.3.
 */
struct riscv_iommu_pq_record {
      uint64_t hdr;
      uint64_t payload;
};

/* Header fields */
#define RISCV_IOMMU_PREQ_HDR_PID        GENMASK_ULL(31, 12)
#define RISCV_IOMMU_PREQ_HDR_PV         BIT_ULL(32)
#define RISCV_IOMMU_PREQ_HDR_PRIV       BIT_ULL(33)
#define RISCV_IOMMU_PREQ_HDR_EXEC       BIT_ULL(34)
#define RISCV_IOMMU_PREQ_HDR_DID        GENMASK_ULL(63, 40)

/* Payload fields */
#define RISCV_IOMMU_PREQ_PAYLOAD_R      BIT_ULL(0)
#define RISCV_IOMMU_PREQ_PAYLOAD_W      BIT_ULL(1)
#define RISCV_IOMMU_PREQ_PAYLOAD_L      BIT_ULL(2)
#define RISCV_IOMMU_PREQ_PAYLOAD_M      GENMASK_ULL(2, 0)
#define RISCV_IOMMU_PREQ_PRG_INDEX      GENMASK_ULL(11, 3)
#define RISCV_IOMMU_PREQ_UADDR          GENMASK_ULL(63, 12)


/**
 * struct riscv_iommu_msi_pte - MSI Page Table Entry
 * @pte: MSI PTE
 * @mrif_info: Memory-resident interrupt file info
 *
 * The MSI Page Table is used for virtualizing MSIs, so that when
 * a device sends an MSI to a guest, the IOMMU can reroute it
 * by translating the MSI address, either to a guest interrupt file
 * or a memory resident interrupt file (MRIF). Note that this page table
 * is an array of MSI PTEs, not a multi-level pt, each entry
 * is a leaf entry. For more infos check out the the AIA spec, chapter 9.5.
 *
 * Also in basic mode the mrif_info field is ignored by the IOMMU and can
 * be used by software, any other reserved fields on pte must be zeroed-out
 * by software.
 */
struct riscv_iommu_msi_pte {
      uint64_t pte;
      uint64_t mrif_info;
};

/* Fields on pte */
#define RISCV_IOMMU_MSI_PTE_V           BIT_ULL(0)
#define RISCV_IOMMU_MSI_PTE_M           GENMASK_ULL(2, 1)

#define RISCV_IOMMU_MSI_PTE_M_MRIF      1
#define RISCV_IOMMU_MSI_PTE_M_BASIC     3

/* When M == 1 (MRIF mode) */
#define RISCV_IOMMU_MSI_PTE_MRIF_ADDR   GENMASK_ULL(53, 7)
/* When M == 3 (basic mode) */
#define RISCV_IOMMU_MSI_PTE_PPN         RISCV_IOMMU_PPN_FIELD
#define RISCV_IOMMU_MSI_PTE_C           BIT_ULL(63)

/* Fields on mrif_info */
#define RISCV_IOMMU_MSI_MRIF_NID        GENMASK_ULL(9, 0)
#define RISCV_IOMMU_MSI_MRIF_NPPN       RISCV_IOMMU_PPN_FIELD
#define RISCV_IOMMU_MSI_MRIF_NID_MSB    BIT_ULL(60)


#endif /* _RISCV_IOMMU_BITS_H_ */
