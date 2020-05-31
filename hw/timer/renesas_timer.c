/*
 * Renesas 16bit Compare-match timer
 *
 * Datasheet: RX62N Group, RX621 Group User's Manual: Hardware
 * (Rev.1.40 R01UH0033EJ0140)
 *
 * Copyright (c) 2019 Yoshinori Sato
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

#define IS_CMT(t) (t->feature == RTIMER_FEAT_CMT)

static int clkdiv(RTIMERState *tmr, int ch)
{
    if (IS_CMT(tmr)) {
        return 8 << (2 * FIELD_EX16(tmr->ch[ch].ctrl, CMCR, CKS));
    } else {
        if (FIELD_EX16(tmr->ch[ch].ctrl, TCR, TPSC) <= 5) {
            return 4 << (2 * FIELD_EX16(tmr->ch[ch].ctrl, TCR, TPSC));
        } else {
            return 0;
        }
    }
}

static void set_next_event(struct channel_rtimer *ch, int64_t now)
{
    int64_t next;
    RTIMERState *tmr = ch->tmrp;
    if (IS_CMT(tmr)) {
        next = ch->cor - ch->cnt;
    } else {
        next = ch->cnt;
    }
    next *= ch->clk;
    ch->base = now;
    ch->next = now + next;
    timer_mod(ch->timer, ch->next);
}

static void timer_event(void *opaque)
{
    struct channel_rtimer *ch = opaque;
    RTIMERState *tmr = ch->tmrp;

    if (IS_CMT(tmr)) {
        ch->cnt = 0;
        if (FIELD_EX16(ch->ctrl, CMCR, CMIE)) {
            qemu_irq_pulse(ch->irq);
        }
    } else {
        ch->cnt = ch->cor;
        if (!FIELD_EX16(ch->ctrl, TCR, UNF)) {
            ch->ctrl = FIELD_DP16(ch->ctrl, TCR, UNF, 1);
            qemu_set_irq(ch->irq, FIELD_EX16(ch->ctrl, TCR, UNIE));
        }
    }
    set_next_event(ch, ch->next);
}

static int64_t read_tcnt(RTIMERState *tmr, int ch)
{
    int64_t delta, now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    if (tmr->ch[ch].clk > 0) {
        delta = (now - tmr->ch[ch].base);
        delta /= tmr->ch[ch].clk;
        if (IS_CMT(tmr)) {
            return delta;
        } else {
            return tmr->ch[ch].cnt - delta;
        }
    } else {
        return tmr->ch[ch].cnt;
    }
}

static void tmr_start_stop(RTIMERState *tmr, int ch, int start)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    tmr->ch[ch].start = start;
    if (start) {
        if (!tmr->ch[ch].timer) {
            tmr->ch[ch].timer =
                timer_new_ns(QEMU_CLOCK_VIRTUAL, timer_event, &tmr->ch[ch]);
        }
        set_next_event(&tmr->ch[ch], now);
    } else {
        tmr->ch[ch].cnt = read_tcnt(tmr, ch);
        tmr->ch[ch].next = 0;
        if (tmr->ch[ch].timer) {
            timer_del(tmr->ch[ch].timer);
        }
    }
}

static void timer_register(RTIMERState *tmr, hwaddr addr, int *ch, int *reg)
{
    if (IS_CMT(tmr)) {
        /*  +0 - CMSTR (TSTR)  */
        /*  +2 - CMCR0  (TCR)  */
        /*  +4 - CMCNT0 (TCNT) */
        /*  +6 - CMCOR0 (TCOR) */
        /*  +8 - CMCR1  (TCR)  */
        /* +10 - CMCNT1 (TCNT) */
        /* +12 - CMCOR1 (TCOR) */
        addr /= 2;
        if (addr > 6) {
            /* Out of register area */
            *reg = -1;
            return;
        }
        if (addr == 0) {
            *ch = -1;
            *reg = R_TSTR;
        } else {
            *ch = addr / 4;
            if (addr < 4) {
                /* skip CMSTR */
                addr--;
            }
            *reg = 2 - (addr % 4);
        }
    } else {
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
        if (tmr->feature == RTIMER_FEAT_TMU_HIGH && addr >= 8) {
            *reg = -1;
            return;
        }
        addr /= 4;
        if (addr < 2) {
            *ch = -1;
            *reg = addr;
        } else if (addr < 11) {
            *ch = (addr - 2) / 3;
            *reg = (addr - 2) % 3 + 2;
        } else {
            *ch = 2;
            *reg = R_TCPR;
        }
    }
}

static uint64_t read_tstr(RTIMERState *tmr)
{
    uint64_t ret = 0;
    int ch;
    for (ch = 0; ch < tmr->num_ch; ch++) {
        ret = deposit64(ret, ch, 1, tmr->ch[ch].start);
    }
    return ret;
}

static void update_clk(RTIMERState *tmr, int ch)
{
    int tpsc;
    int t;
    if (!IS_CMT(tmr)) {
        /* Clock setting validation */
        tpsc = FIELD_EX16(tmr->ch[ch].ctrl, TCR, TPSC);
        switch (tpsc) {
        case 5:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "renesas_timer: Invalid TPSC valule %d.", tpsc);
            break;
        case 6:
        case 7:
            qemu_log_mask(LOG_UNIMP,
                          "renesas_timer: External clock not implemented.");
            break;
        }
        /* Interrupt clear */
        if (FIELD_EX16(tmr->ch[ch].ctrl, TCR, UNF) == 0) {
            qemu_set_irq(tmr->ch[ch].irq, 0);
        }
    }
    t = clkdiv(tmr, ch);
    if (t > 0) {
        t = tmr->input_freq / t;
        tmr->ch[ch].clk = NANOSECONDS_PER_SECOND / t;
    } else {
        tmr->ch[ch].clk = 0;
    }
}

static uint64_t tmr_read(void *opaque, hwaddr addr, unsigned size)
{
    RTIMERState *tmr = opaque;
    int ch = -1, reg = -1;

    timer_register(tmr, addr, &ch, &reg);
    switch (reg) {
    case R_TOCR:
        return tmr->tocr;
    case R_TSTR:
        return read_tstr(tmr);
    case R_TCR:
        return tmr->ch[ch].ctrl;
    case R_TCNT:
        if (tmr->ch[ch].start) {
            return read_tcnt(tmr, ch);
        } else {
            return tmr->ch[ch].cnt;
        }
    case R_TCOR:
        return tmr->ch[ch].cor;
    case R_TCPR:
        qemu_log_mask(LOG_UNIMP,
                      "renesas_timer: Input capture not implemented\n");
        return 0;
    default:
        qemu_log_mask(LOG_UNIMP, "renesas_timer: Register 0x%"
                      HWADDR_PRIX " not implemented\n", addr);
    }
    return UINT64_MAX;
}

static void tmr_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    RTIMERState *tmr = opaque;
    int ch = -1, reg = -1;
    uint16_t tcr_mask;

    timer_register(tmr, addr, &ch, &reg);
    switch (reg) {
    case R_TOCR:
        tmr->tocr = FIELD_DP8(tmr->tocr, TOCR, TCOE,
                              FIELD_EX8(val, TOCR, TCOE));
        break;
    case R_TSTR:
        for (ch = 0; ch < tmr->num_ch; ch++) {
            tmr_start_stop(tmr, ch, extract32(val, ch, 1));
        }
        break;
    case R_TCR:
        switch (tmr->feature) {
        case RTIMER_FEAT_CMT:
            tcr_mask = 0x00a3;
            /* bit7 always 1 */
            val |= 0x0080;
            break;
        case RTIMER_FEAT_TMU_LOW:
            tcr_mask = (ch < 2) ? 0x013f : 0x03ff;
            break;
        case RTIMER_FEAT_TMU_HIGH:
            tcr_mask = 0x0127;
            break;
        default:
            tcr_mask = 0x00ff;
            break;
        }
        /* Upper byte write only 0 */
        tmr->ch[ch].ctrl |= (tcr_mask & 0x00ff);
        tmr->ch[ch].ctrl &= val & tcr_mask;
        update_clk(tmr, ch);
        break;
    case R_TCNT:
        tmr->ch[ch].cnt = val;
        break;
    case R_TCOR:
        tmr->ch[ch].cor = val;
        break;
    case R_TCPR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas_timer: TCPR is read only.");
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "renesas_timer: Register 0x%"
                      HWADDR_PRIX " not implemented\n", addr);
    }
}

static const MemoryRegionOps tmr_ops = {
    .write = tmr_write,
    .read  = tmr_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 2,
        .max_access_size = 4,
    },
};

static void rtimer_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *d = SYS_BUS_DEVICE(dev);
    RTIMERState *tmr = RTIMER(dev);
    int i;
    int ch;

    if (tmr->input_freq == 0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas_timer: input-freq property must be set.");
        return;
    }
    if (IS_CMT(tmr)) {
        memory_region_init_io(&tmr->memory, OBJECT(tmr), &tmr_ops,
                              tmr, "renesas-cmt", 0x10);
        sysbus_init_mmio(d, &tmr->memory);

        for (i = 0; i < TIMER_CH_CMT; i++) {
            sysbus_init_irq(d, &tmr->ch[i].irq);
        }
        tmr->num_ch = 2;
    } else {
        memory_region_init_io(&tmr->memory, OBJECT(tmr), &tmr_ops,
                              tmr, "renesas-tmu", 0x30);
        sysbus_init_mmio(d, &tmr->memory);
        memory_region_init_alias(&tmr->memory_p4, NULL, "renesas-tmu-p4",
                                 &tmr->memory, 0, 0x30);
        sysbus_init_mmio(d, &tmr->memory_p4);
        memory_region_init_alias(&tmr->memory_a7, NULL, "renesas-tmu-a7",
                                 &tmr->memory, 0, 0x30);
        sysbus_init_mmio(d, &tmr->memory_a7);
        ch = (tmr->feature == RTIMER_FEAT_TMU_LOW) ?
            TIMER_CH_TMU : TIMER_CH_TMU - 1;
        for (i = 0; i < ch; i++) {
            sysbus_init_irq(d, &tmr->ch[i].irq);
        }
        tmr->num_ch = (tmr->feature == RTIMER_FEAT_TMU_LOW) ? 3 : 2;
    }
    for (ch = 0; ch < tmr->num_ch; ch++) {
        tmr->ch[ch].tmrp = tmr;
        update_clk(tmr, ch);
        if (IS_CMT(tmr)) {
            tmr->ch[ch].cor = 0xffff;
        } else {
            tmr->ch[ch].cor = tmr->ch[ch].cnt = 0xffffffff;
        }
    }
}

static const VMStateDescription vmstate_rtimer = {
    .name = "rx-cmt",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static Property rtimer_properties[] = {
    DEFINE_PROP_UINT32("feature", RTIMERState, feature, 0),
    DEFINE_PROP_UINT64("input-freq", RTIMERState, input_freq, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void rtimer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_rtimer;
    dc->realize = rtimer_realize;
    device_class_set_props(dc, rtimer_properties);
}

static const TypeInfo rtimer_info = {
    .name       = TYPE_RENESAS_TIMER,
    .parent     = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RTIMERState),
    .class_init = rtimer_class_init,
};

static void rtimer_register_types(void)
{
    type_register_static(&rtimer_info);
}

type_init(rtimer_register_types)
