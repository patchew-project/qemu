/*
 * Renesas 8bit timer
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
#include "qemu/bitops.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/qdev-properties.h"
#include "hw/timer/renesas_8timer.h"
#include "migration/vmstate.h"
#include "qemu/error-report.h"

REG8(TCR, 0)
  FIELD(TCR, CCLR, 3, 2)
  FIELD(TCR, OVIE, 5, 1)
  FIELD(TCR, CMIEA, 6, 1)
  FIELD(TCR, CMIEB, 7, 1)
REG8(TCSR, 2)
  FIELD(TCSR, OSA, 0, 2)
  FIELD(TCSR, OSB, 2, 2)
  FIELD(TCSR, ADTE, 4, 1)
REG8(TCORA, 4)
REG8(TCORB, 6)
REG8(TCNT, 8)
REG8(TCCR, 10)
  FIELD(TCCR, CKS, 0, 3)
  FIELD(TCCR, CSS, 3, 2)
  FIELD(TCCR, TMRIS, 7, 1)

#define CLK_EVT -1

static void update_clk(RTMRState *tmr, int ch)
{
    int64_t t;
    static const int divlist[] = {1, 2, 8, 32, 64, 1024, 8192, 0};
    switch (FIELD_EX8(tmr->ch[ch].tccr, TCCR, CSS)) {
    case 0:
        qemu_log_mask(LOG_UNIMP,
                      "renesas_8timer: External clock not implemented.");
        tmr->ch[ch].clk = 0;
        break;
    case 1:
        /* Internal clock */
        t = divlist[FIELD_EX8(tmr->ch[ch].tccr, TCCR, CKS)];
        if (t > 0) {
            tmr->ch[ch].clk = tmr->input_freq / t;
            tmr->ch[ch].clk = NANOSECONDS_PER_SECOND / t;
        } else {
            tmr->ch[ch].clk = 0;
        }
        break;
    case 2:
        qemu_log_mask(LOG_UNIMP,
                      "renesas_8timer: CSS undefined.");
        tmr->ch[ch].clk = 0;
        break;
    case 3:
        /* Event count */
        tmr->ch[ch].clk = CLK_EVT;
        break;
    }
}

static uint16_t catreg(uint8_t hi, uint8_t lo)
{
    uint16_t ret = 0;
    ret = deposit32(ret, 8, 8, hi);
    ret = deposit32(ret, 0, 8, lo);
    return ret;
}

static int is_clr(uint8_t tcr, int event)
{
    return FIELD_EX8(tcr, TCR, CCLR) == (event + 1);
}

static int is_irq(uint8_t tcr, int event)
{
    switch (event) {
    case cmia:
        return FIELD_EX8(tcr, TCR, CMIEA);
    case cmib:
        return FIELD_EX8(tcr, TCR, CMIEB);
    case ovi:
        return FIELD_EX8(tcr, TCR, OVIE);
    default:
        g_assert_not_reached();
    }
}

static void set_next_event(RTMRState *tmr, int ch)
{
    int64_t next = 0;
    int evt;
    int cor;
    int min;
    if (ch == 1 && tmr->ch[ch].word) {
        /* 16bit count mode */
        next = 0x10000 - catreg(tmr->ch[0].cnt, tmr->ch[1].cnt);
        next *= tmr->ch[1].clk;
        tmr->ch[0].event = tmr->ch[1].event = wovi;
    } else if (tmr->ch[1].clk > 0) {
        min = 0x101;
        for (evt = cmia; evt < TMR_NR_EVENTS - 1; evt++) {
            cor = (evt < TMR_NR_COR) ? (tmr->ch[ch].cor[evt]) : 0xff;
            /* event happend compare match +1 */
            cor++;
            if (tmr->ch[ch].cnt < cor) {
                if (evt < TMR_NR_COR &&
                    !is_clr(tmr->ch[ch].tcr, evt) &&
                    !is_irq(tmr->ch[ch].tcr, evt)) {
                    /* no event happend */
                    continue;
                }
                if (min > cor) {
                    min = cor;
                    next = cor - tmr->ch[ch].cnt;
                    next *= tmr->ch[ch].clk;
                    tmr->ch[ch].event = evt;
                }
            }
        }
    }
    if (next > 0) {
        tmr->ch[ch].base = tmr->ch[ch].next;
        tmr->ch[ch].next += next;
        timer_mod(tmr->ch[ch].timer, tmr->ch[ch].next);
    } else {
        timer_del(tmr->ch[ch].timer);
    }
}

static void event_countup(struct channel_8tmr *ch)
{
    int evt;
    int cor;

    ch->cnt++;
    for (evt = cmia; evt < TMR_NR_COR - 1; evt++) {
        cor = ch->cor[evt];
        if (ch->cnt == (cor + 1) && is_clr(ch->tcr, evt)) {
            ch->cnt = 0;
        }
        if (ch->cnt == cor && is_irq(ch->tcr, evt)) {
            qemu_irq_pulse(ch->irq[evt]);
        }
    }
    if (ch->cnt == 0x100) {
        ch->cnt = 0;
        if (is_irq(ch->tcr, ovi)) {
            qemu_irq_pulse(ch->irq[IRQ_OVI]);
        }
    }
}

static void timer_event(void *opaque)
{
    struct channel_8tmr *ch = opaque;
    RTMRState *tmr = ch->tmrp;

    switch (ch->event) {
    case cmia:
        if (ch->id == 0 && tmr->ch[1].clk == CLK_EVT) {
            /* CH1 event count */
            event_countup(&tmr->ch[1]);
        }
        /* Falls through. */
    case cmib:
        if (FIELD_EX8(ch->tcr, TCR, CCLR) == (ch->event + 1)) {
            ch->cnt = 0;
        } else {
            ch->cnt = ch->cor[ch->event] + 1;
        }
        if (is_irq(ch->tcr, ch->event)) {
            qemu_irq_pulse(ch->irq[ch->event]);
        }
        break;
    case ovi:
        ch->cnt = 0;
        if (is_irq(ch->tcr, ovi)) {
            qemu_irq_pulse(ch->irq[IRQ_OVI]);
        }
        if (ch->id == 1 && tmr->ch[0].clk == CLK_EVT) {
            /* CH0 event count */
            event_countup(&tmr->ch[0]);
        }
        break;
    case wovi:
        tmr->ch[0].cnt = tmr->ch[1].cnt = 0;
        if (is_irq(tmr->ch[0].tcr, ovi)) {
            qemu_irq_pulse(tmr->ch[0].irq[IRQ_OVI]);
        }
        break;
    default:
        g_assert_not_reached();
    }
    set_next_event(tmr, ch->id);
}

static uint16_t read_tcnt(RTMRState *tmr, unsigned int size, int ch)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    int64_t delta;
    uint8_t ret[2];
    int i;

    switch (size) {
    case 1:
        if (tmr->ch[ch].clk > 0) {
            delta = now - tmr->ch[ch].base;
            delta /= tmr->ch[ch].clk;
        } else {
            delta = 0;
        }
        return tmr->ch[ch].cnt + delta;
    case 2:
        if (tmr->ch[1].word) {
            /* 16bit count mode */
            delta = now - tmr->ch[1].base;
            delta /= tmr->ch[1].clk;
            return catreg(tmr->ch[0].cnt, tmr->ch[1].cnt) + delta;
        } else {
            for (i = 0; i < TMR_CH; i++) {
                if (tmr->ch[ch].clk > 0) {
                    delta = now - tmr->ch[ch].base;
                    delta /= tmr->ch[ch].clk;
                } else {
                    delta = 0;
                }
                ret[i] = tmr->ch[i].cnt + delta;
            }
            return catreg(ret[0], ret[1]);
        }
    default:
        g_assert_not_reached();
    }
}

static int validate_access(hwaddr addr, unsigned int size)
{
    /* Byte access always OK */
    if (size == 1) {
        return 1;
    }
    return (addr >= A_TCORA);
}

static uint64_t tmr_read(void *opaque, hwaddr addr, unsigned int size)
{
    RTMRState *tmr = opaque;
    int ch = addr & 1;
    int cor;

    if (!validate_access(addr, size)) {
        qemu_log_mask(LOG_GUEST_ERROR, "renesas_tmr: Invalid read size 0x%"
                      HWADDR_PRIX "\n", addr);
        return UINT64_MAX;
    }
    switch (addr & ~1) {
    case A_TCR:
        return tmr->ch[ch].tcr;
    case A_TCSR:
        return tmr->ch[ch].tcsr;
    case A_TCORA:
    case A_TCORB:
        cor = (addr & 2) >> 1;
        if (size == 1) {
            return tmr->ch[ch].cor[cor];
        } else {
            return catreg(tmr->ch[0].cor[cor], tmr->ch[1].cor[cor]);
        }
    case A_TCNT:
        return read_tcnt(tmr, size, ch);
    case A_TCCR:
        if (size == 1) {
            return tmr->ch[ch].tccr;
        } else {
            return catreg(tmr->ch[0].tccr, tmr->ch[1].tccr);
        }
    default:
        qemu_log_mask(LOG_UNIMP, "renesas_tmr: Register 0x%" HWADDR_PRIX
                      " not implemented\n", addr);
        break;
    }
    return UINT64_MAX;
}

static void tmr_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    RTMRState *tmr = opaque;
    int ch = addr & 1;
    int cor;
    int64_t now;

    if (!validate_access(addr, size)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas_tmr: Invalid write size 0x%" HWADDR_PRIX
                      "\n", addr);
        return;
    }
    switch (addr & ~1) {
    case A_TCR:
        tmr->ch[ch].tcr = val;
        break;
    case A_TCSR:
        if (ch == 1) {
            val = FIELD_DP8(val, TCSR, ADTE, 1);
        }
        tmr->ch[ch].tcsr = val;
        break;
    case A_TCORA:
    case A_TCORB:
        cor = (addr & 2) >> 1;
        if (size == 1) {
            tmr->ch[ch].cor[cor] = val;
        } else {
            tmr->ch[0].cor[cor] = extract32(val, 0, 8);
            tmr->ch[1].cor[cor] = extract32(val, 8, 8);
        }
        break;
    case A_TCNT:
        now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        if (size == 1) {
            tmr->ch[ch].base = now;
            tmr->ch[ch].cnt = val;
        } else {
            tmr->ch[0].base = tmr->ch[1].base = now;
            tmr->ch[0].cnt = extract32(val, 0, 8);
            tmr->ch[1].cnt = extract32(val, 8, 8);
        }
        break;
    case A_TCCR:
        val &= ~0x6060;
        if (size == 1) {
            tmr->ch[ch].tccr = val;
            update_clk(tmr, ch);
        } else {
            tmr->ch[0].tccr = extract32(val, 0, 8);
            tmr->ch[1].tccr = extract32(val, 8, 8);
            update_clk(tmr, 0);
            update_clk(tmr, 1);
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "renesas_tmr: Register 0x%" HWADDR_PRIX
                      " not implemented\n", addr);
        return;
    }
    /*
     * If some conditions are met,
     * the event does not occur until the 16-bit counter overflows.
     */
    tmr->ch[1].word = (tmr->ch[0].clk == -1 && tmr->ch[1].clk > 0 &&
                       (tmr->ch[0].tcr & 0xe8) == 0x00 &&
                       (tmr->ch[0].tcr & 0xf8) == 0x00);
    if (size == 1) {
        set_next_event(tmr, ch);
    } else {
        set_next_event(tmr, 0);
        set_next_event(tmr, 1);
    }
}

static const MemoryRegionOps tmr_ops = {
    .write = tmr_write,
    .read  = tmr_read,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 2,
    },
};

static void rtmr_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *d = SYS_BUS_DEVICE(dev);
    RTMRState *tmr = RTMR(dev);
    int i;

    if (tmr->input_freq == 0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas_8tmr: input-freq property must be set.");
        return;
    }

    memory_region_init_io(&tmr->memory, OBJECT(tmr), &tmr_ops,
                          tmr, "renesas-8tmr", 0x10);
    sysbus_init_mmio(d, &tmr->memory);

    for (i = 0; i < TMR_CH; i++) {
        sysbus_init_irq(d, &tmr->ch[i].irq[IRQ_CMIA]);
        sysbus_init_irq(d, &tmr->ch[i].irq[IRQ_CMIB]);
        sysbus_init_irq(d, &tmr->ch[i].irq[IRQ_OVI]);
        tmr->ch[i].id = i;
        tmr->ch[i].timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                        timer_event, &tmr->ch[i]);
        tmr->ch[i].tmrp = tmr;
        tmr->ch[i].tcr = 0x00;
        tmr->ch[i].tcsr = (i == 0) ? 0x00 : 0x10;
        tmr->ch[i].cnt = 0x00;
        tmr->ch[i].cor[0] = 0xff;
        tmr->ch[i].cor[1] = 0xff;
        tmr->ch[i].tccr = 0x00;
    }
}

static const VMStateDescription vmstate_rtmr = {
    .name = "renesas-8tmr",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static Property rtmr_properties[] = {
    DEFINE_PROP_UINT64("input-freq", RTMRState, input_freq, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void rtmr_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_rtmr;
    dc->realize = rtmr_realize;
    device_class_set_props(dc, rtmr_properties);
}

static const TypeInfo rtmr_info = {
    .name       = TYPE_RENESAS_8TMR,
    .parent     = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RTMRState),
    .class_init = rtmr_class_init,
};

static void rtmr_register_types(void)
{
    type_register_static(&rtmr_info);
}

type_init(rtmr_register_types)
