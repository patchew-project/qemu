/*
 * ARM SMMUv3 support - Internal API
 *
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

#ifndef HW_ARM_SMMU_V3_INTERNAL_H
#define HW_ARM_SMMU_V3_INTERNAL_H

#include "trace.h"
#include "qemu/error-report.h"
#include "hw/arm/smmu-common.h"

/*****************************
 * MMIO Register
 *****************************/
enum {
    SMMU_REG_IDR0            = 0x0,

/* IDR0 Field Values and supported features */

#define SMMU_IDR0_S2P      1  /* stage 2 */
#define SMMU_IDR0_S1P      1  /* stage 1 */
#define SMMU_IDR0_TTF      2  /* Aarch64 only - not Aarch32 (LPAE) */
#define SMMU_IDR0_COHACC   1  /* IO coherent access */
#define SMMU_IDR0_HTTU     2  /* Access and Dirty flag update */
#define SMMU_IDR0_HYP      0  /* Hypervisor Stage 1 contexts */
#define SMMU_IDR0_ATS      0  /* PCIe RC ATS */
#define SMMU_IDR0_ASID16   1  /* 16-bit ASID */
#define SMMU_IDR0_PRI      0  /* Page Request Interface */
#define SMMU_IDR0_VMID16   0  /* 16-bit VMID */
#define SMMU_IDR0_CD2L     0  /* 2-level Context Descriptor table */
#define SMMU_IDR0_STALL    1  /* Stalling fault model */
#define SMMU_IDR0_TERM     1  /* Termination model behaviour */
#define SMMU_IDR0_STLEVEL  1  /* Multi-level Stream Table */

#define SMMU_IDR0_S2P_SHIFT      0
#define SMMU_IDR0_S1P_SHIFT      1
#define SMMU_IDR0_TTF_SHIFT      2
#define SMMU_IDR0_COHACC_SHIFT   4
#define SMMU_IDR0_HTTU_SHIFT     6
#define SMMU_IDR0_HYP_SHIFT      9
#define SMMU_IDR0_ATS_SHIFT      10
#define SMMU_IDR0_ASID16_SHIFT   12
#define SMMU_IDR0_PRI_SHIFT      16
#define SMMU_IDR0_VMID16_SHIFT   18
#define SMMU_IDR0_CD2L_SHIFT     19
#define SMMU_IDR0_STALL_SHIFT    24
#define SMMU_IDR0_TERM_SHIFT     26
#define SMMU_IDR0_STLEVEL_SHIFT  27

    SMMU_REG_IDR1            = 0x4,
#define SMMU_IDR1_SIDSIZE 16
    SMMU_REG_IDR2            = 0x8,
    SMMU_REG_IDR3            = 0xc,
    SMMU_REG_IDR4            = 0x10,
    SMMU_REG_IDR5            = 0x14,
#define SMMU_IDR5_GRAN_SHIFT 4
#define SMMU_IDR5_GRAN       0b101 /* GRAN4K, GRAN64K */
#define SMMU_IDR5_OAS        4     /* 44 bits */
    SMMU_REG_IIDR            = 0x1c,
    SMMU_REG_CR0             = 0x20,

#define SMMU_CR0_SMMU_ENABLE (1 << 0)
#define SMMU_CR0_PRIQ_ENABLE (1 << 1)
#define SMMU_CR0_EVTQ_ENABLE (1 << 2)
#define SMMU_CR0_CMDQ_ENABLE (1 << 3)
#define SMMU_CR0_ATS_CHECK   (1 << 4)

    SMMU_REG_CR0_ACK         = 0x24,
    SMMU_REG_CR1             = 0x28,
    SMMU_REG_CR2             = 0x2c,

    SMMU_REG_STATUSR         = 0x40,

    SMMU_REG_IRQ_CTRL        = 0x50,
    SMMU_REG_IRQ_CTRL_ACK    = 0x54,

#define SMMU_IRQ_CTRL_GERROR_EN (1 << 0)
#define SMMU_IRQ_CTRL_EVENT_EN  (1 << 1)
#define SMMU_IRQ_CTRL_PRI_EN    (1 << 2)

    SMMU_REG_GERROR          = 0x60,

#define SMMU_GERROR_CMDQ           (1 << 0)
#define SMMU_GERROR_EVENTQ_ABT     (1 << 2)
#define SMMU_GERROR_PRIQ_ABT       (1 << 3)
#define SMMU_GERROR_MSI_CMDQ_ABT   (1 << 4)
#define SMMU_GERROR_MSI_EVENTQ_ABT (1 << 5)
#define SMMU_GERROR_MSI_PRIQ_ABT   (1 << 6)
#define SMMU_GERROR_MSI_GERROR_ABT (1 << 7)
#define SMMU_GERROR_SFM_ERR        (1 << 8)

    SMMU_REG_GERRORN         = 0x64,
    SMMU_REG_GERROR_IRQ_CFG0 = 0x68,
    SMMU_REG_GERROR_IRQ_CFG1 = 0x70,
    SMMU_REG_GERROR_IRQ_CFG2 = 0x74,

    /* SMMU_BASE_RA Applies to STRTAB_BASE, CMDQ_BASE and EVTQ_BASE */
#define SMMU_BASE_RA        (1ULL << 62)
    SMMU_REG_STRTAB_BASE     = 0x80,
    SMMU_REG_STRTAB_BASE_CFG = 0x88,

    SMMU_REG_CMDQ_BASE       = 0x90,
    SMMU_REG_CMDQ_PROD       = 0x98,
    SMMU_REG_CMDQ_CONS       = 0x9c,
    /* CMD Consumer (CONS) */
#define SMMU_CMD_CONS_ERR_SHIFT        24
#define SMMU_CMD_CONS_ERR_BITS         7

    SMMU_REG_EVTQ_BASE       = 0xa0,
    SMMU_REG_EVTQ_PROD       = 0xa8,
    SMMU_REG_EVTQ_CONS       = 0xac,
    SMMU_REG_EVTQ_IRQ_CFG0   = 0xb0,
    SMMU_REG_EVTQ_IRQ_CFG1   = 0xb8,
    SMMU_REG_EVTQ_IRQ_CFG2   = 0xbc,

    SMMU_REG_PRIQ_BASE       = 0xc0,
    SMMU_REG_PRIQ_PROD       = 0xc8,
    SMMU_REG_PRIQ_CONS       = 0xcc,
    SMMU_REG_PRIQ_IRQ_CFG0   = 0xd0,
    SMMU_REG_PRIQ_IRQ_CFG1   = 0xd8,
    SMMU_REG_PRIQ_IRQ_CFG2   = 0xdc,

    SMMU_ID_REGS_OFFSET      = 0xfd0,

    /* Secure registers are not used for now */
    SMMU_SECURE_OFFSET       = 0x8000,
};

/**********************
 * Data Structures
 **********************/

struct __smmu_data2 {
    uint32_t word[2];
};

struct __smmu_data8 {
    uint32_t word[8];
};

struct __smmu_data16 {
    uint32_t word[16];
};

struct __smmu_data4 {
    uint32_t word[4];
};

typedef struct __smmu_data4  Cmd; /* Command Entry */
typedef struct __smmu_data8  Evt; /* Event Entry */

/*****************************
 *  Register Access Primitives
 *****************************/

static inline void smmu_write32_reg(SMMUV3State *s, uint32_t addr, uint32_t val)
{
    s->regs[addr >> 2] = val;
}

static inline void smmu_write64_reg(SMMUV3State *s, uint32_t addr, uint64_t val)
{
    addr >>= 2;
    s->regs[addr] = extract64(val, 0, 32);
    s->regs[addr + 1] = extract64(val, 32, 32);
}

static inline uint32_t smmu_read32_reg(SMMUV3State *s, uint32_t addr)
{
    return s->regs[addr >> 2];
}

static inline uint64_t smmu_read64_reg(SMMUV3State *s, uint32_t addr)
{
    addr >>= 2;
    return s->regs[addr] | ((uint64_t)(s->regs[addr + 1]) << 32);
}

static inline int smmu_enabled(SMMUV3State *s)
{
    return smmu_read32_reg(s, SMMU_REG_CR0) & SMMU_CR0_SMMU_ENABLE;
}

/*****************************
 * Interrupts
 *****************************/

#define smmu_evt_irq_enabled(s)                   \
    (smmu_read64_reg(s, SMMU_REG_IRQ_CTRL) & SMMU_IRQ_CTRL_EVENT_EN)
#define smmu_gerror_irq_enabled(s)                  \
    (smmu_read64_reg(s, SMMU_REG_IRQ_CTRL) & SMMU_IRQ_CTRL_GERROR_EN)
#define smmu_pri_irq_enabled(s)                 \
    (smmu_read64_reg(s, SMMU_REG_IRQ_CTRL) & SMMU_IRQ_CTRL_PRI_EN)

#define SMMU_PENDING_GERRORS(s) \
    (smmu_read32_reg(s, SMMU_REG_GERROR) ^ \
     smmu_read32_reg(s, SMMU_REG_GERRORN))

#define SMMU_CMDQ_ERR(s) (SMMU_PENDING_GERRORS(s) & SMMU_GERROR_CMDQ)

/***************************
 * Queue Handling
 ***************************/

typedef enum {
    CMD_Q_EMPTY,
    CMD_Q_FULL,
    CMD_Q_PARTIALLY_FILLED,
} SMMUQStatus;

#define Q_ENTRY(q, idx)  (q->base + q->ent_size * idx)
#define Q_WRAP(q, pc)    ((pc) >> (q)->shift)
#define Q_IDX(q, pc)     ((pc) & ((1 << (q)->shift) - 1))

static inline SMMUQStatus __smmu_queue_status(SMMUV3State *s, SMMUQueue *q)
{
    uint32_t prod = Q_IDX(q, q->prod);
    uint32_t cons = Q_IDX(q, q->cons);

    if ((prod == cons) && (q->wrap.prod != q->wrap.cons)) {
        return CMD_Q_FULL;
    } else if ((prod == cons) && (q->wrap.prod == q->wrap.cons)) {
        return CMD_Q_EMPTY;
    }
    return CMD_Q_PARTIALLY_FILLED;
}
#define smmu_is_q_full(s, q) (__smmu_queue_status(s, q) == CMD_Q_FULL)
#define smmu_is_q_empty(s, q) (__smmu_queue_status(s, q) == CMD_Q_EMPTY)

static inline int __smmu_q_enabled(SMMUV3State *s, uint32_t q)
{
    return smmu_read32_reg(s, SMMU_REG_CR0) & q;
}
#define smmu_cmd_q_enabled(s) __smmu_q_enabled(s, SMMU_CR0_CMDQ_ENABLE)
#define smmu_evt_q_enabled(s) __smmu_q_enabled(s, SMMU_CR0_EVTQ_ENABLE)

static inline void smmu_write_cmdq_err(SMMUV3State *s, uint32_t err_type)
{
    uint32_t regval = smmu_read32_reg(s, SMMU_REG_CMDQ_CONS);

    smmu_write32_reg(s, SMMU_REG_CMDQ_CONS,
                        regval | err_type << SMMU_CMD_CONS_ERR_SHIFT);
}

void smmuv3_write_evtq(SMMUV3State *s, Evt *evt);

/*****************************
 * Commands
 *****************************/

enum {
    SMMU_CMD_PREFETCH_CONFIG = 0x01,
    SMMU_CMD_PREFETCH_ADDR,
    SMMU_CMD_CFGI_STE,
    SMMU_CMD_CFGI_STE_RANGE,
    SMMU_CMD_CFGI_CD,
    SMMU_CMD_CFGI_CD_ALL,
    SMMU_CMD_CFGI_ALL,
    SMMU_CMD_TLBI_NH_ALL     = 0x10,
    SMMU_CMD_TLBI_NH_ASID,
    SMMU_CMD_TLBI_NH_VA,
    SMMU_CMD_TLBI_NH_VAA,
    SMMU_CMD_TLBI_EL3_ALL    = 0x18,
    SMMU_CMD_TLBI_EL3_VA     = 0x1a,
    SMMU_CMD_TLBI_EL2_ALL    = 0x20,
    SMMU_CMD_TLBI_EL2_ASID,
    SMMU_CMD_TLBI_EL2_VA,
    SMMU_CMD_TLBI_EL2_VAA,  /* 0x23 */
    SMMU_CMD_TLBI_S12_VMALL  = 0x28,
    SMMU_CMD_TLBI_S2_IPA     = 0x2a,
    SMMU_CMD_TLBI_NSNH_ALL   = 0x30,
    SMMU_CMD_ATC_INV         = 0x40,
    SMMU_CMD_PRI_RESP,
    SMMU_CMD_RESUME          = 0x44,
    SMMU_CMD_STALL_TERM,
    SMMU_CMD_SYNC,          /* 0x46 */
};

static const char *cmd_stringify[] = {
    [SMMU_CMD_PREFETCH_CONFIG] = "SMMU_CMD_PREFETCH_CONFIG",
    [SMMU_CMD_PREFETCH_ADDR]   = "SMMU_CMD_PREFETCH_ADDR",
    [SMMU_CMD_CFGI_STE]        = "SMMU_CMD_CFGI_STE",
    [SMMU_CMD_CFGI_STE_RANGE]  = "SMMU_CMD_CFGI_STE_RANGE",
    [SMMU_CMD_CFGI_CD]         = "SMMU_CMD_CFGI_CD",
    [SMMU_CMD_CFGI_CD_ALL]     = "SMMU_CMD_CFGI_CD_ALL",
    [SMMU_CMD_CFGI_ALL]        = "SMMU_CMD_CFGI_ALL",
    [SMMU_CMD_TLBI_NH_ALL]     = "SMMU_CMD_TLBI_NH_ALL",
    [SMMU_CMD_TLBI_NH_ASID]    = "SMMU_CMD_TLBI_NH_ASID",
    [SMMU_CMD_TLBI_NH_VA]      = "SMMU_CMD_TLBI_NH_VA",
    [SMMU_CMD_TLBI_NH_VAA]     = "SMMU_CMD_TLBI_NH_VAA",
    [SMMU_CMD_TLBI_EL3_ALL]    = "SMMU_CMD_TLBI_EL3_ALL",
    [SMMU_CMD_TLBI_EL3_VA]     = "SMMU_CMD_TLBI_EL3_VA",
    [SMMU_CMD_TLBI_EL2_ALL]    = "SMMU_CMD_TLBI_EL2_ALL",
    [SMMU_CMD_TLBI_EL2_ASID]   = "SMMU_CMD_TLBI_EL2_ASID",
    [SMMU_CMD_TLBI_EL2_VA]     = "SMMU_CMD_TLBI_EL2_VA",
    [SMMU_CMD_TLBI_EL2_VAA]    = "SMMU_CMD_TLBI_EL2_VAA",
    [SMMU_CMD_TLBI_S12_VMALL]  = "SMMU_CMD_TLBI_S12_VMALL",
    [SMMU_CMD_TLBI_S2_IPA]     = "SMMU_CMD_TLBI_S2_IPA",
    [SMMU_CMD_TLBI_NSNH_ALL]   = "SMMU_CMD_TLBI_NSNH_ALL",
    [SMMU_CMD_ATC_INV]         = "SMMU_CMD_ATC_INV",
    [SMMU_CMD_PRI_RESP]        = "SMMU_CMD_PRI_RESP",
    [SMMU_CMD_RESUME]          = "SMMU_CMD_RESUME",
    [SMMU_CMD_STALL_TERM]      = "SMMU_CMD_STALL_TERM",
    [SMMU_CMD_SYNC]            = "SMMU_CMD_SYNC",
};

/*****************************
 * CMDQ fields
 *****************************/

typedef enum {
    SMMU_CERROR_NONE = 0,
    SMMU_CERROR_ILL,
    SMMU_CERROR_ABT,
    SMMU_CERROR_ATC_INV_SYNC,
} SMMUCmdError;

enum { /* Command completion notification */
    CMD_SYNC_SIG_NONE,
    CMD_SYNC_SIG_IRQ,
    CMD_SYNC_SIG_SEV,
};

#define CMD_TYPE(x)  extract32((x)->word[0], 0, 8)
#define CMD_SEC(x)   extract32((x)->word[0], 9, 1)
#define CMD_SEV(x)   extract32((x)->word[0], 10, 1)
#define CMD_AC(x)    extract32((x)->word[0], 12, 1)
#define CMD_AB(x)    extract32((x)->word[0], 13, 1)
#define CMD_CS(x)    extract32((x)->word[0], 12, 2)
#define CMD_SSID(x)  extract32((x)->word[0], 16, 16)
#define CMD_SID(x)   ((x)->word[1])
#define CMD_VMID(x)  extract32((x)->word[1], 0, 16)
#define CMD_ASID(x)  extract32((x)->word[1], 16, 16)
#define CMD_STAG(x)  extract32((x)->word[2], 0, 16)
#define CMD_RESP(x)  extract32((x)->word[2], 11, 2)
#define CMD_GRPID(x) extract32((x)->word[3], 0, 8)
#define CMD_SIZE(x)  extract32((x)->word[3], 0, 16)
#define CMD_LEAF(x)  extract32((x)->word[3], 0, 1)
#define CMD_SPAN(x)  extract32((x)->word[3], 0, 5)
#define CMD_ADDR(x) ({                                  \
            uint64_t addr = (uint64_t)(x)->word[3];     \
            addr <<= 32;                                \
            addr |=  extract32((x)->word[3], 12, 20);   \
            addr;                                       \
        })

#endif
