/*
 * Renesas 16bit/32bit Compare-match timer (CMT/TMU)
 *
 * Datasheet: RX62N Group, RX621 Group User's Manual: Hardware
 *            (Rev.1.40 R01UH0033EJ0140)
 *        And SH7751 Group, SH7751R Group User's Manual: Hardware
 *            (Rev.4.01 R01UH0457EJ0401)
 *
 * Copyright (c) 2021 Yoshinori Sato
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "qemu/timer.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/qdev-properties.h"
#include "hw/timer/renesas_timer.h"
#include "migration/vmstate.h"
#include "qemu/error-report.h"

REG32(TOCR, 0)
  FIELD(TOCR, TCOE, 0, 1)
REG32(CMSTR, 0)
REG32(TSTR, 4)
REG32(TCOR, 8)
REG32(TCNT, 12)
REG32(TCR, 16)
  FIELD(TCR, TPSC, 0, 3)
  FIELD(TCR, CKEG, 3, 2)
  FIELD(TCR, UNIE, 5, 1)
  FIELD(TCR, ICPE, 6, 2)
  FIELD(TCR, UNF, 8, 1)
  FIELD(TCR, ICPF, 9, 1)
REG32(CMCR, 16)
  FIELD(CMCR, CKS, 0, 2)
  FIELD(CMCR, CMIE, 6, 1)
REG32(TCPR, 20)

static int cmt_div(RenesasTimerBaseState *tmr, int ch)
{
    return 8 << (2 * FIELD_EX16(tmr->ch[ch].ctrl, CMCR, CKS));
}

static int tmu_div(RenesasTimerBaseState *tmr, int ch)
{
    if (FIELD_EX16(tmr->ch[ch].ctrl, TCR, TPSC) <= 5) {
        return 4 << (2 * FIELD_EX16(tmr->ch[ch].ctrl, TCR, TPSC));
    } else {
        return 0;
    }

}

static void cmt_timer_event(void *opaque)
{
    struct rtimer_ch *ch = opaque;
    if (FIELD_EX16(ch->ctrl, CMCR, CMIE)) {
        qemu_irq_pulse(ch->irq);
    }
}

static void tmu_timer_event(void *opaque)
{
    struct rtimer_ch *ch = opaque;
    if (!FIELD_EX16(ch->ctrl, TCR, UNF)) {
        ch->ctrl = FIELD_DP16(ch->ctrl, TCR, UNF, 1);
        qemu_set_irq(ch->irq, FIELD_EX16(ch->ctrl, TCR, UNIE));
    }
}

static int64_t downcount(int64_t val, ptimer_state *t)
{
    return val;
}

static int64_t upcount(int64_t val, ptimer_state *t)
{
    int64_t limit;
    limit = ptimer_get_limit(t);
    return limit - val;
}

static void tmr_start_stop(RenesasTimerBaseState *tmr, int ch, int st)
{
    ptimer_transaction_begin(tmr->ch[ch].timer);
    switch (st) {
    case TIMER_STOP:
        ptimer_stop(tmr->ch[ch].timer);
        tmr->ch[ch].start = false;
        break;
    case TIMER_START:
        ptimer_run(tmr->ch[ch].timer, 0);
        tmr->ch[ch].start = true;
        break;
    }
    ptimer_transaction_commit(tmr->ch[ch].timer);
}

static uint64_t read_tstr(RenesasTimerBaseState *tmr)
{
    uint64_t ret = 0;
    int ch;
    for (ch = 0; ch < tmr->num_ch; ch++) {
        ret = deposit64(ret, ch, 1, tmr->ch[ch].start);
    }
    return ret;
}

static void update_clk(RenesasTimerBaseState *tmr, int ch)
{
    RenesasTimerBaseClass *tc = RENESAS_TIMER_BASE_GET_CLASS(tmr);
    int t;
    ptimer_state *p;
    t = tc->divrate(tmr, ch);
    p = tmr->ch[ch].timer;
    ptimer_transaction_begin(p);
    if (t > 0) {
        ptimer_set_freq(p, tmr->input_freq / t);
    } else {
        ptimer_stop(p);
    }
    ptimer_transaction_commit(p);
}

static void tmu_update_clk(RenesasTimerBaseState *tmr, int ch)
{
    /* Clock setting validation */
    int tpsc = FIELD_EX16(tmr->ch[ch].ctrl, TCR, TPSC);
    switch (tpsc) {
    case 5:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas_timer: Invalid TPSC valule %d.\n", tpsc);
        break;
    case 6:
    case 7:
        qemu_log_mask(LOG_UNIMP,
                      "renesas_timer: External clock not implemented.\n");
        break;
    }
    /* Interrupt clear */
    if (FIELD_EX16(tmr->ch[ch].ctrl, TCR, UNF) == 0) {
        qemu_set_irq(tmr->ch[ch].irq, 0);
    }
    update_clk(tmr, ch);
}

static uint64_t channel_read(RenesasTimerBaseState *tmr, int ch, int reg)
{
    RenesasTimerBaseClass *tc = RENESAS_TIMER_BASE_GET_CLASS(tmr);
    ptimer_state *p = tmr->ch[ch].timer;
    switch (reg) {
    case R_TCR:
        return tmr->ch[ch].ctrl;
    case R_TCNT:
        return tc->convert_count(ptimer_get_count(p), p);
    case R_TCOR:
        return ptimer_get_limit(p);
    }
    return UINT64_MAX;
}

static uint64_t cmt_read(void *opaque, hwaddr addr, unsigned size)
{
    RenesasCMTState *cmt = RENESAS_CMT(opaque);
    int ch, reg;

    /*  +0 - CMSTR (TSTR)  */
    /*  +2 - CMCR0  (TCR)  */
    /*  +4 - CMCNT0 (TCNT) */
    /*  +6 - CMCOR0 (TCOR) */
    /*  +8 - CMCR1  (TCR)  */
    /* +10 - CMCNT1 (TCNT) */
    /* +12 - CMCOR1 (TCOR) */
    addr /= 2;
    if (addr == R_CMSTR) {
        return read_tstr(RENESAS_TIMER_BASE(cmt));
    } else {
        ch = addr / 4;
        if (addr < 4) {
            /* skip CMSTR */
            addr--;
        }
        reg = 2 - (addr % 4);
        return channel_read(RENESAS_TIMER_BASE(cmt), ch, reg);
    }
}

static uint64_t tmu_read(void *opaque, hwaddr addr, unsigned size)
{
    RenesasTMUState *tmu = RENESAS_TMU(opaque);
    RenesasTimerBaseState *tmr = RENESAS_TIMER_BASE(tmu);
    int ch = -1, reg = -1;

    /*  +0 - TCOR  */
    /*  +4 - TSTR  */
    /*  +8 - TCOR0 */
    /* +12 - TCNT0 */
    /* +16 - TCR0  */
    /* +20 - TCOR1 */
    /* +24 - TCNT1 */
    /* +28 - TCR1  */
    /* +32 - TCOR2 */
    /* +36 - TCNT2 */
    /* +40 - TCR2  */
    /* +44 - TCPR2 */

    if (tmr->unit != 0 && addr >= 32) {
        /* UNIT1 channel2 is not exit */
        qemu_log_mask(LOG_UNIMP, "renesas_timer: Register 0x%"
                      HWADDR_PRIX " not implemented\n", addr);
        return UINT64_MAX;
    }
    addr /= 4;
    switch (addr) {
    case R_TOCR:
        return tmu->tocr;
    case R_TSTR:
        return read_tstr(RENESAS_TIMER_BASE(tmu));
    case R_TCPR:
        qemu_log_mask(LOG_UNIMP,
                      "renesas_timer: Input capture not implemented.\n");
        return UINT64_MAX;
    default:
        ch = (addr - 2) / 3;
        reg = (addr - 2) % 3 + 2;
        return channel_read(RENESAS_TIMER_BASE(tmu), ch, reg);
    }
}

static void write_tstr(RenesasTimerBaseState *tmr, uint16_t val)
{
    int ch;
    for (ch = 0; ch < tmr->num_ch; ch++) {
        tmr_start_stop(tmr, ch, extract16(val, ch, 1));
    }
}

static void write_tcr(RenesasTimerBaseState *tmr, int ch,
                      uint16_t val, uint16_t regmask)
{
    RenesasTimerBaseClass *tc = RENESAS_TIMER_BASE_GET_CLASS(tmr);
    tmr->ch[ch].ctrl |= (regmask & 0x00ff);
    tmr->ch[ch].ctrl &= val & regmask;
    tc->update_clk(tmr, ch);
}

static void channel_write(RenesasTimerBaseState *tmr, int ch,
                         int reg, uint64_t val)
{
    RenesasTimerBaseClass *tc = RENESAS_TIMER_BASE_GET_CLASS(tmr);
    ptimer_state *t;
    t = tmr->ch[ch].timer;
    ptimer_transaction_begin(t);
    switch (reg) {
    case R_TCNT:
        ptimer_set_count(t, tc->convert_count(val, t));
        break;
    case R_TCOR:
        ptimer_set_limit(t, val, 0);
        break;
    default:
        g_assert_not_reached();
    }
    ptimer_transaction_commit(t);
}

static void cmt_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    RenesasTimerBaseState *tmr = RENESAS_TIMER_BASE(opaque);
    int ch, reg;
    uint16_t mask;

    addr /= 2;
    if (addr == R_CMSTR) {
        write_tstr(tmr, val);
    } else {
        ch = addr / 4;
        if (addr < 4) {
            /* skip CMSTR */
            addr--;
        }
        reg = (2 - (addr % 4)) + 2;
        if (reg == R_TCR) {
            /* bit7 always 1 */
            val |= 0x0080;
            mask = 0;
            mask = FIELD_DP16(mask, CMCR, CKS, 3);
            mask = FIELD_DP16(mask, CMCR, CMIE, 1);
            write_tcr(RENESAS_TIMER_BASE(tmr), ch, val, mask);
        } else {
            channel_write(RENESAS_TIMER_BASE(tmr), ch, reg, val);
        }
    }
}

static void tmu_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    RenesasTMUState *tmu = RENESAS_TMU(opaque);
    RenesasTimerBaseState *tmr = RENESAS_TIMER_BASE(tmu);

    int ch, reg;
    uint16_t tcr_mask;

    if (tmr->unit != 0 && addr >= 32) {
        /* UNIT1 channel2 is not exit */
        qemu_log_mask(LOG_UNIMP, "renesas_timer: Register 0x%"
                      HWADDR_PRIX " not implemented\n", addr);
        return;
    }
    addr /= 4;
    switch (addr) {
    case R_TOCR:
        tmu->tocr = FIELD_DP8(tmu->tocr, TOCR, TCOE,
                              FIELD_EX8(val, TOCR, TCOE));
        break;
    case R_TSTR:
        write_tstr(tmr, val);
        break;
    case R_TCPR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas_timer: TCPR is read only.\n");
        break;
    default:
        ch = (addr - 2) / 3;
        reg = (addr - 2) % 3 + 2;
        if (reg == R_TCR) {
            tcr_mask = 0;
            tcr_mask = FIELD_DP16(tcr_mask, TCR, TPSC, 7);
            tcr_mask = FIELD_DP16(tcr_mask, TCR, UNIE, 1);
            tcr_mask = FIELD_DP16(tcr_mask, TCR, UNF, 1);
            if (tmr->unit == 0) {
                tcr_mask = FIELD_DP16(tcr_mask, TCR, CKEG, 3);
                if (ch == 2) {
                    tcr_mask = FIELD_DP16(tcr_mask, TCR, ICPE, 3);
                    tcr_mask = FIELD_DP16(tcr_mask, TCR, ICPF, 1);
                }
            }
            write_tcr(tmr, ch, val, tcr_mask);
        } else {
            channel_write(tmr, ch, reg, val);
        }
        break;
    }
}

static const MemoryRegionOps cmt_ops = {
    .write = cmt_write,
    .read  = cmt_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 2,
        .max_access_size = 2,
    },
};

static const MemoryRegionOps tmu_ops = {
    .write = tmu_write,
    .read  = tmu_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 2,
        .max_access_size = 4,
    },
};

static void timer_base_realize(RenesasTimerBaseState *tmr, int num_ch)
{
    RenesasTimerBaseClass *tc = RENESAS_TIMER_BASE_GET_CLASS(tmr);
    int i;
    tmr->num_ch = num_ch;
    for (i = 0; i < num_ch; i++) {
        tmr->ch[i].timer = ptimer_init(tc->timer_event, &tmr->ch[i], 0);
    }
}

static void cmt_realize(DeviceState *dev, Error **errp)
{
    RenesasCMTState *cmt = RENESAS_CMT(dev);
    RenesasTimerBaseState *tmr = RENESAS_TIMER_BASE(cmt);
    int i;

    timer_base_realize(tmr, TIMER_CH_CMT);

    for (i = 0; i < TIMER_CH_CMT; i++) {
        ptimer_transaction_begin(tmr->ch[i].timer);
        ptimer_set_limit(tmr->ch[i].timer, 0xffff, 0);
        ptimer_transaction_commit(tmr->ch[i].timer);
        update_clk(tmr, i);
    }
}

static void cmt_init(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    RenesasCMTState *cmt = RENESAS_CMT(obj);
    RenesasTimerBaseState *tmr = RENESAS_TIMER_BASE(cmt);
    int i;

    memory_region_init_io(&tmr->memory, obj, &cmt_ops,
                          tmr, "renesas-cmt", 0x10);
    sysbus_init_mmio(d, &tmr->memory);

    for (i = 0; i < TIMER_CH_CMT; i++) {
        sysbus_init_irq(d, &tmr->ch[i].irq);
    }
}

static void tmu_realize(DeviceState *dev, Error **errp)
{
    RenesasTMUState *tmu = RENESAS_TMU(dev);
    RenesasTimerBaseState *tmr = RENESAS_TIMER_BASE(tmu);
    int i;
    int num_ch;

    /* Unit0 have 3ch, Unit1 have 2ch */
    num_ch = TIMER_CH_TMU - tmr->unit;
    timer_base_realize(tmr, num_ch);
    for (i = 0; i < num_ch; i++) {
        ptimer_transaction_begin(tmr->ch[i].timer);
        ptimer_set_limit(tmr->ch[i].timer, 0xffffffff, 0);
        ptimer_transaction_commit(tmr->ch[i].timer);
        update_clk(tmr, i);
    }
}

static void tmu_init(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    RenesasTimerBaseState *tmr = RENESAS_TIMER_BASE(obj);
    RenesasTMUState *tmu = RENESAS_TMU(obj);
    int i;

    memory_region_init_io(&tmr->memory, obj, &tmu_ops,
                          tmr, "renesas-tmu", 0x30);
    sysbus_init_mmio(d, &tmr->memory);
    memory_region_init_alias(&tmu->memory_p4, NULL, "renesas-tmu-p4",
                             &tmr->memory, 0, 0x30);
    sysbus_init_mmio(d, &tmu->memory_p4);
    memory_region_init_alias(&tmu->memory_a7, NULL, "renesas-tmu-a7",
                             &tmr->memory, 0, 0x30);
    sysbus_init_mmio(d, &tmu->memory_a7);
    for (i = 0; i < TIMER_CH_TMU; i++) {
        sysbus_init_irq(d, &tmr->ch[i].irq);
    }
}

static Property renesas_timer_properties[] = {
    DEFINE_PROP_INT32("unit", RenesasTimerBaseState, unit, 0),
    DEFINE_PROP_UINT64("input-freq", RenesasTimerBaseState, input_freq, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void renesas_timer_base_class_init(ObjectClass *klass, void *data)
{
    RenesasTimerBaseClass *base = RENESAS_TIMER_BASE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    base->update_clk = update_clk;
    device_class_set_props(dc, renesas_timer_properties);
}

static void cmt_class_init(ObjectClass *klass, void *data)
{
    RenesasTimerBaseClass *base = RENESAS_TIMER_BASE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    base->divrate = cmt_div;
    base->timer_event = cmt_timer_event;
    base->convert_count = upcount;
    base->update_clk = update_clk;
    dc->realize = cmt_realize;
}

static void tmu_class_init(ObjectClass *klass, void *data)
{
    RenesasTimerBaseClass *base = RENESAS_TIMER_BASE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    base->divrate = tmu_div;
    base->timer_event = tmu_timer_event;
    base->convert_count = downcount;
    base->update_clk = tmu_update_clk;
    dc->realize = tmu_realize;
}

static const TypeInfo renesas_timer_info[] = {
    {
        .name       = TYPE_RENESAS_TIMER_BASE,
        .parent     = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(RenesasTimerBaseState),
        .class_init = renesas_timer_base_class_init,
        .class_size = sizeof(RenesasTimerBaseClass),
        .abstract = true,
    },
    {
        .name       = TYPE_RENESAS_CMT,
        .parent     = TYPE_RENESAS_TIMER_BASE,
        .instance_size = sizeof(RenesasCMTState),
        .instance_init = cmt_init,
        .class_init = cmt_class_init,
        .class_size = sizeof(RenesasCMTClass),
    },
    {
        .name       = TYPE_RENESAS_TMU,
        .parent     = TYPE_RENESAS_TIMER_BASE,
        .instance_size = sizeof(RenesasTMUState),
        .instance_init = tmu_init,
        .class_init = tmu_class_init,
        .class_size = sizeof(RenesasTMUClass),
    },
};

DEFINE_TYPES(renesas_timer_info)
