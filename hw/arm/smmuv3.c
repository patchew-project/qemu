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

#include "qemu/osdep.h"
#include "hw/boards.h"
#include "sysemu/sysemu.h"
#include "hw/sysbus.h"
#include "hw/pci/pci.h"
#include "exec/address-spaces.h"

#include "hw/arm/smmuv3.h"
#include "smmuv3-internal.h"

static inline int smmu_enabled(SMMUV3State *s)
{
    return smmu_read32_reg(s, SMMU_REG_CR0) & SMMU_CR0_SMMU_ENABLE;
}

/**
 * smmu_irq_update - update the GERROR register according to
 * the IRQ and the enable state
 *
 * return > 0 when IRQ is supposed to be raised
 * Spec req:
 * Raise irq only when it not active already,
 * blindly toggling bits may actually clear the error
 */
static int smmu_irq_update(SMMUV3State *s, int irq, uint64_t data)
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

    return smmu_read_sysmem(addr, data, q->ent_size, false);
}

static MemTxResult smmu_q_write(SMMUV3State *s, SMMUQueue *q, void *data)
{
    uint64_t addr = Q_ENTRY(q, Q_IDX(q, q->prod));

    if (q->prod == q->entries) {
        q->prod = 0;
        q->wrap.prod++;     /* this will toggle */
    }

    q->prod++;

    smmu_write_sysmem(addr, data, q->ent_size, false);

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

static int smmu_cmdq_consume(SMMUV3State *s)
{
    uint32_t error = SMMU_CMD_ERR_NONE;

    SMMU_DPRINTF(CMDQ, "CMDQ_ERR: %d\n", SMMU_CMDQ_ERR(s));

    if (!smmu_cmd_q_enabled(s)) {
        goto out;
    }

    while (!SMMU_CMDQ_ERR(s) && !smmu_is_q_empty(s, &s->cmdq)) {
        Cmd cmd;
#ifdef ARM_SMMU_DEBUG
        SMMUQueue *q = &s->cmdq;
#endif
        if (smmu_read_cmdq(s, &cmd) != MEMTX_OK) {
            error = SMMU_CMD_ERR_ABORT;
            goto out;
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
            goto out;
        }
    }

out:
    if (error) {
        smmu_irq_raise(s, SMMU_IRQ_GERROR, error);
    }

    SMMU_DPRINTF(CMDQ, "prod_wrap:%d, prod:%x cons_wrap:%d cons:%x\n",
                 s->cmdq.wrap.prod, s->cmdq.prod,
                 s->cmdq.wrap.cons, s->cmdq.cons);

    return 0;
}

/*
 * GERROR is updated when raising an interrupt, GERRORN will be updated
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
        if (smmu_evt_irq_enabled(s)) {
            smmu_irq_raise(s, SMMU_IRQ_EVTQ, 0);
        }
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
    } else {
        SMMU_DPRINTF(INFO, "cmdq not enabled or error :%x\n", SMMU_CMDQ_ERR(s));
    }

}

static void smmu_update_irq(SMMUV3State *s, uint64_t addr, uint64_t val)
{
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

#define SMMU_ID_REG_INIT(s, reg, d) do {        \
    s->regs[reg >> 2] = d;                      \
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

static void smmuv3_init(SMMUV3State *s)
{
    smmuv3_id_reg_init(s);      /* Update ID regs alone */

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
        smmu_read_sysmem(stm_addr, &stm, sizeof(stm), false);

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
    const int stage = 2;

    cfg->granule[stage] = STE_S2TG(ste);
    cfg->tsz[stage] = STE_S2T0SZ(ste);
    cfg->ttbr[stage] = STE_S2TTB(ste);
    cfg->oas[stage] = oas2bits(STE_S2PS(ste));

    if (s2a64) {
        cfg->tsz[stage] = MIN(cfg->tsz[stage], 39);
        cfg->tsz[stage] = MAX(cfg->tsz[stage], 16);
    }
    cfg->va_size[stage] = STE_S2AA64(ste) ? 64 : 32;
    cfg->granule_sz[stage] = tg2granule(cfg->granule[stage], 0) - 3;
}

static void smmu_cfg_populate_s1(SMMUTransCfg *cfg, Cd *cd)
{                           /* stage 1 cfg */
    bool s1a64 = CD_AARCH64(cd);
    const int stage = 1;

    cfg->granule[stage] = (CD_EPD0(cd)) ? CD_TG1(cd) : CD_TG0(cd);
    cfg->tsz[stage] = (CD_EPD0(cd)) ? CD_T1SZ(cd) : CD_T0SZ(cd);
    cfg->ttbr[stage] = (CD_EPD0(cd)) ? CD_TTB1(cd) : CD_TTB0(cd);
    cfg->oas[stage] = oas2bits(CD_IPS(cd));

    if (s1a64) {
        cfg->tsz[stage] = MIN(cfg->tsz[stage], 39);
        cfg->tsz[stage] = MAX(cfg->tsz[stage], 16);
    }
    cfg->va_size[stage] = CD_AARCH64(cd) ? 64 : 32;
    cfg->granule_sz[stage] = tg2granule(cfg->granule[stage], CD_EPD0(cd)) - 3;
}

static SMMUEvtErr smmu_walk_pgtable(SMMUV3State *s, Ste *ste, Cd *cd,
                                    IOMMUTLBEntry *tlbe, bool is_write)
{
    SMMUState *sys = SMMU_SYS_DEV(s);
    SMMUBaseClass *sbc = SMMU_DEVICE_GET_CLASS(sys);
    SMMUTransCfg _cfg = {};
    SMMUTransCfg *cfg = &_cfg;
    SMMUEvtErr retval = 0;
    uint32_t ste_cfg = STE_CONFIG(ste);
    uint32_t page_size = 0, perm = 0;
    hwaddr pa;                 /* Input address, output address */
    int stage = 0;

    SMMU_DPRINTF(DBG1, "ste_cfg :%x\n", ste_cfg);
    /* Both Bypass, we dont need to do anything */
    if (is_ste_bypass(s, ste)) {
        return 0;
    }

    SMMU_DPRINTF(TT_1, "Input addr: %lx ste_config:%d\n",
                 tlbe->iova, ste_cfg);

    if (ste_cfg & STE_CONFIG_S1TR) {
        stage = cfg->stage = 1;
        smmu_cfg_populate_s1(cfg, cd);

        cfg->oas[stage] = MIN(oas2bits(smmu_read32_reg(s, SMMU_REG_IDR5) & 0xf),
                              cfg->oas[stage]);
        /* fix ttbr - make top bits zero*/
        cfg->ttbr[stage] = extract64(cfg->ttbr[stage], 0, cfg->oas[stage]);
        cfg->s2_needed = (STE_CONFIG(ste) == STE_CONFIG_S1TR_S2TR) ? 1 : 0;

        SMMU_DPRINTF(DBG1, "S1 populated\n ");
    }

    if (ste_cfg & STE_CONFIG_S2TR) {
        stage = 2;
        if (cfg->stage) {               /* S1+S2 */
            cfg->s2_needed = true;
        } else {                        /* Stage2 only */
            cfg->stage = stage;
        }

        /* Stage2 only configuratoin */
        smmu_cfg_populate_s2(cfg, ste);

        cfg->oas[stage] = MIN(oas2bits(smmu_read32_reg(s, SMMU_REG_IDR5) & 0xf),
                              cfg->oas[stage]);
        /* fix ttbr - make top bits zero*/
        cfg->ttbr[stage] = extract64(cfg->ttbr[stage], 0, cfg->oas[stage]);

        SMMU_DPRINTF(DBG1, "S2 populated\n ");
    }

    cfg->va = tlbe->iova;

    if ((cfg->stage == 1 && CD_AARCH64(cd)) ||
        STE_S2AA64(ste)) {
        SMMU_DPRINTF(DBG1, "Translate 64\n");
        retval = sbc->translate_64(cfg, &page_size, &perm,
                                   is_write);
    } else {
        SMMU_DPRINTF(DBG1, "Translate 32\n");
        retval = sbc->translate_32(cfg, &page_size, &perm, is_write);
    }

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
    dump_smmutranscfg(cfg);
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

    /* SMMU Bypass, We allow traffic through if SMMU is disabled  */
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

    if (config & (STE_CONFIG_S1TR)) {
        smmu_get_cd(s, &ste, 0, &cd); /* We dont have SSID yet, so 0 */
        SMMU_DPRINTF(INFO, "GET_CD CTXPTR:%p\n", (void *)STE_CTXPTR(&ste));
        if (IS_DBG_ENABLED(CD)) {
            dump_cd(&cd);
        }

        if (!is_cd_valid(s, &ste, &cd)) {
            error = SMMU_EVT_C_BAD_CD;
            goto error_out;
        }
    }

    /* Walk Stage1, if S2 is enabled, S2 walked for Every access on S1 */
    error = smmu_walk_pgtable(s, &ste, &cd, &ret, is_write);

    SMMU_DPRINTF(INFO, "DONE walking tables\n");

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
    SMMUState *sys = SMMU_SYS_DEV(s);
    uintptr_t key = (uintptr_t)bus;
    SMMUPciBus *sbus = g_hash_table_lookup(s->smmu_as_by_busptr, &key);
    SMMUDevice *sdev;

    if (!sbus) {
        sbus = g_malloc0(sizeof(SMMUPciBus) +
                         sizeof(SMMUDevice *) * SMMU_PCI_DEVFN_MAX);
        sbus->bus = bus;
        g_hash_table_insert(s->smmu_as_by_busptr, &key, sbus);
    }

    sdev = sbus->pbdev[devfn];
    if (!sdev) {
        sdev = sbus->pbdev[devfn] = g_malloc0(sizeof(SMMUDevice));

        sdev->smmu = s;
        sdev->bus = bus;
        sdev->devfn = devfn;

        memory_region_init_iommu(&sdev->iommu, OBJECT(sys),
                                 &smmu_iommu_ops, TYPE_SMMU_V3_DEV, UINT64_MAX);
        address_space_init(&sdev->as, &sdev->iommu, TYPE_SMMU_V3_DEV);
    }

    return &sdev->as;
}

static inline void smmu_update_base_reg(SMMUV3State *s, uint64_t *base,
                                        uint64_t val)
{
    *base = val & ~(SMMU_BASE_RA | 0x3fULL);
}

static void smmu_update_qreg(SMMUV3State *s, SMMUQueue *q, hwaddr reg,
                             uint32_t off, uint64_t val, unsigned size)
{
    if (size == 8 && off == 0) {
        smmu_write64_reg(s, reg, val);
    } else {
        smmu_write_reg(s, reg, val);
    }

    switch (off) {
    case 0:                             /* BASE register */
        val = smmu_read64_reg(s, reg);
        q->shift = val & 0x1f;
        q->entries = 1 << (q->shift);
        smmu_update_base_reg(s, &q->base, val);
        break;

    case 4:                             /* CONS */
        q->cons = Q_IDX(q, val);
        q->wrap.cons = val >> q->shift;
        SMMU_DPRINTF(DBG2, "cons written : %d val:%lx\n", q->cons, val);
        break;

    case 8:                             /* PROD */
        q->prod = Q_IDX(q, val);
        q->wrap.prod = val >> q->shift;
        break;
    }

    switch (reg) {
    case SMMU_REG_CMDQ_PROD:            /* should be only for CMDQ_PROD */
    case SMMU_REG_CMDQ_CONS:            /* but we do it anyway */
        smmu_update(s);
        break;
    }
}

static void smmu_write_mmio_fixup(SMMUV3State *s, hwaddr *addr)
{
    switch (*addr) {
    case 0x100a8: case 0x100ac:         /* Aliasing => page0 registers */
    case 0x100c8: case 0x100cc:
        *addr ^= (hwaddr)0x10000;
    }
}

static void smmu_write_mmio(void *opaque, hwaddr addr,
                            uint64_t val, unsigned size)
{
    SMMUState *sys = opaque;
    SMMUV3State *s = SMMU_V3_DEV(sys);
    bool update = false;

    smmu_write_mmio_fixup(s, &addr);

    SMMU_DPRINTF(DBG2, "addr: %lx val:%lx\n", addr, val);

    switch (addr) {
    case 0xFDC ... 0xFFC:
    case SMMU_REG_IDR0 ... SMMU_REG_IDR5:
        SMMU_DPRINTF(CRIT, "write to RO/Unimpl reg %lx val64:%lx\n",
                     addr, val);
        return;

    case SMMU_REG_GERRORN:
        smmu_update_irq(s, addr, val);
        return;

    case SMMU_REG_CR0:
        smmu_write32_reg(s, SMMU_REG_CR0_ACK, val);
        update = true;
        break;

    case SMMU_REG_IRQ_CTRL:
        smmu_write32_reg(s, SMMU_REG_IRQ_CTRL_ACK, val);
        update = true;
        break;

    case SMMU_REG_STRTAB_BASE:
        smmu_update_base_reg(s, &s->strtab_base, val);
        return;

    case SMMU_REG_STRTAB_BASE_CFG:
        if (((val >> 16) & 0x3) == 0x1) {
            s->sid_split = (val >> 6) & 0x1f;
            s->features |= SMMU_FEATURE_2LVL_STE;
        }
        break;

    case SMMU_REG_CMDQ_PROD:
    case SMMU_REG_CMDQ_CONS:
    case SMMU_REG_CMDQ_BASE:
    case SMMU_REG_CMDQ_BASE + 4:
        smmu_update_qreg(s, &s->cmdq, addr, addr - SMMU_REG_CMDQ_BASE,
                         val, size);
        return;

    case SMMU_REG_EVTQ_CONS:            /* fallthrough */
    {
        SMMUQueue *evtq = &s->evtq;
        evtq->cons = Q_IDX(evtq, val);
        evtq->wrap.cons = Q_WRAP(evtq, val);

        SMMU_DPRINTF(IRQ, "Before clearing interrupt "
                     "prod:%x cons:%x prod.w:%d cons.w:%d\n",
                     evtq->prod, evtq->cons, evtq->wrap.prod, evtq->wrap.cons);
        if (smmu_is_q_empty(s, &s->evtq)) {
            SMMU_DPRINTF(IRQ, "Clearing interrupt"
                         " prod:%x cons:%x prod.w:%d cons.w:%d\n",
                         evtq->prod, evtq->cons, evtq->wrap.prod,
                         evtq->wrap.cons);
            qemu_irq_lower(s->irq[SMMU_IRQ_EVTQ]);
        }
    }
    case SMMU_REG_EVTQ_BASE:
    case SMMU_REG_EVTQ_BASE + 4:
    case SMMU_REG_EVTQ_PROD:
        smmu_update_qreg(s, &s->evtq, addr, addr - SMMU_REG_EVTQ_BASE,
                         val, size);
        return;

    case SMMU_REG_PRIQ_CONS:
    case SMMU_REG_PRIQ_BASE:
    case SMMU_REG_PRIQ_BASE + 4:
    case SMMU_REG_PRIQ_PROD:
        smmu_update_qreg(s, &s->priq, addr, addr - SMMU_REG_PRIQ_BASE,
                         val, size);
        return;
    }

    if (size == 8) {
        smmu_write_reg(s, addr, val);
    } else {
        smmu_write32_reg(s, addr, (uint32_t)val);
    }

    if (update) {
        smmu_update(s);
    }
}

static uint64_t smmu_read_mmio(void *opaque, hwaddr addr, unsigned size)
{
    SMMUState *sys = opaque;
    SMMUV3State *s = SMMU_V3_DEV(sys);
    uint64_t val;

    smmu_write_mmio_fixup(s, &addr);

    /* Primecell/Corelink ID registers */
    switch (addr) {
    case 0xFF0 ... 0xFFC:
    case 0xFDC ... 0xFE4:
        val = 0;
        SMMU_DPRINTF(CRIT, "***************** addr: %lx val:%lx\n", addr, val);
        break;

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
    SMMU_DPRINTF(DBG2, "cmdq cons:%d\n", s->cmdq.cons);
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
        SMMU_DPRINTF(CRIT, "No PCI bus, SMMU is not registered\n");
    }
}

static void smmu_reset(DeviceState *dev)
{
    SMMUV3State *s = SMMU_V3_DEV(dev);
    smmuv3_init(s);
}

static int smmu_populate_internal_state(void *opaque, int version_id)
{
    SMMUV3State *s = opaque;

    smmu_update(s);
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

    s->smmu_as_by_busptr = g_hash_table_new_full(smmu_uint64_hash,
                                                 smmu_uint64_equal,
                                                 g_free, g_free);
    sysbus_init_mmio(dev, &sys->iomem);

    smmu_init_irq(s, dev);

    smmu_init_iommu_as(s);
}

static const VMStateDescription vmstate_smmuv3 = {
    .name = "smmuv3",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = smmu_populate_internal_state,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64_ARRAY(regs, SMMUV3State, SMMU_NREGS),
        VMSTATE_END_OF_LIST(),
    },
};

static void smmuv3_instance_init(Object *obj)
{
    /* Nothing much to do here as of now */
}

static void smmuv3_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset   = smmu_reset;
    dc->vmsd    = &vmstate_smmuv3;
    dc->realize = smmu_realize;
}

static const TypeInfo smmuv3_type_info = {
    .name          = TYPE_SMMU_V3_DEV,
    .parent        = TYPE_SMMU_DEV_BASE,
    .instance_size = sizeof(SMMUV3State),
    .instance_init = smmuv3_instance_init,
    .class_data    = NULL,
    .class_size    = sizeof(SMMUV3Class),
    .class_init    = smmuv3_class_init,
};

static void smmuv3_register_types(void)
{
    type_register(&smmuv3_type_info);
}

type_init(smmuv3_register_types)

