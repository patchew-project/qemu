/*
 * Copyright (C) 2014-2016 Broadcom
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
#include "hw/boards.h"
#include "sysemu/sysemu.h"
#include "hw/sysbus.h"
#include "hw/pci/pci.h"
#include "exec/address-spaces.h"

#include "hw/arm/smmu.h"
#include "smmu-common.h"
#include "smmuv3-internal.h"

#define SMMU_NREGS       0x200
#define PCI_DEVFN_MAX    256

#ifdef ARM_SMMU_DEBUG
uint32_t dbg_bits =                             \
    DBG_DEFAULT |                               \
    DBG_VERBOSE3 |                              \
    DBG_EXTRA |                                 \
    DBG_VERBOSE1;
#else
const uint32_t dbg_bits;
#endif

typedef struct RegInfo RegInfo;

typedef void  (*post_write_t)(RegInfo *r, uint64_t addr,
                              uint64_t val, void *opaque);

struct RegInfo {
    uint64_t     data;
    uint64_t     rao_mask;      /* Reserved as One */
    uint64_t     raz_mask;      /* Reserved as Zero */
    post_write_t post;
};

typedef struct SMMUDevice SMMUDevice;

struct SMMUDevice {
    void         *smmu;
    PCIBus       *bus;
    int           devfn;
    MemoryRegion  iommu;
    AddressSpace  as;
    AddressSpace  *asp;
};

typedef struct SMMUV3State SMMUV3State;

struct SMMUV3State {
    SMMUState     smmu_state;

#define SMMU_FEATURE_2LVL_STE (1 << 0)
    /* Local cache of most-frequently used register */
    uint32_t     features;
    uint16_t     sid_size;
    uint16_t     sid_split;
    uint64_t     strtab_base;

    RegInfo      regs[SMMU_NREGS];

    qemu_irq     irq[4];

    SMMUQueue    cmdq, evtq, priq;

    /* IOMMU Address space */
    MemoryRegion iommu;
    AddressSpace iommu_as;

    SMMUDevice   pbdev[PCI_DEVFN_MAX];
};

#define SMMU_V3_DEV(obj) OBJECT_CHECK(SMMUV3State, (obj), TYPE_SMMU_V3_DEV)

static void smmu_write_reg(SMMUV3State *s, uint32_t addr, uint64_t val)
{
    RegInfo *reg = &s->regs[addr >> 2];

    reg->data = val;

    if (reg->post) {
        reg->post(reg, addr, val, s);
    }
}

#define smmu_write32_reg smmu_write_reg

static inline uint32_t smmu_read32_reg(SMMUV3State *s, uint32_t addr)
{
    RegInfo *reg = &s->regs[addr >> 2];

    return (uint32_t)reg->data;
}

static inline uint64_t smmu_read64_reg(SMMUV3State *s, uint32_t addr)
{
    RegInfo *reg = &s->regs[addr >> 2];

    return reg->data;
}

static inline int smmu_enabled(SMMUV3State *s)
{
    return (smmu_read32_reg(s, SMMU_REG_CR0) & SMMU_CR0_SMMU_ENABLE) != 0;
}

typedef enum {
    CMD_Q_EMPTY,
    CMD_Q_FULL,
    CMD_Q_INUSE,
} SMMUQStatus;

static inline SMMUQStatus
__smmu_queue_status(SMMUV3State *s, SMMUQueue *q)
{
    uint32_t prod = Q_IDX(q, q->prod), cons = Q_IDX(q, q->cons);
    if ((prod == cons) && (q->wrap.prod != q->wrap.cons)) {
        return CMD_Q_FULL;
    } else if ((prod == cons) && (q->wrap.prod == q->wrap.cons)) {
        return CMD_Q_EMPTY;
    }
    return CMD_Q_INUSE;
}
#define smmu_is_q_full(s, q) (__smmu_queue_status(s, q) == CMD_Q_FULL)
#define smmu_is_q_empty(s, q) (__smmu_queue_status(s, q) == CMD_Q_EMPTY)

static int __smmu_q_enabled(SMMUV3State *s, uint32_t q)
{
    return smmu_read32_reg(s, SMMU_REG_CR0) & q;
}
#define smmu_cmd_q_enabled(s) __smmu_q_enabled(s, SMMU_CR0_CMDQ_ENABLE)
#define smmu_evt_q_enabled(s) __smmu_q_enabled(s, SMMU_CR0_EVTQ_ENABLE)

static inline int __smmu_irq_enabled(SMMUV3State *s, uint32_t q)
{
    return smmu_read64_reg(s, SMMU_REG_IRQ_CTRL) & q;
}
#define smmu_evt_irq_enabled(s)                   \
    __smmu_irq_enabled(s, SMMU_IRQ_CTRL_EVENT_EN)
#define smmu_gerror_irq_enabled(s)                  \
    __smmu_irq_enabled(s, SMMU_IRQ_CTRL_GERROR_EN)
#define smmu_pri_irq_enabled(s)                 \
    __smmu_irq_enabled(s, SMMU_IRQ_CTRL_PRI_EN)


static inline int is_cd_valid(SMMUV3State *s, Ste *ste, Cd *cd)
{
    return CD_VALID(cd);
}

static inline int is_ste_valid(SMMUV3State *s, Ste *ste)
{
    return STE_VALID(ste);
}

static inline int is_ste_bypass(SMMUV3State *s, Ste *ste)
{
    return STE_CONFIG(ste) == STE_CONFIG_S1BY_S2BY;
}

static inline uint16_t smmu_get_sid(SMMUDevice *sdev)
{
    return  ((pci_bus_num(sdev->bus) & 0xff) << 8) | sdev->devfn;
}

static void smmu_coresight_regs_init(SMMUV3State *sv3)
{
    SMMUState *s = SMMU_SYS_DEV(sv3);
    int i;

    /* Primecell ID registers */
    s->cid[0] = 0x0D;
    s->cid[1] = 0xF0;
    s->cid[2] = 0x05;
    s->cid[3] = 0xB1;

    for (i = 0; i < ARRAY_SIZE(s->pid); i++) {
        s->pid[i] = 0x1;
    }
}

/*
 * smmu_irq_update:
 * update corresponding register,
 * return > 0 when IRQ is supposed to be rased
 * Spec req:
 *      - Raise irq only when it not active already,
 *        blindly toggling bits may actually clear the error
 */
static int
smmu_irq_update(SMMUV3State *s, int irq, uint64_t data)
{
    uint32_t error = 0;

    switch (irq) {
    case SMMU_IRQ_EVTQ:
        if (smmu_evt_irq_enabled(s)) {
            error = SMMU_GERROR_EVENTQ;
        }
        break;
    case SMMU_IRQ_CMD_SYNC:
        if (smmu_gerror_irq_enabled(s)) {
            uint32_t err_type = (uint32_t)data;
            if (err_type) {
                uint32_t regval = smmu_read32_reg(s, SMMU_REG_CMDQ_CONS);
                smmu_write32_reg(s, SMMU_REG_CMDQ_CONS,
                                 regval | err_type << SMMU_CMD_CONS_ERR_SHIFT);
            }
            error = SMMU_GERROR_CMDQ;
        }
        break;
    case SMMU_IRQ_PRIQ:
        if (smmu_pri_irq_enabled(s)) {
            error = SMMU_GERROR_PRIQ;
        }
        break;
    }
    SMMU_DPRINTF(IRQ, "<< error:%x\n", error);

    if (error && smmu_gerror_irq_enabled(s)) {
        uint32_t gerror = smmu_read32_reg(s, SMMU_REG_GERROR);
        uint32_t gerrorn = smmu_read32_reg(s, SMMU_REG_GERRORN);
        SMMU_DPRINTF(IRQ, "<<<< error:%x gerror:%x gerrorn:%x\n",
                     error, gerror, gerrorn);
        if (!((gerror ^ gerrorn) & error)) {
            smmu_write32_reg(s, SMMU_REG_GERROR, gerror ^ error);
        }
    }

    return error;
}

static void smmu_irq_raise(SMMUV3State *s, int irq, uint64_t data)
{
    SMMU_DPRINTF(IRQ, "irq:%d\n", irq);
    if (smmu_irq_update(s, irq, data)) {
            qemu_irq_raise(s->irq[irq]);
    }
}

static MemTxResult smmu_q_read(SMMUV3State *s, SMMUQueue *q, void *data)
{
    uint64_t addr = Q_ENTRY(q, Q_IDX(q, q->cons));

    q->cons++;
    if (q->cons == q->entries) {
        q->cons = 0;
        q->wrap.cons++;     /* this will toggle */
    }

    return smmu_read_sysmem(addr, data, q->ent_size);
}

static MemTxResult smmu_q_write(SMMUV3State *s, SMMUQueue *q, void *data)
{
    uint64_t addr = Q_ENTRY(q, Q_IDX(q, q->prod));

    if (q->prod == q->entries) {
        q->prod = 0;
        q->wrap.prod++;     /* this will toggle */
    }

    q->prod++;

    smmu_write_sysmem(addr, data, q->ent_size);

    return MEMTX_OK;
}

static MemTxResult smmu_read_cmdq(SMMUV3State *s, Cmd *cmd)
{
    SMMUQueue *q = &s->cmdq;
    MemTxResult ret = smmu_q_read(s, q, cmd);
    uint32_t val = 0;

    val |= (q->wrap.cons << q->shift) | q->cons;

    /* Update consumer pointer */
    smmu_write32_reg(s, SMMU_REG_CMDQ_CONS, val);

    return ret;
}

#define SMMU_CMDQ_ERR(s) ((smmu_read32_reg(s, SMMU_REG_GERROR) ^    \
                           smmu_read32_reg(s, SMMU_REG_GERRORN)) &  \
                          SMMU_GERROR_CMDQ)

static int smmu_cmdq_consume(SMMUV3State *s)
{
    uint32_t error = SMMU_CMD_ERR_NONE;

    SMMU_DPRINTF(CMDQ, "CMDQ_ERR: %d\n", SMMU_CMDQ_ERR(s));

    while (!SMMU_CMDQ_ERR(s) && !smmu_is_q_empty(s, &s->cmdq)) {
        Cmd cmd;
#ifdef ARM_SMMU_DEBUG
        SMMUQueue *q = &s->cmdq;
#endif
        if (smmu_read_cmdq(s, &cmd) != MEMTX_OK) {
            error = SMMU_CMD_ERR_ABORT;
            goto out_while;
        }

        SMMU_DPRINTF(DBG2, "CMDQ base: %lx cons:%d prod:%d val:%x wrap:%d\n",
                     q->base, q->cons, q->prod, cmd.word[0], q->wrap.cons);

        switch (CMD_TYPE(&cmd)) {
        case SMMU_CMD_CFGI_STE:
        case SMMU_CMD_CFGI_STE_RANGE:
            break;
        case SMMU_CMD_TLBI_NSNH_ALL: /* TLB not implemented */
        case SMMU_CMD_TLBI_EL2_ALL:  /* Fallthrough */
        case SMMU_CMD_TLBI_EL3_ALL:
        case SMMU_CMD_TLBI_NH_ALL:
        case SMMU_CMD_TLBI_S2_IPA:
            break;
        case SMMU_CMD_SYNC:     /* Fallthrough */
            if (CMD_CS(&cmd) & CMD_SYNC_SIG_IRQ) {
                smmu_irq_raise(s, SMMU_IRQ_CMD_SYNC, SMMU_CMD_ERR_NONE);
            }
            break;
        case SMMU_CMD_PREFETCH_CONFIG:
            break;
        case SMMU_CMD_TLBI_NH_ASID:
        case SMMU_CMD_TLBI_NH_VA:   /* too many of this is sent */
            break;

        default:
            error = SMMU_CMD_ERR_ILLEGAL;
            SMMU_DPRINTF(CRIT, "Unknown Command type: %x, ignoring\n",
                         CMD_TYPE(&cmd));
            if (IS_DBG_ENABLED(CD)) {
                dump_cmd(&cmd);
            }
            break;
        }

        if (error != SMMU_CMD_ERR_NONE) {
            SMMU_DPRINTF(INFO, "CMD Error\n");
            goto out_while;
        }
    }

out_while:
    if (error) {
        smmu_irq_raise(s, SMMU_IRQ_GERROR, error);
    }

    SMMU_DPRINTF(CMDQ, "prod_wrap:%d, prod:%x cons_wrap:%d cons:%x\n",
                 s->cmdq.wrap.prod, s->cmdq.prod,
                 s->cmdq.wrap.cons, s->cmdq.cons);

    return 0;
}

static inline bool
smmu_is_irq_pending(SMMUV3State *s, int irq)
{
    return smmu_read32_reg(s, SMMU_REG_GERROR) ^
        smmu_read32_reg(s, SMMU_REG_GERRORN);
}

/*
 * GERROR is updated when rasing an interrupt, GERRORN will be updated
 * by s/w and should match GERROR before normal operation resumes.
 */
static void smmu_irq_clear(SMMUV3State *s, uint64_t gerrorn)
{
    int irq = SMMU_IRQ_GERROR;
    uint32_t toggled;

    toggled = smmu_read32_reg(s, SMMU_REG_GERRORN) ^ gerrorn;

    while (toggled) {
        irq = ctz32(toggled);

        qemu_irq_lower(s->irq[irq]);

        toggled &= toggled - 1;
    }
}

static int smmu_evtq_update(SMMUV3State *s)
{
    if (!smmu_enabled(s)) {
        return 0;
    }

    if (!smmu_is_q_empty(s, &s->evtq)) {
        if (smmu_evt_irq_enabled(s))
            smmu_irq_raise(s, SMMU_IRQ_EVTQ, 0);
    }

    if (smmu_is_q_empty(s, &s->evtq)) {
        smmu_irq_clear(s, SMMU_GERROR_EVENTQ);
    }

    return 1;
}

static void smmu_create_event(SMMUV3State *s, hwaddr iova,
                              uint32_t sid, bool is_write, int error);

static void smmu_update(SMMUV3State *s)
{
    int error = 0;

    /* SMMU starts processing commands even when not enabled */
    if (!smmu_enabled(s)) {
        goto check_cmdq;
    }

    /* EVENT Q updates takes more priority */
    if ((smmu_evt_q_enabled(s)) && !smmu_is_q_empty(s, &s->evtq)) {
        SMMU_DPRINTF(CRIT, "q empty:%d prod:%d cons:%d p.wrap:%d p.cons:%d\n",
                     smmu_is_q_empty(s, &s->evtq), s->evtq.prod,
                     s->evtq.cons, s->evtq.wrap.prod, s->evtq.wrap.cons);
        error = smmu_evtq_update(s);
    }

    if (error) {
        /* TODO: May be in future we create proper event queue entry */
        /* an error condition is not a recoverable event, like other devices */
        SMMU_DPRINTF(CRIT, "An unfavourable condition\n");
        smmu_create_event(s, 0, 0, 0, error);
    }

check_cmdq:
    if (smmu_cmd_q_enabled(s) && !SMMU_CMDQ_ERR(s)) {
        smmu_cmdq_consume(s);
    }

}

static void __smmu_update_q(SMMUV3State *s, SMMUQueue *q, uint64_t val,
                            uint64_t addr)
{
    switch (addr) {
    case SMMU_REG_CMDQ_BASE:
    case SMMU_REG_EVTQ_BASE:
        q->shift = val & 0x1f;
        q->entries = 1 << (q->shift);
        break;
    case SMMU_REG_CMDQ_PROD:
    case SMMU_REG_EVTQ_PROD:
        q->prod = Q_IDX(q, val);
        q->wrap.prod = val >> q->shift;
        break;
    case SMMU_REG_EVTQ_CONS:
    case SMMU_REG_CMDQ_CONS:
        q->cons = Q_IDX(q, val);
        q->wrap.cons = val >> q->shift;
        break;
    }

    if (addr == SMMU_REG_CMDQ_PROD) { /* possibly new command present */
        smmu_update(s);
    }
}

static void smmu_update_q(RegInfo *r, uint64_t addr, uint64_t val,
                          void *opaque)
{
    SMMUQueue *q = NULL;
    SMMUV3State *s = opaque;

    switch (addr) {
    case SMMU_REG_CMDQ_BASE ... SMMU_REG_CMDQ_CONS:
        q = &s->cmdq;
        break;
    case SMMU_REG_EVTQ_BASE ... SMMU_REG_EVTQ_IRQ_CFG2:
        q = &s->evtq;
        break;
    default:
        SMMU_DPRINTF(CRIT, "Trying to write to not Q in %s\n", __func__);
        return;
    }

    __smmu_update_q(s, q, val, addr);
}

static void smmu_update_irq(RegInfo *r, uint64_t addr, uint64_t val,
                            void *opaque)
{
    SMMUV3State *s = opaque;

    smmu_irq_clear(s, val);

    smmu_write32_reg(s, SMMU_REG_GERRORN, val);

    SMMU_DPRINTF(IRQ, "irq pend: %d gerror:%x gerrorn:%x\n",
                 smmu_is_irq_pending(s, 0),
                 smmu_read32_reg(s, SMMU_REG_GERROR),
                 smmu_read32_reg(s, SMMU_REG_GERRORN));

    /* Clear only when no more left */
    if (!smmu_is_irq_pending(s, 0)) {
        qemu_irq_lower(s->irq[0]);
    }
}

static void smmu_update_base(RegInfo *r, uint64_t addr, uint64_t val,
                            void *opaque)
{
    SMMUV3State *s = opaque;
    SMMUQueue *q = NULL;
    uint64_t *base = NULL;

    switch (addr) {
    case SMMU_REG_STRTAB_BASE:
        base = &s->strtab_base;
        break;
    case SMMU_REG_EVTQ_BASE:
        q = &s->evtq;
        base = &s->evtq.base;
        break;
    case SMMU_REG_CMDQ_BASE:
        q = &s->cmdq;
        base = &s->cmdq.base;
        break;
    case SMMU_REG_PRIQ_BASE:
        q = &s->priq;
        base = &s->priq.base;
        break;
    }

    /* BIT[62], BIT[5:0] are ignored */
    *base = (uint64_t)(smmu_read64_reg(s, addr)) & ~(SMMU_BASE_RA | 0x3fULL);

    if (q) {
        __smmu_update_q(s, q, val, addr);
    }
}

static void smmuv3_reg_update_cr0(RegInfo *r, uint64_t addr, uint64_t val,
                                  void *opaque)
{
    SMMUV3State *s = opaque;
    /* Update the ACK register */
    smmu_write32_reg(s, SMMU_REG_CR0_ACK, val);
    smmu_update(s);                     /* Start processing if enabled */
}

static void smmuv3_reg_update_strtab_bcfg(RegInfo *r, uint64_t addr,
                                              uint64_t val, void *opaque)
{
    SMMUV3State *s = opaque;

    if (((val >> 16) & 0x3) == 0x1) {
        s->sid_split = (val >> 6) & 0x1f;
        s->features |= SMMU_FEATURE_2LVL_STE;
    }
}

static void smmuv3_reg_update_irq_ctrl(RegInfo *r, uint64_t addr, uint64_t val,
                                       void *opaque)
{
    SMMUV3State *s = opaque;

    smmu_write32_reg(s, SMMU_REG_IRQ_CTRL_ACK, val);
    smmu_update(s);
}

static void smmu_update_evtq_cons(RegInfo *r, uint64_t addr, uint64_t val,
                                  void *opaque)
{
    SMMUV3State *s = opaque;
    SMMUQueue *evtq = &s->evtq;

    evtq->cons = Q_IDX(evtq, val);
    evtq->wrap.cons = Q_WRAP(evtq, val);

    SMMU_DPRINTF(IRQ, "BEFORE CLEARING INTERRUPT "
                 "prod:%x cons:%x prod.w:%d cons.w:%d\n",
                 evtq->prod, evtq->cons, evtq->wrap.prod, evtq->wrap.cons);
    if (smmu_is_q_empty(s, &s->evtq)) {

        SMMU_DPRINTF(IRQ, "CLEARING INTERRUPT"
                     " prod:%x cons:%x prod.w:%d cons.w:%d\n",
                     evtq->prod, evtq->cons, evtq->wrap.prod, evtq->wrap.cons);
        qemu_irq_lower(s->irq[SMMU_IRQ_EVTQ]);
    }
}

#define REG_TO_OFFSET(reg) (reg >> 2)

static RegInfo smmu_v3_regs[SMMU_NREGS] = {
    [REG_TO_OFFSET(SMMU_REG_CR0)]             = {
        .post = smmuv3_reg_update_cr0,
    },
    [REG_TO_OFFSET(SMMU_REG_GERRORN)]         = {
        .post = smmu_update_irq,
    },
    [REG_TO_OFFSET(SMMU_REG_STRTAB_BASE)]     = {
        .post = smmu_update_base,
    },
    [REG_TO_OFFSET(SMMU_REG_IRQ_CTRL)]        = {
        .post = smmuv3_reg_update_irq_ctrl,
    },
    [REG_TO_OFFSET(SMMU_REG_STRTAB_BASE_CFG)] = {
        .post = smmuv3_reg_update_strtab_bcfg,
    },
    [REG_TO_OFFSET(SMMU_REG_CMDQ_BASE)]       = {
        .post = smmu_update_base,
    },
    [REG_TO_OFFSET(SMMU_REG_CMDQ_PROD)]       = {
        .post = smmu_update_q,
    },
    [REG_TO_OFFSET(SMMU_REG_EVTQ_BASE)]       = {
        .post = smmu_update_base,
    },
    [REG_TO_OFFSET(SMMU_REG_EVTQ_CONS)]       = {
        .post = smmu_update_evtq_cons,
    },
    [REG_TO_OFFSET(SMMU_REG_PRIQ_BASE)]       = {
        /* .post = smmu_update_base, */
    },
};

#define SMMU_ID_REG_INIT(s, reg, d) do {            \
        (s)->regs[REG_TO_OFFSET(reg)] = (RegInfo) { \
            .data = (d),                            \
            .rao_mask = (d),                        \
            .raz_mask = ~(d),                       \
            .post = NULL,                           \
        };                                          \
    } while (0)

static void smmuv3_id_reg_init(SMMUV3State *s)
{
    uint32_t data =
        1 << 27 |                   /* 2 Level stream id */
        1 << 26 |                   /* Term Model  */
        1 << 24 |                   /* Stall model not supported */
        1 << 18 |                   /* VMID 16 bits */
        1 << 16 |                   /* PRI */
        1 << 12 |                   /* ASID 16 bits */
        1 << 10 |                   /* ATS */
        1 << 9 |                    /* HYP */
        2 << 6 |                    /* HTTU */
        1 << 4 |                    /* COHACC */
        2 << 2 |                    /* TTF=Arch64 */
        1 << 1 |                    /* Stage 1 */
        1 << 0;                     /* Stage 2 */

    SMMU_ID_REG_INIT(s, SMMU_REG_IDR0, data);

#define SMMU_SID_SIZE         16
#define SMMU_QUEUE_SIZE_LOG2  19
    data =
        1 << 27 |                    /* Attr Types override */
        SMMU_QUEUE_SIZE_LOG2 << 21 | /* Cmd Q size */
        SMMU_QUEUE_SIZE_LOG2 << 16 | /* Event Q size */
        SMMU_QUEUE_SIZE_LOG2 << 11 | /* PRI Q size */
        0  << 6 |                    /* SSID not supported */
        SMMU_SID_SIZE << 0 ;         /* SID size  */

    SMMU_ID_REG_INIT(s, SMMU_REG_IDR1, data);

    data =
        1 << 6 |                    /* Granule 64K */
        1 << 4 |                    /* Granule 4K */
        4 << 0;                     /* OAS = 44 bits */

    SMMU_ID_REG_INIT(s, SMMU_REG_IDR5, data);

}

static void smmuv3_regs_init(SMMUV3State *s)
{
    int i = ARRAY_SIZE(smmu_v3_regs);
    while (i--) {
        RegInfo *from = &smmu_v3_regs[i];
        RegInfo *to = &s->regs[i];
        *to = *from;
    }

    smmuv3_id_reg_init(s);      /* Update ID regs alone */
}

static void smmuv3_init(SMMUV3State *s)
{
    smmu_coresight_regs_init(s);

    smmuv3_regs_init(s);

    s->sid_size = SMMU_SID_SIZE;

    s->cmdq.entries = (smmu_read32_reg(s, SMMU_REG_IDR1) >> 21) & 0x1f;
    s->cmdq.ent_size = sizeof(Cmd);
    s->evtq.entries = (smmu_read32_reg(s, SMMU_REG_IDR1) >> 16) & 0x1f;
    s->evtq.ent_size = sizeof(Evt);
}

/*
 * All SMMU data structures are little endian, and are aligned to 8 bytes
 * L1STE/STE/L1CD/CD, Queue entries in CMDQ/EVTQ/PRIQ
 */
static inline int smmu_get_ste(SMMUV3State *s, hwaddr addr, Ste *buf)
{
    return dma_memory_read(&address_space_memory, addr, buf, sizeof(*buf));
}

/*
 * For now we only support CD with a single entry, 'ssid' is used to identify
 * otherwise
 */
static inline int smmu_get_cd(SMMUV3State *s, Ste *ste, uint32_t ssid, Cd *buf)
{
    hwaddr addr = STE_CTXPTR(ste);

    if (STE_S1CDMAX(ste) != 0) {
        SMMU_DPRINTF(CRIT, "Multilevel Ctx Descriptor not supported yet\n");
    }

    return dma_memory_read(&address_space_memory, addr, buf, sizeof(*buf));
}

static int
is_ste_consistent(SMMUV3State *s, Ste *ste)
{
    uint32_t _config = STE_CONFIG(ste) & 0x7,
        idr0 = smmu_read32_reg(s, SMMU_REG_IDR0),
        idr5 = smmu_read32_reg(s, SMMU_REG_IDR5);

    uint32_t httu = extract32(idr0, 6, 2);
    bool config[] = {_config & 0x1,
                     _config & 0x2,
                     _config & 0x3};
    bool granule_supported;

    bool s1p = idr0 & SMMU_IDR0_S1P,
        s2p = idr0 & SMMU_IDR0_S2P,
        hyp = idr0 & SMMU_IDR0_HYP,
        cd2l = idr0 & SMMU_IDR0_CD2L,
        idr0_vmid = idr0 & SMMU_IDR0_VMID16,
        ats = idr0 & SMMU_IDR0_ATS,
        ttf0 = (idr0 >> 2) & 0x1,
        ttf1 = (idr0 >> 3) & 0x1;

    int ssidsz = (smmu_read32_reg(s, SMMU_REG_IDR1) >> 6) & 0x1f;

    uint32_t ste_vmid = STE_S2VMID(ste),
        ste_eats = STE_EATS(ste),
        ste_s2s = STE_S2S(ste),
        ste_s1fmt = STE_S1FMT(ste),
        aa64 = STE_S2AA64(ste),
        ste_s1cdmax = STE_S1CDMAX(ste);

    uint8_t ste_strw = STE_STRW(ste);
    uint64_t oas, max_pa;
    bool strw_ign;
    bool addr_out_of_range;

    if (!STE_VALID(ste)) {
        SMMU_DPRINTF(STE, "STE NOT valid\n");
        return false;
    }

    switch (STE_S2TG(ste)) {
    case 1:
        granule_supported = 0x4; break;
    case 2:
        granule_supported = 0x2; break;
    case 0:
        granule_supported = 0x1; break;
    }
    granule_supported &= (idr5 >> 4);

    if (!config[2]) {
        if ((!s1p && config[0]) ||
            (!s2p && config[1]) ||
            (s2p && config[1])) {
            SMMU_DPRINTF(STE, "STE inconsistant, S2P mismatch\n");
            return false;
        }
        if (!ssidsz && ste_s1cdmax && config[0] && !cd2l &&
            (ste_s1fmt == 1 || ste_s1fmt == 2)) {
            SMMU_DPRINTF(STE, "STE inconsistant, CD mismatch\n");
            return false;
        }
        if (ats && ((_config & 0x3) == 0) &&
            ((ste_eats == 2 && (_config != 0x7 || ste_s2s)) ||
             (ste_eats == 1 && !ste_s2s))) {
            SMMU_DPRINTF(STE, "STE inconsistant, EATS/S2S mismatch\n");
            return false;
        }
        if (config[0] && (ssidsz && (ste_s1cdmax > ssidsz))) {
            SMMU_DPRINTF(STE, "STE inconsistant, SSID out of range\n");
            return false;
        }
    }

    oas = MIN(STE_S2PS(ste), idr5 & 0x7);

    if (oas == 3) {
        max_pa = deposit64(0, 0, 42, ~0UL);
    } else {
        max_pa = deposit64(0, 0, (32 + (oas * 4)), ~0UL);
    }

    strw_ign = (!s1p || !hyp || (_config == 4));

    addr_out_of_range = (int64_t)(max_pa - STE_S2TTB(ste)) < 0;

    if (config[1] && (
        (aa64 && !granule_supported) ||
        (!aa64 && !ttf0) ||
        (aa64 && !ttf1)  ||
        ((STE_S2HA(ste) || STE_S2HD(ste)) && !aa64) ||
        ((STE_S2HA(ste) || STE_S2HD(ste)) && !httu) ||
        (STE_S2HD(ste) && (httu == 1)) ||
        addr_out_of_range)) {
        SMMU_DPRINTF(STE, "STE inconsistant\n");
        SMMU_DPRINTF(STE, "config[1]:%d gran:%d addr:%d\n"
                     " aa64:%d ttf0:%d ttf1:%d s2ha:%d s2hd:%d httu:%d\n",
                     config[1], granule_supported,
                     addr_out_of_range, aa64, ttf0, ttf1, STE_S2HA(ste),
                     STE_S2HD(ste), httu);
        SMMU_DPRINTF(STE, "maxpa:%lx s2ttb:%lx\n", max_pa, STE_S2TTB(ste));
        return false;
    }
    if (s2p && (config[0] == 0 && config[1]) &&
        (strw_ign || !ste_strw) && !idr0_vmid && !(ste_vmid >> 8)) {
        SMMU_DPRINTF(STE, "STE inconsistant, VMID out of range\n");
        return false;
    }

    return true;
}

static int tg2granule(int bits, bool tg1)
{
    switch (bits) {
    case 1:
        return tg1 ? 14 : 16;
    case 2:
        return tg1 ? 14 : 12;
    case 3:
        return tg1 ? 16 : 12;
    default:
        return 12;
    }
}

static inline int oas2bits(int oas)
{
    switch (oas) {
    case 2:
        return 40;
    case 3:
        return 42;
    case 4:
        return 44;
    case 5:
    default: return 48;
    }
}

#define STM2U64(stm) ({                                 \
            uint64_t hi, lo;                            \
            hi = (stm)->word[1];                        \
            lo = (stm)->word[0] & ~(uint64_t)0x1f;      \
            hi << 32 | lo;                              \
        })

#define STMSPAN(stm) (1 << (extract32((stm)->word[0], 0, 4) - 1))

static int smmu_find_ste(SMMUV3State *s, uint16_t sid, Ste *ste)
{
    hwaddr addr;

    SMMU_DPRINTF(STE, "SID:%x\n", sid);
    /* Check SID range */
    if (sid > (1 << s->sid_size)) {
        return SMMU_EVT_C_BAD_SID;
    }
    SMMU_DPRINTF(STE, "features:%x\n", s->features);
    if (s->features & SMMU_FEATURE_2LVL_STE) {
        int span;
        hwaddr stm_addr;
        STEDesc stm;
        int l1_ste_offset, l2_ste_offset;
        SMMU_DPRINTF(STE, "no. ste: %x\n", s->sid_split);

        l1_ste_offset = sid >> s->sid_split;
        l2_ste_offset = sid & ((1 << s->sid_split) - 1);
        SMMU_DPRINTF(STE, "l1_off:%x, l2_off:%x\n", l1_ste_offset,
                     l2_ste_offset);
        stm_addr = (hwaddr)(s->strtab_base + l1_ste_offset * sizeof(stm));
        smmu_read_sysmem(stm_addr, &stm, sizeof(stm));

        SMMU_DPRINTF(STE, "strtab_base:%lx stm_addr:%lx\n"
                     "l1_ste_offset:%x l1(64):%#016lx\n",
                     s->strtab_base, stm_addr, l1_ste_offset,
                     STM2U64(&stm));

        span = STMSPAN(&stm);
        SMMU_DPRINTF(STE, "l2_ste_offset:%x ~ span:%d\n", l2_ste_offset, span);
        if (l2_ste_offset > span) {
            SMMU_DPRINTF(CRIT, "l2_ste_offset > span\n");
            return SMMU_EVT_C_BAD_STE;
        }
        addr = STM2U64(&stm) + l2_ste_offset * sizeof(*ste);
    } else {
        addr = s->strtab_base + sid * sizeof(*ste);
    }
    SMMU_DPRINTF(STE, "ste:%lx\n", addr);
    if (smmu_get_ste(s, addr, ste)) {
        SMMU_DPRINTF(CRIT, "Unable to Fetch STE\n");
        return SMMU_EVT_F_UUT;
    }

    return 0;
}

static void smmu_cfg_populate_s2(SMMUTransCfg *cfg, Ste *ste)
{                           /* stage 2 cfg */
    bool s2a64 = STE_S2AA64(ste);

    cfg->granule = STE_S2TG(ste);
    cfg->tsz = STE_S2T0SZ(ste);
    cfg->ttbr = STE_S2TTB(ste);
    cfg->oas = oas2bits(STE_S2PS(ste));

    if (s2a64) {
        cfg->tsz = MIN(cfg->tsz, 39);
        cfg->tsz = MAX(cfg->tsz, 16);
    }
    cfg->va_size = STE_S2AA64(ste) ? 64 : 32;
    cfg->granule_sz = tg2granule(cfg->granule, 0) - 3;
}

static void smmu_cfg_populate_s1(SMMUTransCfg *cfg, Cd *cd)
{                           /* stage 1 cfg */
    bool s1a64 = CD_AARCH64(cd);

    cfg->granule = (CD_EPD0(cd)) ? CD_TG1(cd) : CD_TG0(cd);
    cfg->tsz = (CD_EPD0(cd)) ? CD_T1SZ(cd) : CD_T0SZ(cd);
    cfg->ttbr = (CD_EPD0(cd)) ? CD_TTB1(cd) : CD_TTB0(cd);
    cfg->oas = oas2bits(CD_IPS(cd));

    if (s1a64) {
        cfg->tsz = MIN(cfg->tsz, 39);
        cfg->tsz = MAX(cfg->tsz, 16);
    }
    cfg->va_size = CD_AARCH64(cd) ? 64 : 32;
    cfg->granule_sz = tg2granule(cfg->granule, CD_EPD0(cd)) - 3;
}

static SMMUEvtErr smmu_walk_pgtable(SMMUV3State *s, Ste *ste, Cd *cd,
                                    IOMMUTLBEntry *tlbe, bool is_write)
{
    SMMUState *sys = SMMU_SYS_DEV(s);
    SMMUBaseClass *sbc = SMMU_DEVICE_GET_CLASS(sys);
    SMMUTransCfg _cfg[2] = {{{0,} } };
    SMMUTransCfg *s1cfg = &_cfg[0], *s2cfg = &_cfg[1];
    SMMUTransCfg *cfg = NULL;
    SMMUEvtErr retval = 0;
    uint32_t ste_cfg = STE_CONFIG(ste);
    uint32_t page_size = 0, perm = 0;
    hwaddr pa;                 /* Input address, output address */

    SMMU_DPRINTF(DBG1, "ste_cfg :%x\n", ste_cfg);
    /* Both Bypass, we dont need to do anything */
    if (ste_cfg == STE_CONFIG_S1BY_S2BY) {
        return 0;
    }

    SMMU_DPRINTF(TT_1, "Input addr: %lx ste_config:%d\n",
                 tlbe->iova, ste_cfg);

    if (ste_cfg & STE_CONFIG_S1TR_S2BY) {
        smmu_cfg_populate_s1(s1cfg, cd);

        s1cfg->oas = MIN(oas2bits(smmu_read32_reg(s, SMMU_REG_IDR5) & 0xf),
                         s1cfg->oas);
        /* fix ttbr - make top bits zero*/
        s1cfg->ttbr = extract64(s1cfg->ttbr, 0, s1cfg->oas);
        s1cfg->s2cfg = s2cfg;
        s1cfg->s2_needed = (STE_CONFIG(ste) == STE_CONFIG_S1TR_S2TR) ? 1 : 0;
        cfg = s1cfg;
        SMMU_DPRINTF(DBG1, "DONE: Stage1 tanslated: %lx\n ", s1cfg->pa);
    }

    if (ste_cfg & STE_CONFIG_S1BY_S2TR) {
        /* Stage2 only configuratoin */
        smmu_cfg_populate_s2(s2cfg, ste);

        s2cfg->oas = MIN(oas2bits(smmu_read32_reg(s, SMMU_REG_IDR5) & 0xf),
                         s2cfg->oas);
        /* fix ttbr - make top bits zero*/
        s2cfg->ttbr = extract64(s2cfg->ttbr, 0, s2cfg->oas);

        if (!cfg) {
            cfg = s2cfg;
        }

        SMMU_DPRINTF(DBG1, "DONE: Stage2 tanslated :%lx\n ", s2cfg->pa);
    }

    cfg->va = tlbe->iova;

    retval = sbc->translate_lpae(cfg, &page_size, &perm,
                                 is_write);
    if (retval != 0) {
        SMMU_DPRINTF(CRIT, "FAILED Stage1 translation\n");
        goto exit;
    }
    pa = cfg->pa;

    SMMU_DPRINTF(TT_1, "DONE: o/p addr:%lx mask:%x is_write:%d\n ",
                 pa, page_size - 1, is_write);
    tlbe->translated_addr = pa;
    tlbe->addr_mask = page_size - 1;
    tlbe->perm = perm;
exit:
    return retval;
}

static MemTxResult smmu_write_evtq(SMMUV3State *s, Evt *evt)
{
    SMMUQueue *q = &s->evtq;
    int ret = smmu_q_write(s, q, evt);
    uint32_t val = 0;

    val |= (q->wrap.prod << q->shift) | q->prod;

    smmu_write32_reg(s, SMMU_REG_EVTQ_PROD, val);

    return ret;
}

/*
 * Events created on the EventQ
 */
static void smmu_create_event(SMMUV3State *s, hwaddr iova,
                              uint32_t sid, bool is_write, int error)
{
    SMMUQueue *q = &s->evtq;
    uint64_t head;
    Evt evt;

    if (!smmu_evt_q_enabled(s)) {
        return;
    }

    EVT_SET_TYPE(&evt, error);
    EVT_SET_SID(&evt, sid);

    switch (error) {
    case SMMU_EVT_F_UUT:
    case SMMU_EVT_C_BAD_STE:
        break;
    case SMMU_EVT_C_BAD_CD:
    case SMMU_EVT_F_CD_FETCH:
        break;
    case SMMU_EVT_F_TRANS_FORBIDDEN:
    case SMMU_EVT_F_WALK_EXT_ABRT:
        EVT_SET_INPUT_ADDR(&evt, iova);
    default:
        break;
    }

    smmu_write_evtq(s, &evt);

    head = Q_IDX(q, q->prod);

    if (smmu_is_q_full(s, &s->evtq)) {
        head = q->prod ^ (1 << 31);     /* Set overflow */
    }

    smmu_write32_reg(s, SMMU_REG_EVTQ_PROD, head);

    smmu_irq_raise(s, SMMU_IRQ_EVTQ, (uint64_t)&evt);
}

/*
 * TR - Translation Request
 * TT - Translated Tansaction
 * OT - Other Transaction
 */
static IOMMUTLBEntry
smmuv3_translate(MemoryRegion *mr, hwaddr addr, bool is_write)
{
    SMMUDevice *sdev = container_of(mr, SMMUDevice, iommu);
    SMMUV3State *s = sdev->smmu;
    uint16_t sid = 0, config;
    Ste ste;
    Cd cd;
    SMMUEvtErr error = 0;

    IOMMUTLBEntry ret = {
        .target_as = &address_space_memory,
        .iova = addr,
        .translated_addr = addr,
        .addr_mask = ~(hwaddr)0,
        .perm = IOMMU_NONE,
    };

    /* SMMU Bypass */
    /* We allow traffic through if SMMU is disabled */
    if (!smmu_enabled(s)) {
        SMMU_DPRINTF(CRIT, "SMMU Not enabled.. bypassing addr:%lx\n", addr);
        goto bypass;
    }

    sid = smmu_get_sid(sdev);
    SMMU_DPRINTF(TT_1, "SID:%x bus:%d ste_base:%lx\n",
                 sid, pci_bus_num(sdev->bus), s->strtab_base);

    /* Fetch & Check STE */
    error = smmu_find_ste(s, sid, &ste);
    if (error) {
        goto error_out;  /* F_STE_FETCH or F_CFG_CONFLICT */
    }

    if (IS_DBG_ENABLED(STE)) {
        dump_ste(&ste);
    }

    if (is_ste_valid(s, &ste) && is_ste_bypass(s, &ste)) {
        goto bypass;
    }

    SMMU_DPRINTF(STE, "STE is not bypass\n");
    if (!is_ste_consistent(s, &ste)) {
        error = SMMU_EVT_C_BAD_STE;
        goto error_out;
    }
    SMMU_DPRINTF(INFO, "Valid STE Found\n");

    /* Stream Bypass */
    config = STE_CONFIG(&ste) & 0x3;
    /*
     * Mostly we have S1-Translate and S2-Bypass, Others will be
     * implemented as we go
     */
    if (config == STE_CONFIG_S1BY_S2BY) {
        goto bypass;
    }

    if (config & (STE_CONFIG_S1TR_S2BY)) {
        smmu_get_cd(s, &ste, 0, &cd); /* We dont have SSID yet, so 0 */
        SMMU_DPRINTF(CRIT, "GET_CD\n");
        if (1 || IS_DBG_ENABLED(CD)) {
            dump_cd(&cd);
        }

        if (!is_cd_valid(s, &ste, &cd)) {
            error = SMMU_EVT_C_BAD_CD;
            goto error_out;
        }
    }

    /* Walk Stage1, if S2 is enabled, S2 walked for Every access on S1 */
    error = smmu_walk_pgtable(s, &ste, &cd, &ret, is_write);

    SMMU_DPRINTF(INFO, "DONE walking tables(1)\n");
error_out:
    if (error) {        /* Post the Error using Event Q */
        SMMU_DPRINTF(CRIT, "Translation Error: %x\n", error);
        smmu_create_event(s, ret.iova, sid, is_write, error);
        goto out;
    }

bypass:
    ret.perm = is_write ? IOMMU_RW : IOMMU_RO;

out:
    return ret;
}

static const MemoryRegionIOMMUOps smmu_iommu_ops = {
    .translate = smmuv3_translate,
};

static AddressSpace *smmu_init_pci_iommu(PCIBus *bus, void *opaque, int devfn)
{
    SMMUV3State *s = opaque;
    SMMUDevice *sdev = &s->pbdev[PCI_SLOT(devfn)];
    SMMUState *sys = SMMU_SYS_DEV(s);

    sdev->smmu = s;
    sdev->bus = bus;
    sdev->devfn = devfn;

    memory_region_init_iommu(&sdev->iommu, OBJECT(sys),
                             &smmu_iommu_ops, TYPE_SMMU_V3_DEV, UINT64_MAX);


    sdev->asp = address_space_init_shareable(&sdev->iommu, NULL);

    return sdev->asp;
}

static void smmu_write_mmio(void *opaque, hwaddr addr,
                            uint64_t val, unsigned size)
{
    SMMUState *sys = opaque;
    SMMUV3State *s = SMMU_V3_DEV(sys);

    switch (addr) {
    /* Unlikely event */
    case SMMU_REG_CR0_ACK:
    case SMMU_REG_STATUSR:
    case SMMU_REG_GERROR:
    case SMMU_REG_IRQ_CTRL_ACK:
    case 0xFDC ... 0xFFC:
    case SMMU_REG_IDR0 ... SMMU_REG_IDR5:
        SMMU_DPRINTF(CRIT, "write to RO/Unimpl reg %lx val64:%lx\n",
                     addr, val);
        return;
        /* Some 64bit writes are done as if its 2 * 32-bit write */
    case SMMU_REG_STRTAB_BASE + 4:
    case SMMU_REG_EVTQ_BASE + 4:
    case SMMU_REG_CMDQ_BASE + 4: {
        uint64_t tmp = smmu_read64_reg(s, addr - 4);
        tmp &= 0xffffffffULL;
        tmp |=  (val & 0xffffffff) << 32;
        smmu_write_reg(s, addr - 4, tmp);
    }
        break;
    case 0x100a8: case 0x100ac:         /* Aliasing => page0 registers */
    case 0x100c8: case 0x100cc:
        addr ^= (hwaddr)0x10000;
    default:
        smmu_write_reg(s, addr, val);
        break;
    }

    SMMU_DPRINTF(DBG2, "reg:%lx new: %lx\n", addr, val);
}

static uint64_t smmu_read_mmio(void *opaque, hwaddr addr, unsigned size)
{
    SMMUState *sys = opaque;
    SMMUV3State *s = SMMU_V3_DEV(sys);
    uint64_t val;

    /* Primecell/Corelink ID registers */
    switch (addr) {
    case 0xFF0 ... 0xFFC:
        val = (uint64_t)sys->cid[(addr - 0xFF0) >> 2];
        break;

    case 0xFDC ... 0xFE4:
        val = (uint64_t)sys->pid[(addr - 0xFDC) >> 2];
        break;

    case 0x100a8: case 0x100ac:         /* Aliased EVTQ_CONS/EVTQ_PROD */
    case 0x100c8: case 0x100cc:         /* Aliased PRIQ_CONS/PRIQ_PROD */
        addr ^= (hwaddr)0x10000;
    default:
        val = (uint64_t)smmu_read32_reg(s, addr);
        break;

    case SMMU_REG_STRTAB_BASE ... SMMU_REG_CMDQ_BASE:
    case SMMU_REG_EVTQ_BASE:
    case SMMU_REG_PRIQ_BASE ... SMMU_REG_PRIQ_IRQ_CFG1:
        val = smmu_read64_reg(s, addr);
        break;
    }

    SMMU_DPRINTF(DBG2, "addr: %lx val:%lx\n", addr, val);
    return val;
}

static const MemoryRegionOps smmu_mem_ops = {
    .read = smmu_read_mmio,
    .write = smmu_write_mmio,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static void smmu_init_irq(SMMUV3State *s, SysBusDevice *dev)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(s->irq); i++) {
        sysbus_init_irq(dev, &s->irq[i]);
    }
}

static void smmu_init_iommu_as(SMMUV3State *sys)
{
    SMMUState *s = SMMU_SYS_DEV(sys);
    PCIBus *pcibus = pci_find_primary_bus();

    if (pcibus) {
        SMMU_DPRINTF(CRIT, "Found PCI bus, setting up iommu\n");
        pci_setup_iommu(pcibus, smmu_init_pci_iommu, s);
    } else {
        SMMU_DPRINTF(CRIT, "Could'nt find PCI bus, SMMU is not registered\n");
    }
}

static void smmu_reset(DeviceState *dev)
{
    SMMUV3State *s = SMMU_V3_DEV(dev);
    smmuv3_init(s);
}

static int smmuv3_get_reg_state(QEMUFile *f, void *pv, size_t size)
{
    RegInfo *r = pv;
    int i;
    for (i = 0; i < SMMU_NREGS; i++) {
        r[i].data = qemu_get_be64(f);
        r[i].rao_mask = qemu_get_be64(f);
        r[i].raz_mask = qemu_get_be64(f);
        r[i].post = (post_write_t)qemu_get_be64(f);
    }

    return 0;
}

static void smmuv3_put_reg_state(QEMUFile *f, void *pv, size_t size)
{
    RegInfo *r = pv;
    int i;
    for (i = 0; i < SMMU_NREGS; i++) {
        qemu_put_be64(f, r[i].data);
        qemu_put_be64(f, r[i].rao_mask);
        qemu_put_be64(f, r[i].raz_mask);
        qemu_put_be64(f, (uint64_t)r[i].post);
    }
}

static const VMStateInfo reg_state_info = {
    .name = "reg_state",
    .get = smmuv3_get_reg_state,
    .put = smmuv3_put_reg_state,
};

static int smmu_populate_internal_state(void *opaque, int version_id)
{
    /*
     * TODO: Need to restore state by re-reading registers
     */
    return 0;
}

static void smmu_realize(DeviceState *d, Error **errp)
{
    SMMUState *sys = SMMU_SYS_DEV(d);
    SMMUV3State *s = SMMU_V3_DEV(sys);
    SysBusDevice *dev = SYS_BUS_DEVICE(d);

    /* Register Access */
    memory_region_init_io(&sys->iomem, OBJECT(s),
                          &smmu_mem_ops, sys, TYPE_SMMU_V3_DEV, 0x20000);

    sysbus_init_mmio(dev, &sys->iomem);

    smmu_init_irq(s, dev);

    smmu_init_iommu_as(s);
}

static const VMStateDescription vmstate_smmu = {
    .name = "smmu",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = smmu_populate_internal_state,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(cid, SMMUState, 4),
        VMSTATE_UINT32_ARRAY(pid, SMMUState, 8),
        VMSTATE_ARRAY(regs, SMMUV3State, SMMU_NREGS,
                      0, reg_state_info, RegInfo),
        VMSTATE_END_OF_LIST(),
    },
};

static void smmu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SMMUBaseClass *sbc = SMMU_DEVICE_CLASS(klass);

    sbc->translate_lpae = smmu_translate_lpae;

    dc->reset = smmu_reset;
    dc->vmsd = &vmstate_smmu;
    dc->realize = smmu_realize;
}

static void smmu_base_instance_init(Object *obj)
{
    SMMUV3State *s = SMMU_V3_DEV(obj);
    int i;

    for (i = 0; i < PCI_DEVFN_MAX; i++) {
        char *name = g_strdup_printf("mr-%d", i);
        object_property_add_link(obj, name, TYPE_MEMORY_REGION,
                                 (Object **)&s->pbdev[i].iommu,
                                 qdev_prop_allow_set_link_before_realize,
                                 OBJ_PROP_LINK_UNREF_ON_RELEASE,
                                 NULL);
        g_free(name);
    }
}

static void smmu_instance_init(Object *obj)
{
    int i;
    SMMUV3State *s = SMMU_V3_DEV(obj);

    for (i = 0; i < PCI_DEVFN_MAX; i++) {
        s->pbdev[i].smmu = s;
    }
}

static const TypeInfo smmu_base_info = {
    .name          = TYPE_SMMU_DEV_BASE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SMMUV3State),
    .instance_init = smmu_base_instance_init,
    .class_size    = sizeof(SMMUBaseClass),
    .abstract      = true,
};

static void smmu_register_types(void)
{
    TypeInfo type_info = {
        .name = TYPE_SMMU_V3_DEV,
        .parent = TYPE_SMMU_DEV_BASE,
        .class_data = NULL,
        .class_init = smmu_class_init,
        .instance_init = smmu_instance_init,
    };

    type_register_static(&smmu_base_info);

    type_register(&type_info);
}

type_init(smmu_register_types)

