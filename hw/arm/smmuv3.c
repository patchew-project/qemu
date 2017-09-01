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
#include "trace.h"
#include "qemu/error-report.h"
#include "exec/target_page.h"

#include "hw/arm/smmuv3.h"
#include "smmuv3-internal.h"

/**
 * smmuv3_irq_trigger - pulse @irq if enabled and update
 * GERROR register in case of GERROR interrupt
 *
 * @irq: irq type
 * @gerror: gerror new value, only relevant if @irq is GERROR
 */
static void smmuv3_irq_trigger(SMMUV3State *s, SMMUIrq irq, uint32_t gerror_val)
{
    uint32_t pending_gerrors = SMMU_PENDING_GERRORS(s);
    bool pulse = false;

    switch (irq) {
    case SMMU_IRQ_EVTQ:
        pulse = smmu_evt_irq_enabled(s);
        break;
    case SMMU_IRQ_PRIQ:
        pulse = smmu_pri_irq_enabled(s);
        break;
    case SMMU_IRQ_CMD_SYNC:
        pulse = true;
        break;
    case SMMU_IRQ_GERROR:
    {
        /* don't toggle an already pending error */
        bool new_gerrors = ~pending_gerrors & gerror_val;
        uint32_t gerror = smmu_read32_reg(s, SMMU_REG_GERROR);

        smmu_write32_reg(s, SMMU_REG_GERROR, gerror | new_gerrors);

        /* pulse the GERROR irq only if all fields were acked */
        pulse = smmu_gerror_irq_enabled(s) && !pending_gerrors;
        break;
    }
    }
    if (pulse) {
            trace_smmuv3_irq_trigger(irq,
                                     smmu_read32_reg(s, SMMU_REG_GERROR),
                                     SMMU_PENDING_GERRORS(s));
            qemu_irq_pulse(s->irq[irq]);
    }
}

static void smmuv3_write_gerrorn(SMMUV3State *s, uint32_t gerrorn)
{
    uint32_t pending_gerrors = SMMU_PENDING_GERRORS(s);
    uint32_t sanitized;

    /* Make sure SW does not toggle irqs that are not active */
    sanitized = gerrorn & pending_gerrors;

    smmu_write32_reg(s, SMMU_REG_GERRORN, sanitized);
    trace_smmuv3_write_gerrorn(gerrorn, sanitized, SMMU_PENDING_GERRORS(s));
}

static MemTxResult smmu_q_read(SMMUQueue *q, void *data)
{
    uint64_t addr = Q_ENTRY(q, Q_IDX(q, q->cons));
    MemTxResult ret;

    ret = smmu_read_sysmem(addr, data, q->ent_size, false);
    if (ret != MEMTX_OK) {
        return ret;
    }

    q->cons++;
    if (q->cons == q->entries) {
        q->cons = 0;
        q->wrap.cons++;
    }

    return ret;
}

static void smmu_q_write(SMMUQueue *q, void *data)
{
    uint64_t addr = Q_ENTRY(q, Q_IDX(q, q->prod));

    smmu_write_sysmem(addr, data, q->ent_size, false);

    q->prod++;
    if (q->prod == q->entries) {
        q->prod = 0;
        q->wrap.prod++;
    }
}

static MemTxResult smmuv3_read_cmdq(SMMUV3State *s, Cmd *cmd)
{
    SMMUQueue *q = &s->cmdq;
    MemTxResult ret = smmu_q_read(q, cmd);
    uint32_t val = 0;

    if (ret != MEMTX_OK) {
        return ret;
    }

    val |= (q->wrap.cons << q->shift) | q->cons;
    smmu_write32_reg(s, SMMU_REG_CMDQ_CONS, val);

    return ret;
}

static void smmuv3_write_evtq(SMMUV3State *s, Evt *evt)
{
    SMMUQueue *q = &s->evtq;
    bool was_empty = smmu_is_q_empty(s, q);
    bool was_full = smmu_is_q_full(s, q);
    uint32_t val;

    if (!smmu_evt_q_enabled(s)) {
        return;
    }

    if (was_full) {
        return;
    }

    smmu_q_write(q, evt);

    val = (q->wrap.prod << q->shift) | q->prod;
    smmu_write32_reg(s, SMMU_REG_EVTQ_PROD, val);

    if (was_empty) {
        smmuv3_irq_trigger(s, SMMU_IRQ_EVTQ, 0);
    }
}

/*
 * smmuv3_record_event - Record an event
 */
static void smmuv3_record_event(SMMUV3State *s, hwaddr iova,
                                uint32_t sid, IOMMUAccessFlags perm,
                                SMMUEvtErr type)
{
    Evt evt;
    bool rnw = perm & IOMMU_RO;

    if (!smmu_evt_q_enabled(s)) {
        return;
    }

    EVT_SET_TYPE(&evt, type);
    EVT_SET_SID(&evt, sid);
    /* SSV=0 (substream invalid) and substreamID= 0 */

    switch (type) {
    case SMMU_EVT_OK:
        return;
    case SMMU_EVT_F_UUT:
        EVT_SET_INPUT_ADDR(&evt, iova);
        EVT_SET_RNW(&evt, rnw);
        /* PnU and Ind not filled */
        break;
    case SMMU_EVT_C_BAD_SID:
        break;
    case SMMU_EVT_F_STE_FETCH:
        /* Implementation defined and FetchAddr not filled yet */
        break;
    case SMMU_EVT_C_BAD_STE:
        break;
    case SMMU_EVT_F_BAD_ATS_REQ:
        /* ATS not yet implemented */
        break;
    case SMMU_EVT_F_STREAM_DISABLED:
        break;
    case SMMU_EVT_F_TRANS_FORBIDDEN:
        EVT_SET_INPUT_ADDR(&evt, iova);
        EVT_SET_RNW(&evt, rnw);
        break;
    case SMMU_EVT_C_BAD_SSID:
        break;
    case SMMU_EVT_F_CD_FETCH:
        break;
    case SMMU_EVT_C_BAD_CD:
        /* Implementation defined and FetchAddr not filled yet */
        break;
    case SMMU_EVT_F_WALK_EXT_ABRT:
        EVT_SET_INPUT_ADDR(&evt, iova);
        EVT_SET_RNW(&evt, rnw);
        /* Reason, Class, S2, Ind, PnU, FetchAddr not filled yet */
        break;
    case SMMU_EVT_F_TRANS:
    case SMMU_EVT_F_ADDR_SZ:
    case SMMU_EVT_F_ACCESS:
        EVT_SET_INPUT_ADDR(&evt, iova);
        EVT_SET_RNW(&evt, rnw);
        /* STAG, Class, S2, InD, PnU, IPA not filled yet */
        break;
    case SMMU_EVT_F_PERM:
        EVT_SET_INPUT_ADDR(&evt, iova);
        EVT_SET_RNW(&evt, rnw);
        /* STAG, TTRnW, Class, S2, InD, PnU, IPA not filled yet */
        break;
    case SMMU_EVT_F_TLB_CONFLICT:
        EVT_SET_INPUT_ADDR(&evt, iova);
        EVT_SET_RNW(&evt, rnw);
        /* Reason, S2, InD, PnU, IPA not filled yet */
        break;
    case SMMU_EVT_F_CFG_CONFLICT:
        /* Implementation defined reason not filled yet */
        break;
    case SMMU_EVT_E_PAGE_REQ:
        /* PRI not supported */
        break;
    }

    smmuv3_write_evtq(s, &evt);
}

static void smmuv3_init_regs(SMMUV3State *s)
{
    uint32_t data =
        SMMU_IDR0_STLEVEL << SMMU_IDR0_STLEVEL_SHIFT |
        SMMU_IDR0_TERM    << SMMU_IDR0_TERM_SHIFT    |
        SMMU_IDR0_STALL   << SMMU_IDR0_STALL_SHIFT   |
        SMMU_IDR0_VMID16  << SMMU_IDR0_VMID16_SHIFT  |
        SMMU_IDR0_PRI     << SMMU_IDR0_PRI_SHIFT     |
        SMMU_IDR0_ASID16  << SMMU_IDR0_ASID16_SHIFT  |
        SMMU_IDR0_ATS     << SMMU_IDR0_ATS_SHIFT     |
        SMMU_IDR0_HYP     << SMMU_IDR0_HYP_SHIFT     |
        SMMU_IDR0_HTTU    << SMMU_IDR0_HTTU_SHIFT    |
        SMMU_IDR0_COHACC  << SMMU_IDR0_COHACC_SHIFT  |
        SMMU_IDR0_TTF     << SMMU_IDR0_TTF_SHIFT     |
        SMMU_IDR0_S1P     << SMMU_IDR0_S1P_SHIFT     |
        SMMU_IDR0_S2P     << SMMU_IDR0_S2P_SHIFT;

    smmu_write32_reg(s, SMMU_REG_IDR0, data);

#define SMMU_QUEUE_SIZE_LOG2  19
    data =
        1 << 27 |                    /* Attr Types override */
        SMMU_QUEUE_SIZE_LOG2 << 21 | /* Cmd Q size */
        SMMU_QUEUE_SIZE_LOG2 << 16 | /* Event Q size */
        SMMU_QUEUE_SIZE_LOG2 << 11 | /* PRI Q size */
        0  << 6 |                    /* SSID not supported */
        SMMU_IDR1_SIDSIZE;

    smmu_write32_reg(s, SMMU_REG_IDR1, data);

    s->sid_size = SMMU_IDR1_SIDSIZE;

    data = SMMU_IDR5_GRAN << SMMU_IDR5_GRAN_SHIFT | SMMU_IDR5_OAS;

    smmu_write32_reg(s, SMMU_REG_IDR5, data);
}

static void smmuv3_init_queues(SMMUV3State *s)
{
    s->cmdq.prod = 0;
    s->cmdq.cons = 0;
    s->cmdq.wrap.prod = 0;
    s->cmdq.wrap.cons = 0;

    s->evtq.prod = 0;
    s->evtq.cons = 0;
    s->evtq.wrap.prod = 0;
    s->evtq.wrap.cons = 0;

    s->cmdq.entries = SMMU_QUEUE_SIZE_LOG2;
    s->cmdq.ent_size = sizeof(Cmd);
    s->evtq.entries = SMMU_QUEUE_SIZE_LOG2;
    s->evtq.ent_size = sizeof(Evt);
}

static void smmuv3_init(SMMUV3State *s)
{
    smmuv3_init_regs(s);
    smmuv3_init_queues(s);
}

static inline void smmu_update_base_reg(SMMUV3State *s, uint64_t *base,
                                        uint64_t val)
{
    *base = val & ~(SMMU_BASE_RA | 0x3fULL);
}

/*
 * All SMMU data structures are little endian, and are aligned to 8 bytes
 * L1STE/STE/L1CD/CD, Queue entries in CMDQ/EVTQ/PRIQ
 */
static inline int smmu_get_ste(SMMUV3State *s, hwaddr addr, Ste *buf)
{
    trace_smmuv3_get_ste(addr);
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
        error_report("Multilevel Ctx Descriptor not supported yet");
    }

    trace_smmuv3_get_cd(addr);
    return dma_memory_read(&address_space_memory, addr, buf, sizeof(*buf));
}

/**
 * is_ste_consistent - Check validity of STE
 * according to 6.2.1 Validity of STE
 * TODO: check the relevance of each check and compliance
 * with this spec chapter
 */
static bool is_ste_consistent(SMMUV3State *s, Ste *ste)
{
    uint32_t _config = STE_CONFIG(ste);
    uint32_t ste_vmid, ste_eats, ste_s2s, ste_s1fmt, ste_s2aa64, ste_s1cdmax;
    uint32_t ste_strw;
    bool strw_unused, addr_out_of_range, granule_supported;
    bool config[] = {_config & 0x1, _config & 0x2, _config & 0x3};

    ste_vmid = STE_S2VMID(ste);
    ste_eats = STE_EATS(ste); /* Enable PCIe ATS trans */
    ste_s2s = STE_S2S(ste);
    ste_s1fmt = STE_S1FMT(ste);
    ste_s2aa64 = STE_S2AA64(ste);
    ste_s1cdmax = STE_S1CDMAX(ste); /*CD bit # S1ContextPtr */
    ste_strw = STE_STRW(ste); /* stream world control */

    if (!STE_VALID(ste)) {
        error_report("STE NOT valid");
        return false;
    }

    granule_supported = is_s2granule_valid(ste);

    /* As S1/S2 combinations are supported do not check
     * corresponding STE config values */

    if (!config[2]) {
        /* Report abort to device, no event recorded */
        error_report("STE config 0b000 not implemented");
        return false;
    }

    if (!SMMU_IDR1_SIDSIZE && ste_s1cdmax && config[0] &&
        !SMMU_IDR0_CD2L && (ste_s1fmt == 1 || ste_s1fmt == 2)) {
        error_report("STE inconsistant, CD mismatch");
        return false;
    }
    if (SMMU_IDR0_ATS && ((_config & 0x3) == 0) &&
        ((ste_eats == 2 && (_config != 0x7 || ste_s2s)) ||
        (ste_eats == 1 && !ste_s2s))) {
        error_report("STE inconsistant, EATS/S2S mismatch");
        return false;
    }
    if (config[0] && (SMMU_IDR1_SIDSIZE &&
        (ste_s1cdmax > SMMU_IDR1_SIDSIZE))) {
        error_report("STE inconsistant, SSID out of range");
        return false;
    }

    strw_unused = (!SMMU_IDR0_S1P || !SMMU_IDR0_HYP || (_config == 4));

    addr_out_of_range = STE_S2TTB(ste) > MAX_PA(ste);

    if (is_ste_stage2(ste)) {
        if ((ste_s2aa64 && !is_s2granule_valid(ste)) ||
            (!ste_s2aa64 && !(SMMU_IDR0_TTF & 0x1)) ||
            (ste_s2aa64 && !(SMMU_IDR0_TTF & 0x2))  ||
            ((STE_S2HA(ste) || STE_S2HD(ste)) && !ste_s2aa64) ||
            ((STE_S2HA(ste) || STE_S2HD(ste)) && !SMMU_IDR0_HTTU) ||
            (STE_S2HD(ste) && (SMMU_IDR0_HTTU == 1)) || addr_out_of_range) {
            error_report("STE inconsistant");
            trace_smmuv3_is_ste_consistent(config[1], granule_supported,
                                           addr_out_of_range, ste_s2aa64,
                                           STE_S2HA(ste), STE_S2HD(ste),
                                           STE_S2TTB(ste));
        return false;
        }
    }
    if (SMMU_IDR0_S2P && (config[0] == 0 && config[1]) &&
        (strw_unused || !ste_strw) && !SMMU_IDR0_VMID16 && !(ste_vmid >> 8)) {
        error_report("STE inconsistant, VMID out of range");
        return false;
    }
    return true;
}

/**
 * smmu_find_ste - Return the stream table entry associated
 * to the sid
 *
 * @s: smmuv3 handle
 * @sid: stream ID
 * @ste: returned stream table entry
 *
 * Supports linear and 2-level stream table
 * Return 0 on success or an SMMUEvtErr enum value otherwise
 */
static int smmu_find_ste(SMMUV3State *s, uint16_t sid, Ste *ste)
{
    hwaddr addr;

    trace_smmuv3_find_ste(sid, s->features, s->sid_split);
    /* Check SID range */
    if (sid > (1 << s->sid_size)) {
        return SMMU_EVT_C_BAD_SID;
    }
    if (s->features & SMMU_FEATURE_2LVL_STE) {
        int l1_ste_offset, l2_ste_offset, max_l2_ste, span;
        hwaddr l1ptr, l2ptr;
        STEDesc l1std;

        l1_ste_offset = sid >> s->sid_split;
        l2_ste_offset = sid & ((1 << s->sid_split) - 1);
        l1ptr = (hwaddr)(s->strtab_base + l1_ste_offset * sizeof(l1std));
        smmu_read_sysmem(l1ptr, &l1std, sizeof(l1std), false);
        span = L1STD_SPAN(&l1std);

        if (!span) {
            /* l2ptr is not valid */
            error_report("invalid sid=%d (L1STD span=0)", sid);
            return SMMU_EVT_C_BAD_SID;
        }
        max_l2_ste = (1 << span) - 1;
        l2ptr = L1STD_L2PTR(&l1std);
        trace_smmuv3_find_ste_2lvl(s->strtab_base, l1ptr, l1_ste_offset,
                                   l2ptr, l2_ste_offset, max_l2_ste);
        if (l2_ste_offset > max_l2_ste) {
            error_report("l2_ste_offset=%d > max_l2_ste=%d",
                         l2_ste_offset, max_l2_ste);
            return SMMU_EVT_C_BAD_STE;
        }
        addr = L1STD_L2PTR(&l1std) + l2_ste_offset * sizeof(*ste);
    } else {
        addr = s->strtab_base + sid * sizeof(*ste);
    }

    if (smmu_get_ste(s, addr, ste)) {
        error_report("Unable to Fetch STE");
        return SMMU_EVT_F_STE_FETCH;
    }

    return 0;
}

/**
 * smmu_cfg_populate_s1 - Populate the stage 1 translation config
 * from the context descriptor
 */
static int smmu_cfg_populate_s1(SMMUTransCfg *cfg, Cd *cd)
{
    bool s1a64 = CD_AARCH64(cd);
    int epd0 = CD_EPD0(cd);
    int tg;

    cfg->stage   = 1;
    tg           = epd0 ? CD_TG1(cd) : CD_TG0(cd);
    cfg->tsz     = epd0 ? CD_T1SZ(cd) : CD_T0SZ(cd);
    cfg->ttbr    = epd0 ? CD_TTB1(cd) : CD_TTB0(cd);
    cfg->oas     = oas2bits(CD_IPS(cd));

    if (s1a64) {
        cfg->tsz = MIN(cfg->tsz, 39);
        cfg->tsz = MAX(cfg->tsz, 16);
    }
    cfg->granule_sz = tg2granule(tg, epd0);

    cfg->oas = MIN(oas2bits(SMMU_IDR5_OAS), cfg->oas);
    /* fix ttbr - make top bits zero*/
    cfg->ttbr = extract64(cfg->ttbr, 0, cfg->oas);
    cfg->aa64 = s1a64;
    cfg->initial_level  = 4 - (64 - cfg->tsz - 4) / (cfg->granule_sz - 3);

    trace_smmuv3_cfg_stage(cfg->stage, cfg->oas, cfg->tsz, cfg->ttbr,
                           cfg->aa64, cfg->granule_sz, cfg->initial_level);

    return 0;
}

/**
 * smmu_cfg_populate_s2 - Populate the stage 2 translation config
 * from the Stream Table Entry
 */
static int smmu_cfg_populate_s2(SMMUTransCfg *cfg, Ste *ste)
{
    bool s2a64 = STE_S2AA64(ste);
    int default_initial_level;
    int tg;

    cfg->stage = 2;

    tg           = STE_S2TG(ste);
    cfg->tsz     = STE_S2T0SZ(ste);
    cfg->ttbr    = STE_S2TTB(ste);
    cfg->oas     = pa_range(ste);

    cfg->aa64    = s2a64;

    if (s2a64) {
        cfg->tsz = MIN(cfg->tsz, 39);
        cfg->tsz = MAX(cfg->tsz, 16);
    }
    cfg->granule_sz = tg2granule(tg, 0);

    cfg->oas = MIN(oas2bits(SMMU_IDR5_OAS), cfg->oas);
    /* fix ttbr - make top bits zero*/
    cfg->ttbr = extract64(cfg->ttbr, 0, cfg->oas);

    default_initial_level = 4 - (64 - cfg->tsz - 4) / (cfg->granule_sz - 3);
    cfg->initial_level = ~STE_S2SL0(ste);
    if (cfg->initial_level  != default_initial_level) {
        error_report("%s concatenated translation tables at initial S2 lookup"
                     " not supported", __func__);
        return SMMU_EVT_C_BAD_STE;;
    }

    trace_smmuv3_cfg_stage(cfg->stage, cfg->oas, cfg->tsz, cfg->ttbr,
                           cfg->aa64, cfg->granule_sz, cfg->initial_level);

    return 0;
}

/**
 * smmuv3_decode_config - Prepare the translation configuration
 * for the @mr iommu region
 * @mr: iommu memory region the translation config must be prepared for
 * @cfg: output translation configuration
 *
 * return 0 on success or an SMMUEvtErr enum value otherwise
 */
static int smmuv3_decode_config(IOMMUMemoryRegion *mr, SMMUTransCfg *cfg)
{
    SMMUDevice *sdev = container_of(mr, SMMUDevice, iommu);
    int sid = smmu_get_sid(sdev);
    SMMUV3State *s = sdev->smmu;
    Ste ste;
    Cd cd;
    int ret = 0;

    if (!smmu_enabled(s)) {
        cfg->disabled = true;
        return 0;
    }
    ret = smmu_find_ste(s, sid, &ste);
    if (ret) {
        return ret;
    }

    if (!STE_VALID(&ste)) {
        return SMMU_EVT_C_BAD_STE;
    }

    switch (STE_CONFIG(&ste)) {
    case STE_CONFIG_BYPASS:
        cfg->bypassed = true;
        return 0;
    case STE_CONFIG_S1:
         break;
    case STE_CONFIG_S2:
         break;
    default: /* reserved, abort, nested */
        return SMMU_EVT_F_UUT;
    }

    /* S1 or S2 */

    if (!is_ste_consistent(s, &ste)) {
        return SMMU_EVT_C_BAD_STE;
    }

    if (is_ste_stage1(&ste)) {
        ret = smmu_get_cd(s, &ste, 0, &cd); /* We dont have SSID yet */
        if (ret) {
            return SMMU_EVT_F_CD_FETCH;
        }

        if (!is_cd_valid(s, &ste, &cd)) {
            return SMMU_EVT_C_BAD_CD;
        }
        return smmu_cfg_populate_s1(cfg, &cd);
    }

    return smmu_cfg_populate_s2(cfg, &ste);
}

static IOMMUTLBEntry smmuv3_translate(IOMMUMemoryRegion *mr, hwaddr addr,
                                      IOMMUAccessFlags flag)
{
    SMMUDevice *sdev = container_of(mr, SMMUDevice, iommu);
    SMMUV3State *s = sdev->smmu;
    uint16_t sid = smmu_get_sid(sdev);
    SMMUEvtErr ret;
    SMMUTransCfg cfg = {};
    IOMMUTLBEntry entry = {
        .target_as = &address_space_memory,
        .iova = addr,
        .translated_addr = addr,
        .addr_mask = ~(hwaddr)0,
        .perm = flag,
    };

    ret = smmuv3_decode_config(mr, &cfg);
    if (ret || cfg.disabled || cfg.bypassed) {
        goto out;
    }

    entry.addr_mask = (1 << cfg.granule_sz) - 1;

    ret = smmu_translate(&cfg, &entry);

    trace_smmuv3_translate(mr->parent_obj.name, sid, addr,
                           entry.translated_addr, entry.perm, ret);
out:
    if (ret) {
        error_report("%s translation failed for iova=0x%"PRIx64,
                     mr->parent_obj.name, addr);
        smmuv3_record_event(s, entry.iova, sid, flag, ret);
    }
    return entry;
}

static int smmuv3_notify_entry(IOMMUTLBEntry *entry, void *private)
{
    trace_smmuv3_notify_entry(entry->iova, entry->translated_addr,
                              entry->addr_mask, entry->perm);
    memory_region_notify_one((IOMMUNotifier *)private, entry);
    return 0;
}

/* Unmap the whole notifier's range */
static void smmuv3_unmap_notifier_range(IOMMUNotifier *n)
{
    IOMMUTLBEntry entry;
    hwaddr size = n->end - n->start + 1;

    entry.target_as = &address_space_memory;
    entry.iova = n->start & ~(size - 1);
    entry.perm = IOMMU_NONE;
    entry.addr_mask = size - 1;

    memory_region_notify_one(n, &entry);
}

static void smmuv3_replay(IOMMUMemoryRegion *mr, IOMMUNotifier *n)
{
    SMMUTransCfg cfg = {};
    int ret;

    trace_smmuv3_replay(mr->parent_obj.name, n, n->start, n->end);
    smmuv3_unmap_notifier_range(n);

    ret = smmuv3_decode_config(mr, &cfg);
    if (ret) {
        error_report("%s error decoding the configuration for iommu mr=%s",
                     __func__, mr->parent_obj.name);
    }

    if (cfg.disabled || cfg.bypassed) {
        return;
    }
    /* walk the page tables and replay valid entries */
    smmu_page_walk(&cfg, 0, (1ULL << (64 - cfg.tsz)) - 1, false,
                   smmuv3_notify_entry, n);
}
static void smmuv3_notify_iova_range(IOMMUMemoryRegion *mr, IOMMUNotifier *n,
                                     uint64_t iova, size_t size)
{
    SMMUTransCfg cfg = {};
    IOMMUTLBEntry entry;
    int ret;

    trace_smmuv3_notify_iova_range(mr->parent_obj.name, iova, size, n);
    ret = smmuv3_decode_config(mr, &cfg);
    if (ret) {
        error_report("%s error decoding the configuration for iommu mr=%s",
                     __func__, mr->parent_obj.name);
    }

    if (cfg.disabled || cfg.bypassed) {
        return;
    }

    /* first unmap */
    entry.target_as = &address_space_memory;
    entry.iova = iova & ~(size - 1);
    entry.addr_mask = size - 1;
    entry.perm = IOMMU_NONE;

    memory_region_notify_one(n, &entry);

    /* then figure out if a new mapping needs to be applied */
    smmu_page_walk(&cfg, iova, iova + entry.addr_mask , false,
                   smmuv3_notify_entry, n);
}

static void smmuv3_notify_flag_changed(IOMMUMemoryRegion *iommu,
                                       IOMMUNotifierFlag old,
                                       IOMMUNotifierFlag new)
{
    SMMUDevice *sdev = container_of(iommu, SMMUDevice, iommu);
    SMMUV3State *s3 = sdev->smmu;
    SMMUState *s = &(s3->smmu_state);
    SMMUNotifierNode *node = NULL;
    SMMUNotifierNode *next_node = NULL;

    if (old == IOMMU_NOTIFIER_NONE) {
        trace_smmuv3_notify_flag_add(iommu->parent_obj.name);
        node = g_malloc0(sizeof(*node));
        node->sdev = sdev;
        QLIST_INSERT_HEAD(&s->notifiers_list, node, next);
        return;
    }

    /* update notifier node with new flags */
    QLIST_FOREACH_SAFE(node, &s->notifiers_list, next, next_node) {
        if (node->sdev == sdev) {
            if (new == IOMMU_NOTIFIER_NONE) {
                trace_smmuv3_notify_flag_del(iommu->parent_obj.name);
                QLIST_REMOVE(node, next);
                g_free(node);
            }
            return;
        }
    }
}
/*
 * Replay all iommu memory regions attached to the smmu
 */
static void smmuv3_replay_all(SMMUState *s)
{
    SMMUNotifierNode *node;

    QLIST_FOREACH(node, &s->notifiers_list, next) {
        trace_smmuv3_replay_mr(node->sdev->iommu.parent_obj.name);
        memory_region_iommu_replay_all(&node->sdev->iommu);
    }
}

/*
 * Replay the iommu memory region corresponding to a given streamid
 */
static void smmuv3_replay_sid(SMMUState *s, uint16_t sid)
{
    uint8_t bus_n, devfn;
    SMMUPciBus *smmu_bus;
    SMMUDevice *smmu;

    bus_n = PCI_BUS_NUM(sid);
    smmu_bus = smmu_find_as_from_bus_num(s, bus_n);
    if (smmu_bus) {
        devfn = sid & 0xFF;
        smmu = smmu_bus->pbdev[devfn];
        if (smmu) {
            trace_smmuv3_replay_mr(smmu->iommu.parent_obj.name);
            memory_region_iommu_replay_all(&smmu->iommu);
        }
    }
}

static void smmuv3_replay_iova_range(SMMUState *s, uint64_t iova, size_t size)
{
    SMMUNotifierNode *node;

    QLIST_FOREACH(node, &s->notifiers_list, next) {
        IOMMUMemoryRegion *mr = &node->sdev->iommu;
        IOMMUNotifier *n;

        IOMMU_NOTIFIER_FOREACH(n, mr) {
            smmuv3_notify_iova_range(mr, n, iova, size);
        }
    }
}

static int smmuv3_cmdq_consume(SMMUV3State *s)
{
    SMMUCmdError cmd_error = SMMU_CERROR_NONE;

    trace_smmuv3_cmdq_consume(SMMU_CMDQ_ERR(s), smmu_cmd_q_enabled(s),
                              s->cmdq.prod, s->cmdq.cons,
                              s->cmdq.wrap.prod, s->cmdq.wrap.cons);

    if (!smmu_cmd_q_enabled(s)) {
        return 0;
    }

    while (!SMMU_CMDQ_ERR(s) && !smmu_is_q_empty(s, &s->cmdq)) {
        uint32_t type;
        Cmd cmd;

        if (smmuv3_read_cmdq(s, &cmd) != MEMTX_OK) {
            cmd_error = SMMU_CERROR_ABT;
            break;
        }

        type = CMD_TYPE(&cmd);

        trace_smmuv3_cmdq_opcode(cmd_stringify[type]);

        switch (CMD_TYPE(&cmd)) {
        case SMMU_CMD_SYNC:
            if (CMD_CS(&cmd) & CMD_SYNC_SIG_IRQ) {
                smmuv3_irq_trigger(s, SMMU_IRQ_CMD_SYNC, 0);
            }
            break;
        case SMMU_CMD_PREFETCH_CONFIG:
        case SMMU_CMD_PREFETCH_ADDR:
            break;
        case SMMU_CMD_CFGI_STE:
        {
             uint32_t streamid = cmd.word[1];

             trace_smmuv3_cmdq_cfgi_ste(streamid);
             smmuv3_replay_sid(&s->smmu_state, streamid);
            break;
        }
        case SMMU_CMD_CFGI_STE_RANGE: /* same as SMMU_CMD_CFGI_ALL */
        {
            uint32_t start = cmd.word[1], range, end, i;

            range = extract32(cmd.word[2], 0, 5);
            end = start + (1 << (range + 1)) - 1;
            trace_smmuv3_cmdq_cfgi_ste_range(start, end);
            for (i = start; i <= end; i++) {
                smmuv3_replay_sid(&s->smmu_state, i);
            }
            break;
        }
        case SMMU_CMD_CFGI_CD:
        case SMMU_CMD_CFGI_CD_ALL:
        {
            uint32_t streamid = cmd.word[1];

            smmuv3_replay_sid(&s->smmu_state, streamid);
            break;
        }
        case SMMU_CMD_TLBI_NH_ALL:
        case SMMU_CMD_TLBI_NH_ASID:
            smmuv3_replay_all(&s->smmu_state);
            break;
        case SMMU_CMD_TLBI_NH_VA:
        {
            int asid = extract32(cmd.word[1], 16, 16);
            int vmid = extract32(cmd.word[1], 0, 16);
            uint64_t low = extract32(cmd.word[2], 12, 20);
            uint64_t high = cmd.word[3];
            uint64_t addr = high << 32 | (low << 12);
            size_t size = qemu_target_page_size();

            trace_smmuv3_cmdq_tlbi_nh_va(asid, vmid, addr);
            smmuv3_replay_iova_range(&s->smmu_state, addr, size);
            break;
        }
        case SMMU_CMD_TLBI_NH_VAA:
        case SMMU_CMD_TLBI_EL3_ALL:
        case SMMU_CMD_TLBI_EL3_VA:
        case SMMU_CMD_TLBI_EL2_ALL:
        case SMMU_CMD_TLBI_EL2_ASID:
        case SMMU_CMD_TLBI_EL2_VA:
        case SMMU_CMD_TLBI_EL2_VAA:
        case SMMU_CMD_TLBI_S12_VMALL:
        case SMMU_CMD_TLBI_S2_IPA:
        case SMMU_CMD_TLBI_NSNH_ALL:
            smmuv3_replay_all(&s->smmu_state);
            break;
        case SMMU_CMD_ATC_INV:
        case SMMU_CMD_PRI_RESP:
        case SMMU_CMD_RESUME:
        case SMMU_CMD_STALL_TERM:
            trace_smmuv3_unhandled_cmd(type);
            break;
        default:
            cmd_error = SMMU_CERROR_ILL;
            error_report("Illegal command type: %d", CMD_TYPE(&cmd));
            break;
        }
    }

    if (cmd_error) {
        error_report("GERROR_CMDQ: CONS.ERR=%d", cmd_error);
        smmu_write_cmdq_err(s, cmd_error);
        smmuv3_irq_trigger(s, SMMU_IRQ_GERROR, SMMU_GERROR_CMDQ);
    }

    trace_smmuv3_cmdq_consume_out(s->cmdq.wrap.prod, s->cmdq.prod,
                                  s->cmdq.wrap.cons, s->cmdq.cons);

    return 0;
}

static void smmu_update_qreg(SMMUV3State *s, SMMUQueue *q, hwaddr reg,
                             uint32_t off, uint64_t val, unsigned size)
{
   if (size == 8 && off == 0) {
        smmu_write64_reg(s, reg, val);
    } else {
        smmu_write32_reg(s, reg, val);
    }

    switch (off) {
    case 0:                             /* BASE register */
        val = smmu_read64_reg(s, reg);
        q->shift = val & 0x1f;
        q->entries = 1 << (q->shift);
        smmu_update_base_reg(s, &q->base, val);
        break;

    case 8:                             /* PROD */
        q->prod = Q_IDX(q, val);
        q->wrap.prod = val >> q->shift;
        break;

    case 12:                             /* CONS */
        q->cons = Q_IDX(q, val);
        q->wrap.cons = val >> q->shift;
        trace_smmuv3_update_qreg(q->cons, val);
        break;

    }

    if (reg == SMMU_REG_CMDQ_PROD) {
        smmuv3_cmdq_consume(s);
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

    smmu_write_mmio_fixup(s, &addr);

    trace_smmuv3_write_mmio(addr, val, size);

    switch (addr) {
    case 0xFDC ... 0xFFC:
    case SMMU_REG_IDR0 ... SMMU_REG_IDR5:
        trace_smmuv3_write_mmio_idr(addr, val);
        return;
    case SMMU_REG_GERRORN:
        smmuv3_write_gerrorn(s, val);
        /*
         * By acknowledging the CMDQ_ERR, SW may notify cmds can
         * be processed again
         */
        smmuv3_cmdq_consume(s);
        return;
    case SMMU_REG_CR0:
        smmu_write32_reg(s, SMMU_REG_CR0, val);
        /* immediatly reflect the changes in CR0_ACK */
        smmu_write32_reg(s, SMMU_REG_CR0_ACK, val);
        /* in case the command queue has been enabled */
        smmuv3_cmdq_consume(s);
        return;
    case SMMU_REG_IRQ_CTRL:
        smmu_write32_reg(s, SMMU_REG_IRQ_CTRL_ACK, val);
        return;
    case SMMU_REG_STRTAB_BASE:
        smmu_update_base_reg(s, &s->strtab_base, val);
        return;
    case SMMU_REG_STRTAB_BASE_CFG:
        if (((val >> 16) & 0x3) == 0x1) {
            s->sid_split = (val >> 6) & 0x1f;
            s->features |= SMMU_FEATURE_2LVL_STE;
        }
        return;
    case SMMU_REG_CMDQ_BASE ... SMMU_REG_CMDQ_CONS:
        smmu_update_qreg(s, &s->cmdq, addr, addr - SMMU_REG_CMDQ_BASE,
                         val, size);
        return;

    case SMMU_REG_EVTQ_BASE ... SMMU_REG_EVTQ_CONS:
        smmu_update_qreg(s, &s->evtq, addr, addr - SMMU_REG_EVTQ_BASE,
                         val, size);
        return;

    case SMMU_REG_PRIQ_BASE ... SMMU_REG_PRIQ_CONS:
        error_report("%s PRI queue is not supported", __func__);
        abort();
    }

    if (size == 8) {
        smmu_write64_reg(s, addr, val);
    } else {
        smmu_write32_reg(s, addr, (uint32_t)val);
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
        error_report("addr:0x%"PRIx64" val:0x%"PRIx64, addr, val);
        break;
    case SMMU_REG_STRTAB_BASE ... SMMU_REG_CMDQ_BASE:
    case SMMU_REG_EVTQ_BASE:
    case SMMU_REG_PRIQ_BASE ... SMMU_REG_PRIQ_IRQ_CFG1:
        val = smmu_read64_reg(s, addr);
        break;
    default:
        val = (uint64_t)smmu_read32_reg(s, addr);
        break;
    }

    trace_smmuv3_read_mmio(addr, val, size);
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
    .impl = {
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

static void smmu_reset(DeviceState *dev)
{
    SMMUV3State *s = SMMU_V3_DEV(dev);
    smmuv3_init(s);
}

static void smmu_realize(DeviceState *d, Error **errp)
{
    SMMUState *sys = SMMU_SYS_DEV(d);
    SMMUV3State *s = SMMU_V3_DEV(sys);
    SysBusDevice *dev = SYS_BUS_DEVICE(d);

    memory_region_init_io(&sys->iomem, OBJECT(s),
                          &smmu_mem_ops, sys, TYPE_SMMU_V3_DEV, 0x20000);

    sys->mrtypename = g_strdup(TYPE_SMMUV3_IOMMU_MEMORY_REGION);

    sysbus_init_mmio(dev, &sys->iomem);

    smmu_init_irq(s, dev);
}

static const VMStateDescription vmstate_smmuv3 = {
    .name = "smmuv3",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, SMMUV3State, SMMU_NREGS),
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
    /* Supported by TYPE_VIRT_MACHINE */
    dc->user_creatable = true;
}

static void smmuv3_iommu_memory_region_class_init(ObjectClass *klass,
                                                  void *data)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_CLASS(klass);

    imrc->translate = smmuv3_translate;
    imrc->notify_flag_changed = smmuv3_notify_flag_changed;
    imrc->replay = smmuv3_replay;
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

static const TypeInfo smmuv3_iommu_memory_region_info = {
    .parent = TYPE_IOMMU_MEMORY_REGION,
    .name = TYPE_SMMUV3_IOMMU_MEMORY_REGION,
    .class_init = smmuv3_iommu_memory_region_class_init,
};

static void smmuv3_register_types(void)
{
    type_register(&smmuv3_type_info);
    type_register(&smmuv3_iommu_memory_region_info);
}

type_init(smmuv3_register_types)

