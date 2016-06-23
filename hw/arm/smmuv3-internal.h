/*
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
 * Copyright (C) 2014-2015 Broadcom
 *
 * Author: Prem Mallappa <pmallapp@broadcom.com>
 *
 */

#ifndef HW_ARM_SMMU_V3_INTERNAL_H
#define HW_ARM_SMMU_V3_INTERNAL_H

/*****************************
 * MMIO Register
 *****************************/
enum {
    SMMU_REG_IDR0            = 0x0,

#define SMMU_IDR0_S2P            (1 << 0)
#define SMMU_IDR0_S1P            (1 << 1)
#define SMMU_IDR0_TTF            (0x3 << 2)
#define SMMU_IDR0_HTTU           (0x3 << 6)
#define SMMU_IDR0_HYP            (1 << 9)
#define SMMU_IDR0_ATS            (1 << 10)
#define SMMU_IDR0_VMID16         (1 << 18)
#define SMMU_IDR0_CD2L           (1 << 19)

    SMMU_REG_IDR1            = 0x4,
    SMMU_REG_IDR2            = 0x8,
    SMMU_REG_IDR3            = 0xc,
    SMMU_REG_IDR4            = 0x10,
    SMMU_REG_IDR5            = 0x14,
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

#define SMMU_GERROR_CMDQ       (1 << 0)
#define SMMU_GERROR_EVENTQ     (1 << 2)
#define SMMU_GERROR_PRIQ       (1 << 3)
#define SMMU_GERROR_MSI_CMDQ   (1 << 4)
#define SMMU_GERROR_MSI_EVENTQ (1 << 5)
#define SMMU_GERROR_MSI_PRIQ   (1 << 6)
#define SMMU_GERROR_MSI_GERROR (1 << 7)
#define SMMU_GERROR_SFM_ERR    (1 << 8)

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

/*****************************
 * STE fields
 *****************************/
#define STE_VALID(x)   extract32((x)->word[0], 0, 1) /* 0 */
#define STE_CONFIG(x)  (extract32((x)->word[0], 1, 3) & 0x3)
enum {
    STE_CONFIG_NONE      = 0,
    STE_CONFIG_S1BY_S2BY = 4,   /* S1 Bypass, S2 Bypass */
    STE_CONFIG_S1TR_S2BY,       /* S1 Translate, S2 Bypass */
    STE_CONFIG_S1BY_S2TR,       /* S1 Bypass, S2 Translate */
    STE_CONFIG_S1TR_S2TR,       /* S1 Translate, S2 Translate */
};
#define STE_S1FMT(x)   extract32((x)->word[0], 4, 2)
#define STE_S1CDMAX(x) extract32((x)->word[1], 8, 2)
#define STE_EATS(x)    extract32((x)->word[2], 28, 2)
#define STE_STRW(x)    extract32((x)->word[2], 30, 2)
#define STE_S2VMID(x)  extract32((x)->word[4], 0, 16) /* 4 */
#define STE_S2T0SZ(x)  extract32((x)->word[5], 0, 6) /* 5 */
#define STE_S2TG(x)    extract32((x)->word[5], 14, 2)
#define STE_S2PS(x)    extract32((x)->word[5], 16, 3)
#define STE_S2AA64(x)  extract32((x)->word[5], 19, 1)
#define STE_S2HD(x)    extract32((x)->word[5], 24, 1)
#define STE_S2HA(x)    extract32((x)->word[5], 25, 1)
#define STE_S2S(x)     extract32((x)->word[5], 26, 1)
#define STE_CTXPTR(x)                                           \
    ({                                                          \
        unsigned long addr;                                     \
        addr = (uint64_t)extract32((x)->word[1], 0, 16) << 32;  \
        addr |= (uint64_t)((x)->word[0] & 0xffffffc0);          \
        addr;                                                   \
    })

#define STE_S2TTB(x)                                            \
    ({                                                          \
        unsigned long addr;                                     \
        addr = (uint64_t)extract32((x)->word[7], 0, 16) << 32;  \
        addr |= (uint64_t)((x)->word[6] & 0xfffffff0);          \
        addr;                                                   \
    })

/*****************************
 * CD fields
 *****************************/
#define CD_VALID(x)   extract32((x)->word[0], 30, 1)
#define CD_ASID(x)    extract32((x)->word[1], 16, 16)
#define CD_TTB(x, sel)                                      \
    ({                                                      \
        uint64_t hi, lo;                                    \
        hi = extract32((x)->word[(sel) * 2 + 3], 0, 16);    \
        hi <<= 32;                                          \
        lo = (x)->word[(sel) * 2 + 2] & ~0xf;               \
        hi | lo;                                            \
    })

#define CD_TSZ(x, sel)   extract32((x)->word[0], (16 * (sel)) + 0, 6)
#define CD_TG(x, sel)    extract32((x)->word[0], (16 * (sel)) + 6, 2)
#define CD_EPD(x, sel)   extract32((x)->word[0], (16 * (sel)) + 14, 1)

#define CD_T0SZ(x)    CD_TSZ((x), 0)
#define CD_T1SZ(x)    CD_TSZ((x), 1)
#define CD_TG0(x)     CD_TG((x), 0)
#define CD_TG1(x)     CD_TG((x), 1)
#define CD_EPD0(x)    CD_EPD((x), 0)
#define CD_EPD1(x)    CD_EPD((x), 1)
#define CD_IPS(x)     extract32((x)->word[1], 0, 3)
#define CD_AARCH64(x) extract32((x)->word[1], 9, 1)
#define CD_TTB0(x)    CD_TTB((x), 0)
#define CD_TTB1(x)    CD_TTB((x), 1)

#define CDM_VALID(x)    ((x)->word[0] & 0x1)

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

/*****************************
 * CMDQ fields
 *****************************/

enum { /* Command Errors */
    SMMU_CMD_ERR_NONE = 0,
    SMMU_CMD_ERR_ILLEGAL,
    SMMU_CMD_ERR_ABORT
};

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

/*****************************
 * EVTQ fields
 *****************************/
#define EVT_Q_OVERFLOW        (1 << 31)

#define EVT_SET_TYPE(x, t)    deposit32((x)->word[0], 0, 8, t)
#define EVT_SET_SID(x, s)     ((x)->word[1] =  s)
#define EVT_SET_INPUT_ADDR(x, addr) ({                    \
            (x)->word[5] = (uint32_t)(addr >> 32);        \
            (x)->word[4] = (uint32_t)(addr & 0xffffffff); \
            addr;                                         \
        })

/*****************************
 * Events
 *****************************/
enum evt_err {
    SMMU_EVT_F_UUT    = 0x1,
    SMMU_EVT_C_BAD_SID,
    SMMU_EVT_F_STE_FETCH,
    SMMU_EVT_C_BAD_STE,
    SMMU_EVT_F_BAD_ATS_REQ,
    SMMU_EVT_F_STREAM_DISABLED,
    SMMU_EVT_F_TRANS_FORBIDDEN,
    SMMU_EVT_C_BAD_SSID,
    SMMU_EVT_F_CD_FETCH,
    SMMU_EVT_C_BAD_CD,
    SMMU_EVT_F_WALK_EXT_ABRT,
    SMMU_EVT_F_TRANS        = 0x10,
    SMMU_EVT_F_ADDR_SZ,
    SMMU_EVT_F_ACCESS,
    SMMU_EVT_F_PERM,
    SMMU_EVT_F_TLB_CONFLICT = 0x20,
    SMMU_EVT_F_CFG_CONFLICT = 0x21,
    SMMU_EVT_E_PAGE_REQ     = 0x24,
};

typedef enum evt_err SMMUEvtErr;


/*****************************
 * SMMU Data structures
 *****************************/
#define ARM_SMMU_FEAT_PASSID_SUPPORT  (1 << 24) /* Some random bits for now */
#define ARM_SMMU_FEAT_CD_2LVL         (1 << 25)

struct SMMUQueue {
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
};
typedef struct SMMUQueue SMMUQueue;

#define Q_ENTRY(q, idx)  (q->base + q->ent_size * idx)
#define Q_WRAP(q, pc)    ((pc) >> (q)->shift)
#define Q_IDX(q, pc)     ((pc) & ((1 << (q)->shift) - 1))

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

typedef struct __smmu_data2  STEDesc; /* STE Level 1 Descriptor */
typedef struct __smmu_data16 Ste;     /* Stream Table Entry(STE) */
typedef struct __smmu_data2  CDDesc;  /* CD Level 1 Descriptor */
typedef struct __smmu_data16 Cd;      /* Context Descriptor(CD) */

typedef struct __smmu_data4  Cmd; /* Command Entry */
typedef struct __smmu_data8  Evt; /* Event Entry */
typedef struct __smmu_data4  Pri; /* PRI entry */


/*****************************
 * Broadcom Specific register and bits
 *****************************/
#define SMMU_REG_CNTL         (0x410 << 2)
#define SMMU_REG_CNTL_1       (0x411 << 2)
#define SMMU_REG_INTERRUPT    (0x412 << 2)
/* BIT encoding is same as SMMU_REG_INTERRUPT, except for last 4 bits */
#define SMMU_REG_INTERRUPT_EN (0x413 << 2)

#define SMMU_INTR_BMI_ERR  (1 << 6) /* Smmu BMI Rd Wr Error*/
#define SMMU_INTR_BSI_ERR  (1 << 5) /* Smmu BSI Rd Wr Error*/
#define SMMU_INTR_SBU_INTR (1 << 4) /* SBU interrupt 0 */
#define SMMU_INTR_CMD_SYNC (1 << 3) /* CmdSync completion set to interrupt */
#define SMMU_INTR_EVENT    (1 << 2) /* high till EventQ.PROD != EventQ.CONS */
#define SMMU_INTR_PRI      (1 << 1) /* PriQ. high till PriQ.PROD != PriQ.CONS */
#define SMMU_INTR_GERROR   (1 << 0) /* cleared when  GERRORN is written */

/*****************************
 * QEMu related
 *****************************/

typedef struct {
    SMMUBaseClass smmu_base_class;
} SMMUV3Class;

#define SMMU_DEVICE_CLASS(klass)                                    \
    OBJECT_CLASS_CHECK(SMMUBaseClass, (klass), TYPE_SMMU_DEV_BASE)

#define SMMU_V3_DEVICE_GET_CLASS(obj)                              \
    OBJECT_GET_CLASS(SMMUBaseClass, (obj), TYPE_SMMU_V3_DEV)

#ifdef ARM_SMMU_DEBUG
static inline void dump_ste(Ste *ste)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(ste->word); i += 2) {
        SMMU_DPRINTF(STE, "STE[%2d]: %#010x\t STE[%2d]: %#010x\n",
                i, ste->word[i], i + 1, ste->word[i + 1]);
    }
}

static inline void dump_cd(Cd *cd)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(cd->word); i += 2) {
        SMMU_DPRINTF(CD, "CD[%2d]: %#010x\t CD[%2d]: %#010x\n",
                i, cd->word[i], i + 1, cd->word[i + 1]);
    }
}

static inline void dump_evt(Evt *e)
{}

static inline void dump_cmd(Cmd *cmd)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(cmd->word); i += 2) {
        SMMU_DPRINTF(CMDQ, "CMD[%2d]: %#010x\t CMD[%2d]: %#010x\n",
                i, cmd->word[i], i + 1, cmd->word[i + 1]);
    }
}
#else
#define dump_ste(...) do {} while (0)
#define dump_cd(...) do {} while (0)
#define dump_evt(...) do {} while (0)
#define dump_cmd(...) do {} while (0)
#endif

#endif
