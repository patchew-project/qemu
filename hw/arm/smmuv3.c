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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/boards.h"
#include "sysemu/sysemu.h"
#include "hw/sysbus.h"
#include "hw/qdev-core.h"
#include "hw/pci/pci.h"
#include "exec/address-spaces.h"
#include "trace.h"
#include "qemu/error-report.h"
#include "qapi/error.h"

#include "hw/arm/smmuv3.h"
#include "smmuv3-internal.h"

/**
 * smmuv3_trigger_irq - pulse @irq if enabled and update
 * GERROR register in case of GERROR interrupt
 *
 * @irq: irq type
 * @gerror_mask: mask of gerrors to toggle (relevant if @irq is GERROR)
 */
static void smmuv3_trigger_irq(SMMUv3State *s, SMMUIrq irq,
                               uint32_t gerror_mask)
{

    bool pulse = false;

    switch (irq) {
    case SMMU_IRQ_EVTQ:
        pulse = smmuv3_eventq_irq_enabled(s);
        break;
    case SMMU_IRQ_PRIQ:
        error_setg(&error_fatal, "PRI not supported");
        break;
    case SMMU_IRQ_CMD_SYNC:
        pulse = true;
        break;
    case SMMU_IRQ_GERROR:
    {
        uint32_t pending = s->gerror ^ s->gerrorn;
        uint32_t new_gerrors = ~pending & gerror_mask;

        if (!new_gerrors) {
            /* only toggle non pending errors */
            return;
        }
        s->gerror ^= new_gerrors;
        trace_smmuv3_write_gerror(new_gerrors, s->gerror);

        /* pulse the GERROR irq only if all previous gerrors were acked */
        pulse = smmuv3_gerror_irq_enabled(s) && !pending;
        break;
    }
    }
    if (pulse) {
            trace_smmuv3_trigger_irq(irq);
            qemu_irq_pulse(s->irq[irq]);
    }
}

static void smmuv3_write_gerrorn(SMMUv3State *s, uint32_t new_gerrorn)
{
    uint32_t pending = s->gerror ^ s->gerrorn;
    uint32_t toggled = s->gerrorn ^ new_gerrorn;
    uint32_t acked;

    if (toggled & ~pending) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "guest toggles non pending errors = 0x%x\n",
                      toggled & ~pending);
    }

    /* Make sure SW does not toggle irqs that are not active */
    acked = toggled & pending;
    s->gerrorn ^= acked;

    trace_smmuv3_write_gerrorn(acked, s->gerrorn);
}

static uint32_t queue_index_inc(uint32_t val,
                                uint32_t qidx_mask, uint32_t qwrap_mask)
{
    uint32_t i = (val + 1) & qidx_mask;

    if (i <= (val & qidx_mask)) {
        i = ((val & qwrap_mask) ^ qwrap_mask) | i;
    } else {
        i = (val & qwrap_mask) | i;
    }
    return i;
}

static inline void queue_prod_incr(SMMUQueue *q)
{
    q->prod = queue_index_inc(q->prod, INDEX_MASK(q), WRAP_MASK(q));
}

static inline void queue_cons_incr(SMMUQueue *q)
{
    q->cons = queue_index_inc(q->cons, INDEX_MASK(q), WRAP_MASK(q));
}

static inline MemTxResult queue_read(SMMUQueue *q, void *data)
{
    dma_addr_t addr = Q_CONS_ENTRY(q);

    return dma_memory_read(&address_space_memory, addr,
                           (uint8_t *)data, q->entry_size);
}

static void queue_write(SMMUQueue *q, void *data)
{
    dma_addr_t addr = Q_PROD_ENTRY(q);
    MemTxResult ret;

    ret = dma_memory_write(&address_space_memory, addr,
                           (uint8_t *)data, q->entry_size);
    if (ret != MEMTX_OK) {
        return;
    }

    queue_prod_incr(q);
}

static void smmuv3_write_eventq(SMMUv3State *s, Evt *evt)
{
    SMMUQueue *q = &s->eventq;
    bool q_empty = Q_EMPTY(q);
    bool q_full = Q_FULL(q);

    if (!SMMUV3_EVENTQ_ENABLED(s)) {
        return;
    }

    if (q_full) {
        return;
    }

    queue_write(q, evt);

    if (q_empty) {
        smmuv3_trigger_irq(s, SMMU_IRQ_EVTQ, 0);
    }
}

void smmuv3_record_event(SMMUv3State *s, SMMUEventInfo *info)
{
    Evt evt;

    if (!SMMUV3_EVENTQ_ENABLED(s)) {
        return;
    }

    EVT_SET_TYPE(&evt, info->type);
    EVT_SET_SID(&evt, info->sid);

    switch (info->type) {
    case SMMU_EVT_OK:
        return;
    case SMMU_EVT_F_UUT:
        EVT_SET_SSID(&evt, info->u.f_uut.ssid);
        EVT_SET_SSV(&evt,  info->u.f_uut.ssv);
        EVT_SET_ADDR(&evt, info->u.f_uut.addr);
        EVT_SET_RNW(&evt,  info->u.f_uut.rnw);
        EVT_SET_PNU(&evt,  info->u.f_uut.pnu);
        EVT_SET_IND(&evt,  info->u.f_uut.ind);
        break;
    case SMMU_EVT_C_BAD_STREAMID:
        EVT_SET_SSID(&evt, info->u.c_bad_streamid.ssid);
        EVT_SET_SSV(&evt,  info->u.c_bad_streamid.ssv);
        break;
    case SMMU_EVT_F_STE_FETCH:
        EVT_SET_SSID(&evt, info->u.f_ste_fetch.ssid);
        EVT_SET_SSV(&evt,  info->u.f_ste_fetch.ssv);
        EVT_SET_ADDR(&evt, info->u.f_ste_fetch.addr);
        break;
    case SMMU_EVT_C_BAD_STE:
        EVT_SET_SSID(&evt, info->u.c_bad_ste.ssid);
        EVT_SET_SSV(&evt,  info->u.c_bad_ste.ssv);
        break;
    case SMMU_EVT_F_STREAM_DISABLED:
        break;
    case SMMU_EVT_F_TRANS_FORBIDDEN:
        EVT_SET_ADDR(&evt, info->u.f_transl_forbidden.addr);
        EVT_SET_RNW(&evt, info->u.f_transl_forbidden.rnw);
        break;
    case SMMU_EVT_C_BAD_SUBSTREAMID:
        EVT_SET_SSID(&evt, info->u.c_bad_substream.ssid);
        break;
    case SMMU_EVT_F_CD_FETCH:
        EVT_SET_SSID(&evt, info->u.f_cd_fetch.ssid);
        EVT_SET_SSV(&evt,  info->u.f_cd_fetch.ssv);
        EVT_SET_ADDR(&evt, info->u.f_cd_fetch.addr);
        break;
    case SMMU_EVT_C_BAD_CD:
        EVT_SET_SSID(&evt, info->u.c_bad_cd.ssid);
        EVT_SET_SSV(&evt,  info->u.c_bad_cd.ssv);
        break;
    case SMMU_EVT_F_WALK_EABT:
    case SMMU_EVT_F_TRANSLATION:
    case SMMU_EVT_F_ADDR_SIZE:
    case SMMU_EVT_F_ACCESS:
    case SMMU_EVT_F_PERMISSION:
        EVT_SET_STALL(&evt, info->u.f_walk_eabt.stall);
        EVT_SET_STAG(&evt, info->u.f_walk_eabt.stag);
        EVT_SET_SSID(&evt, info->u.f_walk_eabt.ssid);
        EVT_SET_SSV(&evt, info->u.f_walk_eabt.ssv);
        EVT_SET_S2(&evt, info->u.f_walk_eabt.s2);
        EVT_SET_ADDR(&evt, info->u.f_walk_eabt.addr);
        EVT_SET_RNW(&evt, info->u.f_walk_eabt.rnw);
        EVT_SET_PNU(&evt, info->u.f_walk_eabt.pnu);
        EVT_SET_IND(&evt, info->u.f_walk_eabt.ind);
        EVT_SET_CLASS(&evt, info->u.f_walk_eabt.class);
        EVT_SET_ADDR2(&evt, info->u.f_walk_eabt.addr2);
        break;
    case SMMU_EVT_F_CFG_CONFLICT:
        EVT_SET_SSID(&evt, info->u.f_cfg_conflict.ssid);
        EVT_SET_SSV(&evt,  info->u.f_cfg_conflict.ssv);
        break;
    /* rest is not implemented */
    case SMMU_EVT_F_BAD_ATS_TREQ:
    case SMMU_EVT_F_TLB_CONFLICT:
    case SMMU_EVT_E_PAGE_REQ:
    default:
        error_report("%s event %d not supported", __func__,
                     info->type);
        return;
    }

    trace_smmuv3_record_event(SMMU_EVENT_STRING(info->type), info->sid);
    smmuv3_write_eventq(s, &evt);
    info->recorded = true;
}

static void smmuv3_init_regs(SMMUv3State *s)
{
    /**
     * IDR0: stage1 only, AArch64 only, coherent access, 16b ASID,
     *       multi-level stream table
     */
    s->idr[0] = FIELD_DP32(s->idr[0], IDR0, S1P, 1); /* stage 1 supported */
    s->idr[0] = FIELD_DP32(s->idr[0], IDR0, TTF, 2); /* AArch64 PTW only */
    s->idr[0] = FIELD_DP32(s->idr[0], IDR0, COHACC, 1); /* IO coherent */
    s->idr[0] = FIELD_DP32(s->idr[0], IDR0, ASID16, 1); /* 16-bit ASID */
    s->idr[0] = FIELD_DP32(s->idr[0], IDR0, TTENDIAN, 2); /* little endian */
    s->idr[0] = FIELD_DP32(s->idr[0], IDR0, STALL_MODEL, 1); /* No stall */
    /* terminated transaction will always be aborted/error returned */
    s->idr[0] = FIELD_DP32(s->idr[0], IDR0, TERM_MODEL, 1);
    /* 2-level stream table supported */
    s->idr[0] = FIELD_DP32(s->idr[0], IDR0, STLEVEL, 1);

    s->idr[1] = FIELD_DP32(s->idr[1], IDR1, SIDSIZE, SMMU_IDR1_SIDSIZE);
    s->idr[1] = FIELD_DP32(s->idr[1], IDR1, EVENTQS, 19);
    s->idr[1] = FIELD_DP32(s->idr[1], IDR1, CMDQS,   19);

   /* 4K and 64K granule support */
    s->idr[5] = FIELD_DP32(s->idr[5], IDR5, GRAN4K, 1);
    s->idr[5] = FIELD_DP32(s->idr[5], IDR5, GRAN64K, 1);
    s->idr[5] = FIELD_DP32(s->idr[5], IDR5, OAS, SMMU_IDR5_OAS); /* 44 bits */

    s->cmdq.base = deposit64(s->cmdq.base, 0, 5, 19); /* LOG2SIZE = 19 */
    s->cmdq.prod = 0;
    s->cmdq.cons = 0;
    s->cmdq.entry_size = sizeof(struct Cmd);
    s->eventq.base = deposit64(s->eventq.base, 0, 5, 19); /* LOG2SIZE = 19 */
    s->eventq.prod = 0;
    s->eventq.cons = 0;
    s->eventq.entry_size = sizeof(struct Evt);

    s->features = 0;
    s->sid_split = 0;
}

static int smmu_get_ste(SMMUv3State *s, dma_addr_t addr, STE *buf,
                        SMMUEventInfo *event)
{
    int ret;

    trace_smmuv3_get_ste(addr);
    /* TODO: guarantee 64-bit single-copy atomicity */
    ret = dma_memory_read(&address_space_memory, addr,
                          (void *)buf, sizeof(*buf));
    if (ret != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Cannot fetch pte at address=0x%"PRIx64"\n", addr);
        event->type = SMMU_EVT_F_STE_FETCH;
        event->u.f_ste_fetch.addr = addr;
        return -EINVAL;
    }
    return 0;

}

/* @ssid > 0 not supported yet */
static int smmu_get_cd(SMMUv3State *s, STE *ste, uint32_t ssid,
                       CD *buf, SMMUEventInfo *event)
{
    dma_addr_t addr = STE_CTXPTR(ste);
    int ret;

    trace_smmuv3_get_cd(addr);
    /* TODO: guarantee 64-bit single-copy atomicity */
    ret = dma_memory_read(&address_space_memory, addr,
                           (void *)buf, sizeof(*buf));
    if (ret != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Cannot fetch pte at address=0x%"PRIx64"\n", addr);
        event->type = SMMU_EVT_F_CD_FETCH;
        event->u.f_ste_fetch.addr = addr;
        return -EINVAL;
    }
    return 0;
}

static int decode_ste(SMMUv3State *s, SMMUTransCfg *cfg,
                      STE *ste, SMMUEventInfo *event)
{
    uint32_t config = STE_CONFIG(ste);
    int ret = -EINVAL;

    if (STE_CFG_ABORT(config)) {
        /* abort but don't record any event */
        cfg->aborted = true;
        return ret;
    }

    if (STE_CFG_BYPASS(config)) {
        cfg->bypassed = true;
        return ret;
    }

    if (!STE_VALID(ste)) {
        goto bad_ste;
    }

    if (STE_CFG_S2_ENABLED(config)) {
        error_setg(&error_fatal, "SMMUv3 does not support stage 2 yet");
    }

    if (STE_S1CDMAX(ste) != 0) {
        error_setg(&error_fatal,
                   "SMMUv3 does not support multiple context descriptors yet");
        goto bad_ste;
    }
    return 0;

bad_ste:
    event->type = SMMU_EVT_C_BAD_STE;
    return -EINVAL;
}

/**
 * smmu_find_ste - Return the stream table entry associated
 * to the sid
 *
 * @s: smmuv3 handle
 * @sid: stream ID
 * @ste: returned stream table entry
 * @event: handle to an event info
 *
 * Supports linear and 2-level stream table
 * Return 0 on success, -EINVAL otherwise
 */
static int smmu_find_ste(SMMUv3State *s, uint32_t sid, STE *ste,
                         SMMUEventInfo *event)
{
    dma_addr_t addr;
    int ret;

    trace_smmuv3_find_ste(sid, s->features, s->sid_split);
    /* Check SID range */
    if (sid > (1 << SMMU_IDR1_SIDSIZE)) {
        event->type = SMMU_EVT_C_BAD_STREAMID;
        return -EINVAL;
    }
    if (s->features & SMMU_FEATURE_2LVL_STE) {
        int l1_ste_offset, l2_ste_offset, max_l2_ste, span;
        dma_addr_t strtab_base, l1ptr, l2ptr;
        STEDesc l1std;

        strtab_base = s->strtab_base & SMMU_BASE_ADDR_MASK;
        l1_ste_offset = sid >> s->sid_split;
        l2_ste_offset = sid & ((1 << s->sid_split) - 1);
        l1ptr = (dma_addr_t)(strtab_base + l1_ste_offset * sizeof(l1std));
        /* TODO: guarantee 64-bit single-copy atomicity */
        ret = dma_memory_read(&address_space_memory, l1ptr,
                              (uint8_t *)&l1std, sizeof(l1std));
        if (ret != MEMTX_OK) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "Could not read L1PTR at 0X%"PRIx64"\n", l1ptr);
            event->type = SMMU_EVT_F_STE_FETCH;
            event->u.f_ste_fetch.addr = l1ptr;
            return -EINVAL;
        }

        span = L1STD_SPAN(&l1std);

        if (!span) {
            /* l2ptr is not valid */
            qemu_log_mask(LOG_GUEST_ERROR,
                          "invalid sid=%d (L1STD span=0)\n", sid);
            event->type = SMMU_EVT_C_BAD_STREAMID;
            return -EINVAL;
        }
        max_l2_ste = (1 << span) - 1;
        l2ptr = L1STD_L2PTR(&l1std);
        trace_smmuv3_find_ste_2lvl(s->strtab_base, l1ptr, l1_ste_offset,
                                   l2ptr, l2_ste_offset, max_l2_ste);
        if (l2_ste_offset > max_l2_ste) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "l2_ste_offset=%d > max_l2_ste=%d\n",
                          l2_ste_offset, max_l2_ste);
            event->type = SMMU_EVT_C_BAD_STE;
            return -EINVAL;
        }
        addr = L1STD_L2PTR(&l1std) + l2_ste_offset * sizeof(*ste);
    } else {
        addr = s->strtab_base + sid * sizeof(*ste);
    }

    if (smmu_get_ste(s, addr, ste, event)) {
        return -EINVAL;
    }

    return 0;
}

static int decode_cd(SMMUTransCfg *cfg, CD *cd, SMMUEventInfo *event)
{
    int ret = -EINVAL;
    int i;

    if (!CD_VALID(cd) || !CD_AARCH64(cd)) {
        goto error;
    }

    /* we support only those at the moment */
    cfg->aa64 = true;
    cfg->stage = 1;

    cfg->oas = oas2bits(CD_IPS(cd));
    cfg->oas = MIN(oas2bits(SMMU_IDR5_OAS), cfg->oas);
    cfg->tbi = CD_TBI(cd);
    cfg->asid = CD_ASID(cd);

    trace_smmuv3_decode_cd(cfg->oas);

    /* decode data dependent on TT */
    for (i = 0; i <= 1; i++) {
        int tg, tsz;
        SMMUTransTableInfo *tt = &cfg->tt[i];

        cfg->tt[i].disabled = CD_EPD(cd, i);
        if (cfg->tt[i].disabled) {
            continue;
        }

        tsz = CD_TSZ(cd, i);
        if (tsz < 16 || tsz > 39) {
            goto error;
        }

        tg = CD_TG(cd, i);
        tt->granule_sz = tg2granule(tg, i);
        if ((tt->granule_sz != 12 && tt->granule_sz != 16) || CD_ENDI(cd)) {
            goto error;
        }

        tt->tsz = tsz;
        tt->initial_level = 4 - (64 - tsz - 4) / (tt->granule_sz - 3);
        tt->ttb = CD_TTB(cd, i);
        tt->ttb = extract64(tt->ttb, 0, cfg->oas);
        trace_smmuv3_decode_cd_tt(i, tt->tsz, tt->ttb,
                                  tt->granule_sz, tt->initial_level);
    }

    event->record_trans_faults = CD_R(cd);

    return 0;

error:
    event->type = SMMU_EVT_C_BAD_CD;
    return ret;
}

/**
 * smmuv3_decode_config - Prepare the translation configuration
 * for the @mr iommu region
 * @mr: iommu memory region the translation config must be prepared for
 * @cfg: output translation configuration which is populated through
 *       the different configuration decodng steps
 * @event: must be zero'ed by the caller
 *
 * return < 0 if the translation needs to be aborted (@event is filled
 * accordingly). Return 0 otherwise.
 */
static int smmuv3_decode_config(IOMMUMemoryRegion *mr, SMMUTransCfg *cfg,
                                SMMUEventInfo *event)
{
    SMMUDevice *sdev = container_of(mr, SMMUDevice, iommu);
    uint32_t sid = smmu_get_sid(sdev);
    SMMUv3State *s = sdev->smmu;
    int ret = -EINVAL;
    STE ste;
    CD cd;

    if (smmu_find_ste(s, sid, &ste, event)) {
        return ret;
    }

    if (decode_ste(s, cfg, &ste, event)) {
        return ret;
    }

    if (smmu_get_cd(s, &ste, 0 /* ssid */, &cd, event)) {
        return ret;
    }

    return decode_cd(cfg, &cd, event);
}

static IOMMUTLBEntry smmuv3_translate(IOMMUMemoryRegion *mr, hwaddr addr,
                                      IOMMUAccessFlags flag)
{
    SMMUDevice *sdev = container_of(mr, SMMUDevice, iommu);
    SMMUv3State *s = sdev->smmu;
    uint32_t sid = smmu_get_sid(sdev);
    SMMUEventInfo event = {.type = SMMU_EVT_OK, .sid = sid};
    SMMUPTWEventInfo ptw_info = {};
    SMMUTransCfg cfg = {};
    IOMMUTLBEntry entry = {
        .target_as = &address_space_memory,
        .iova = addr,
        .translated_addr = addr,
        .addr_mask = ~(hwaddr)0,
        .perm = IOMMU_NONE,
    };
    int ret = 0;

    if (!smmu_enabled(s)) {
        goto out;
    }

    ret = smmuv3_decode_config(mr, &cfg, &event);
    if (ret) {
        goto out;
    }

    if (cfg.aborted) {
        goto out;
    }

    ret = smmu_ptw(&cfg, addr, flag, &entry, &ptw_info);
    if (ret) {
        switch (ptw_info.type) {
        case SMMU_PTW_ERR_WALK_EABT:
            event.type = SMMU_EVT_F_WALK_EABT;
            event.u.f_walk_eabt.addr = addr;
            event.u.f_walk_eabt.rnw = flag & 0x1;
            event.u.f_walk_eabt.class = 0x1;
            event.u.f_walk_eabt.addr2 = ptw_info.addr;
            break;
        case SMMU_PTW_ERR_TRANSLATION:
            if (event.record_trans_faults) {
                event.type = SMMU_EVT_F_TRANSLATION;
                event.u.f_translation.addr = addr;
                event.u.f_translation.rnw = flag & 0x1;
            }
            break;
        case SMMU_PTW_ERR_ADDR_SIZE:
            if (event.record_trans_faults) {
                event.type = SMMU_EVT_F_ADDR_SIZE;
                event.u.f_addr_size.addr = addr;
                event.u.f_addr_size.rnw = flag & 0x1;
            }
            break;
        case SMMU_PTW_ERR_ACCESS:
            if (event.record_trans_faults) {
                event.type = SMMU_EVT_F_ACCESS;
                event.u.f_access.addr = addr;
                event.u.f_access.rnw = flag & 0x1;
            }
            break;
        case SMMU_PTW_ERR_PERMISSION:
            if (event.record_trans_faults) {
                event.type = SMMU_EVT_F_PERMISSION;
                event.u.f_permission.addr = addr;
                event.u.f_permission.rnw = flag & 0x1;
            }
            break;
        default:
            error_setg(&error_fatal, "SMMUV3 BUG");
        }
    }

    trace_smmuv3_translate(mr->parent_obj.name, sid, addr,
                           entry.translated_addr, entry.perm);
out:
    if (ret) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s translation failed for iova=0x%"PRIx64" (%s)\n",
                      mr->parent_obj.name, addr, SMMU_EVENT_STRING(event.type));
        entry.perm = IOMMU_NONE;
        smmuv3_record_event(s, &event);
    } else if (!cfg.aborted) {
        entry.perm = flag;
    }

    return entry;
}

static int smmuv3_cmdq_consume(SMMUv3State *s)
{
    SMMUCmdError cmd_error = SMMU_CERROR_NONE;
    SMMUQueue *q = &s->cmdq;
    uint32_t type = 0;

    if (!SMMUV3_CMDQ_ENABLED(s)) {
        return 0;
    }
    /*
     * some commands depend on register values, as above. In case those
     * register values change while handling the command, spec says it
     * is UNPREDICTABLE whether the command is interpreted under the new
     * or old value.
     */

    while (!Q_EMPTY(q)) {
        uint32_t pending = s->gerror ^ s->gerrorn;
        Cmd cmd;

        trace_smmuv3_cmdq_consume(Q_PROD(q), Q_CONS(q),
                                  Q_PROD_WRAP(q), Q_CONS_WRAP(q));

        if (FIELD_EX32(pending, GERROR, CMDQ_ERR)) {
            break;
        }

        if (queue_read(q, &cmd) != MEMTX_OK) {
            cmd_error = SMMU_CERROR_ABT;
            break;
        }

        type = CMD_TYPE(&cmd);

        trace_smmuv3_cmdq_opcode(SMMU_CMD_STRING(type));

        switch (type) {
        case SMMU_CMD_SYNC:
            if (CMD_SYNC_CS(&cmd) & CMD_SYNC_SIG_IRQ) {
                smmuv3_trigger_irq(s, SMMU_IRQ_CMD_SYNC, 0);
            }
            break;
        case SMMU_CMD_PREFETCH_CONFIG:
        case SMMU_CMD_PREFETCH_ADDR:
        case SMMU_CMD_CFGI_STE:
        case SMMU_CMD_CFGI_STE_RANGE: /* same as SMMU_CMD_CFGI_ALL */
        case SMMU_CMD_CFGI_CD:
        case SMMU_CMD_CFGI_CD_ALL:
        case SMMU_CMD_TLBI_NH_ALL:
        case SMMU_CMD_TLBI_NH_ASID:
        case SMMU_CMD_TLBI_NH_VA:
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
        if (cmd_error) {
            break;
        }
        /*
         * We only increment the cons index after the completion of
         * the command. We do that because the SYNC returns immediatly
         * and do not check the completion of previous commands
         */
        queue_cons_incr(q);
    }

    if (cmd_error) {
        error_report("Error on %s command execution: %d",
                     SMMU_CMD_STRING(type), cmd_error);
        smmu_write_cmdq_err(s, cmd_error);
        smmuv3_trigger_irq(s, SMMU_IRQ_GERROR, R_GERROR_CMDQ_ERR_MASK);
    }

    trace_smmuv3_cmdq_consume_out(Q_PROD(q), Q_CONS(q),
                                  Q_PROD_WRAP(q), Q_CONS_WRAP(q));

    return 0;
}

static void smmu_write_mmio(void *opaque, hwaddr addr,
                            uint64_t val, unsigned size)
{
    SMMUState *sys = opaque;
    SMMUv3State *s = ARM_SMMUV3(sys);

    /* CONSTRAINED UNPREDICTABLE choice to have page0/1 be exact aliases */
    addr &= ~0x10000;

    if (size != 4 && size != 8) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "SMMUv3 MMIO write: bad size %u\n", size);
    }

    trace_smmuv3_write_mmio(addr, val, size);

    switch (addr) {
    case A_CR0:
        s->cr[0] = val;
        s->cr0ack = val;
        /* in case the command queue has been enabled */
        smmuv3_cmdq_consume(s);
        return;
    case A_CR1:
        s->cr[1] = val;
        return;
    case A_CR2:
        s->cr[2] = val;
        return;
    case A_IRQ_CTRL:
        s->irq_ctrl = val;
        return;
    case A_GERRORN:
        smmuv3_write_gerrorn(s, val);
        /*
         * By acknowledging the CMDQ_ERR, SW may notify cmds can
         * be processed again
         */
        smmuv3_cmdq_consume(s);
        return;
    case A_GERROR_IRQ_CFG0: /* 64b */
        smmu_write64(&s->gerror_irq_cfg0, 0, size, val);
        return;
    case A_GERROR_IRQ_CFG0 + 4:
        smmu_write64(&s->gerror_irq_cfg0, 4, size, val);
        return;
    case A_GERROR_IRQ_CFG1:
        s->gerror_irq_cfg1 = val;
        return;
    case A_GERROR_IRQ_CFG2:
        s->gerror_irq_cfg2 = val;
        return;
    case A_STRTAB_BASE: /* 64b */
        smmu_write64(&s->strtab_base, 0, size, val);
        return;
    case A_STRTAB_BASE + 4:
        smmu_write64(&s->strtab_base, 4, size, val);
        return;
    case A_STRTAB_BASE_CFG:
        s->strtab_base_cfg = val;
        if (FIELD_EX32(val, STRTAB_BASE_CFG, FMT) == 1) {
            s->sid_split = FIELD_EX32(val, STRTAB_BASE_CFG, SPLIT);
            s->features |= SMMU_FEATURE_2LVL_STE;
        }
        return;
    case A_CMDQ_BASE: /* 64b */
        smmu_write64(&s->cmdq.base, 0, size, val);
        return;
    case A_CMDQ_BASE + 4: /* 64b */
        smmu_write64(&s->cmdq.base, 4, size, val);
        return;
    case A_CMDQ_PROD:
        s->cmdq.prod = val;
        smmuv3_cmdq_consume(s);
        return;
    case A_CMDQ_CONS:
        s->cmdq.cons = val;
        return;
    case A_EVENTQ_BASE: /* 64b */
        smmu_write64(&s->eventq.base, 0, size, val);
        return;
    case A_EVENTQ_BASE + 4:
        smmu_write64(&s->eventq.base, 4, size, val);
        return;
    case A_EVENTQ_PROD:
        s->eventq.prod = val;
        return;
    case A_EVENTQ_CONS:
        s->eventq.cons = val;
        return;
    case A_EVENTQ_IRQ_CFG0: /* 64b */
        s->eventq.prod = val;
        smmu_write64(&s->eventq_irq_cfg0, 0, size, val);
        return;
    case A_EVENTQ_IRQ_CFG0 + 4:
        smmu_write64(&s->eventq_irq_cfg0, 4, size, val);
        return;
    case A_EVENTQ_IRQ_CFG1:
        s->eventq_irq_cfg1 = val;
        return;
    case A_EVENTQ_IRQ_CFG2:
        s->eventq_irq_cfg2 = val;
        return;
    default:
        error_report("%s unhandled access at 0x%"PRIx64, __func__, addr);
    }
}

static uint64_t smmu_read_mmio(void *opaque, hwaddr addr, unsigned size)
{
    SMMUState *sys = opaque;
    SMMUv3State *s = ARM_SMMUV3(sys);
    uint64_t val;

    /* CONSTRAINED UNPREDICTABLE choice to have page0/1 be exact aliases */
    addr &= ~0x10000;

    if (size != 4 && size != 8) {
        qemu_log_mask(LOG_GUEST_ERROR, "SMMUv3 MMIO read: bad size %u\n", size);
        return 0;
    }

    /* Primecell/Corelink ID registers */
    switch (addr) {
    case A_CIDR0:
        val = 0x0D;
        break;
    case A_CIDR1:
        val = 0xF0;
        break;
    case A_CIDR2:
        val = 0x05;
        break;
    case A_CIDR3:
        val = 0xB1;
        break;
    case A_PIDR0:
        val = 0x84; /* Part Number */
        break;
    case A_PIDR1:
        val = 0xB4; /* JEP106 ID code[3:0] for Arm and Part numver[11:8] */
        break;
    case A_PIDR3:
        val = 0x10; /* MMU600 p1 */
        break;
    case A_PIDR4:
        val = 0x4; /* 4KB region count, JEP106 continuation code for Arm */
        break;
    case 0xFD4 ... 0xFDC: /* SMMU_PDIR 5-7 */
        val = 0;
        break;
    case A_IDR0 ... A_IDR5:
        val = s->idr[(addr - A_IDR0) / 4];
        break;
    case A_IIDR:
        val = s->iidr;
        break;
    case A_CR0:
        val = s->cr[0];
        break;
    case A_CR0ACK:
        val = s->cr0ack;
        break;
    case A_CR1:
        val = s->cr[1];
        break;
    case A_CR2:
        val = s->cr[2];
        break;
    case A_STATUSR:
        val = s->statusr;
        break;
    case A_IRQ_CTRL:
        val = s->irq_ctrl;
        break;
    case A_IRQ_CTRL_ACK:
        val = s->irq_ctrl_ack;
        break;
    case A_GERROR:
        val = s->gerror;
        break;
    case A_GERRORN:
        val = s->gerrorn;
        break;
    case A_GERROR_IRQ_CFG0: /* 64b */
        val = smmu_read64(s->gerror_irq_cfg0, 0, size);
        break;
    case A_GERROR_IRQ_CFG0 + 4:
        val = smmu_read64(s->gerror_irq_cfg0, 4, size);
        break;
    case A_GERROR_IRQ_CFG1:
        val = s->gerror_irq_cfg1;
        break;
    case A_GERROR_IRQ_CFG2:
        val = s->gerror_irq_cfg2;
        break;
    case A_STRTAB_BASE: /* 64b */
        val = smmu_read64(s->strtab_base, 0, size);
        break;
    case A_STRTAB_BASE + 4: /* 64b */
        val = smmu_read64(s->strtab_base, 4, size);
        break;
    case A_STRTAB_BASE_CFG:
        val = s->strtab_base_cfg;
        break;
    case A_CMDQ_BASE: /* 64b */
        val = smmu_read64(s->cmdq.base, 0, size);
        break;
    case A_CMDQ_BASE + 4:
        val = smmu_read64(s->cmdq.base, 4, size);
        break;
    case A_CMDQ_PROD:
        val = s->cmdq.prod;
        break;
    case A_CMDQ_CONS:
        val = s->cmdq.cons;
        break;
    case A_EVENTQ_BASE: /* 64b */
        val = smmu_read64(s->eventq.base, 0, size);
        break;
    case A_EVENTQ_BASE + 4: /* 64b */
        val = smmu_read64(s->eventq.base, 4, size);
        break;
    case A_EVENTQ_PROD:
        val = s->eventq.prod;
        break;
    case A_EVENTQ_CONS:
        val = s->eventq.cons;
        break;
    default:
        error_report("%s unhandled access at 0x%"PRIx64, __func__, addr);
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

static void smmu_init_irq(SMMUv3State *s, SysBusDevice *dev)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(s->irq); i++) {
        sysbus_init_irq(dev, &s->irq[i]);
    }
}

static void smmu_reset(DeviceState *dev)
{
    SMMUv3State *s = ARM_SMMUV3(dev);
    SMMUv3Class *c = ARM_SMMUV3_GET_CLASS(s);

    c->parent_reset(dev);

    smmuv3_init_regs(s);
}

static void smmu_realize(DeviceState *d, Error **errp)
{
    SMMUState *sys = ARM_SMMU(d);
    SMMUv3State *s = ARM_SMMUV3(sys);
    SMMUv3Class *c = ARM_SMMUV3_GET_CLASS(s);
    SysBusDevice *dev = SYS_BUS_DEVICE(d);
    Error *local_err = NULL;

    c->parent_realize(d, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    memory_region_init_io(&sys->iomem, OBJECT(s),
                          &smmu_mem_ops, sys, TYPE_ARM_SMMUV3, 0x20000);

    sys->mrtypename = g_strdup(TYPE_SMMUV3_IOMMU_MEMORY_REGION);

    sysbus_init_mmio(dev, &sys->iomem);

    smmu_init_irq(s, dev);
}

static const VMStateDescription vmstate_smmuv3 = {
    .name = "smmuv3",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(features, SMMUv3State),
        VMSTATE_UINT8(sid_size, SMMUv3State),
        VMSTATE_UINT8(sid_split, SMMUv3State),

        VMSTATE_UINT32_ARRAY(idr, SMMUv3State, 6),
        VMSTATE_UINT32(iidr, SMMUv3State),
        VMSTATE_UINT32_ARRAY(cr, SMMUv3State, 3),
        VMSTATE_UINT32(cr0ack, SMMUv3State),
        VMSTATE_UINT32(statusr, SMMUv3State),
        VMSTATE_UINT32(irq_ctrl, SMMUv3State),
        VMSTATE_UINT32(irq_ctrl_ack, SMMUv3State),
        VMSTATE_UINT32(gerror, SMMUv3State),
        VMSTATE_UINT32(gerrorn, SMMUv3State),
        VMSTATE_UINT64(gerror_irq_cfg0, SMMUv3State),
        VMSTATE_UINT32(gerror_irq_cfg1, SMMUv3State),
        VMSTATE_UINT32(gerror_irq_cfg2, SMMUv3State),
        VMSTATE_UINT64(strtab_base, SMMUv3State),
        VMSTATE_UINT32(strtab_base_cfg, SMMUv3State),
        VMSTATE_UINT64(eventq_irq_cfg0, SMMUv3State),
        VMSTATE_UINT32(eventq_irq_cfg1, SMMUv3State),
        VMSTATE_UINT32(eventq_irq_cfg2, SMMUv3State),

        VMSTATE_UINT64(cmdq.base, SMMUv3State),
        VMSTATE_UINT32(cmdq.prod, SMMUv3State),
        VMSTATE_UINT32(cmdq.cons, SMMUv3State),
        VMSTATE_UINT8(cmdq.entry_size, SMMUv3State),
        VMSTATE_UINT64(eventq.base, SMMUv3State),
        VMSTATE_UINT32(eventq.prod, SMMUv3State),
        VMSTATE_UINT32(eventq.cons, SMMUv3State),
        VMSTATE_UINT8(eventq.entry_size, SMMUv3State),

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
    SMMUv3Class *c = ARM_SMMUV3_CLASS(klass);

    dc->vmsd    = &vmstate_smmuv3;
    device_class_set_parent_reset(dc, smmu_reset, &c->parent_reset);
    c->parent_realize = dc->realize;
    dc->realize = smmu_realize;
}

static void smmuv3_notify_flag_changed(IOMMUMemoryRegion *iommu,
                                       IOMMUNotifierFlag old,
                                       IOMMUNotifierFlag new)
{
    if (old == IOMMU_NOTIFIER_NONE) {
        error_setg(&error_fatal,
                   "SMMUV3: vhost and vfio notifiers not yet supported");
    }
}

static void smmuv3_iommu_memory_region_class_init(ObjectClass *klass,
                                                  void *data)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_CLASS(klass);

    imrc->translate = smmuv3_translate;
    imrc->notify_flag_changed = smmuv3_notify_flag_changed;
}

static const TypeInfo smmuv3_type_info = {
    .name          = TYPE_ARM_SMMUV3,
    .parent        = TYPE_ARM_SMMU,
    .instance_size = sizeof(SMMUv3State),
    .instance_init = smmuv3_instance_init,
    .class_size    = sizeof(SMMUv3Class),
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

