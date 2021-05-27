/*
 * Renesas 8bit timer
 *
 * Datasheet: RX62N Group, RX621 Group User's Manual: Hardware
 *            (Rev.1.40 R01UH0033EJ0140)
 *
 * Copyright (c) 2020 Yoshinori Sato
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
#include "qemu/bitops.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-clock.h"
#include "hw/timer/renesas_tmr8.h"
#include "migration/vmstate.h"
#include "qemu/error-report.h"

REG8(TCR, 0)
  FIELD(TCR, CCLR, 3, 2)
  FIELD(TCR, OVIE, 5, 1)
  FIELD(TCR, CMIEA, 6, 1)
  FIELD(TCR, CMIEB, 7, 1)
  FIELD(TCR, CMIE, 6, 2)
  FIELD(TCR, ALLIE, 5, 3)
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

enum CSS {
    CSS_EXT = 0,        /* extarnal clock */
    CSS_INT = 1,        /* internal clock */
    CSS_UND = 2,        /* undefined */
    CSS_EVT = 3,        /* event count */
};

static void update_clk(RenesasTMR8State *tmr, int ch)
{
    int64_t t;
    static const int divlist[] = {1, 2, 8, 32, 64, 1024, 8192, 0};
    switch (FIELD_EX8(tmr->ch[ch].tccr, TCCR, CSS)) {
    case CSS_EXT:
        qemu_log_mask(LOG_UNIMP,
                      "renesas_tmr8: External clock not implemented.\n");
        tmr->ch[ch].clk = 0;
        break;
    case CSS_INT:
        t = divlist[FIELD_EX8(tmr->ch[ch].tccr, TCCR, CKS)];
        if (t > 0 && clock_is_enabled(tmr->pck)) {
            t = tmr->input_freq / t;
            tmr->ch[ch].clk = NANOSECONDS_PER_SECOND / t;
        } else {
            tmr->ch[ch].clk = 0;
        }
        break;
    case CSS_UND:
        qemu_log_mask(LOG_UNIMP,
                      "renesas_8timer: CSS undefined.");
        tmr->ch[ch].clk = 0;
        break;
    case CSS_EVT:
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

static bool is_clr_count(uint8_t tcr, enum timer_event event)
{
    switch (event) {
    case EVT_CMIA:
    case EVT_CMIB:
        return FIELD_EX8(tcr, TCR, CCLR) == event;
    case EVT_OVI:
        return true;
    default:
        g_assert_not_reached();
    }
}

static bool is_irq_enabled(uint8_t tcr, enum timer_event event)
{
    switch (event) {
    case EVT_CMIA:
        return FIELD_EX8(tcr, TCR, CMIEA);
    case EVT_CMIB:
        return FIELD_EX8(tcr, TCR, CMIEB);
    case EVT_OVI:
        return FIELD_EX8(tcr, TCR, OVIE);
    default:
        g_assert_not_reached();
    }
}

static bool event_enabled(uint8_t tcr, enum timer_event event)
{
    return is_clr_count(tcr, event) || is_irq_enabled(tcr, event);
}

static int event_cor(struct tmr8_ch *ch, enum timer_event event)
{
    switch (event) {
    case EVT_CMIA:
        return ch->cor[REG_A];
    case EVT_CMIB:
        return ch->cor[REG_B];
    default:
        return 0xff;
    }
}

static bool is_word_mode(RenesasTMR8State *tmr)
{
    /*
     * If the following conditions are met, it is treated as a 16-bit counter.
     * ch0 - free running and no compare match event
     * ch1 - free running no event
     */
    return tmr->ch[0].clk == CLK_EVT &&
        tmr->ch[1].clk > 0 &&
        FIELD_EX8(tmr->ch[0].tcr, TCR, CCLR) == 0 &&
        FIELD_EX8(tmr->ch[0].tcr, TCR, CMIE) == 0 &&
        FIELD_EX8(tmr->ch[0].tccr, TCCR, CSS) == CSS_EVT &&
        FIELD_EX8(tmr->ch[1].tcr, TCR, CCLR) == 0 &&
        FIELD_EX8(tmr->ch[1].tcr, TCR, ALLIE) == 0;
}

static void set_next_event(RenesasTMR8State *tmr, int ch)
{
    int64_t next = 0;
    enum timer_event evt;
    int cor;
    int min;
    if (ch == 1 && is_word_mode(tmr)) {
        /* 16bit count mode */
        next = 0x10000 - catreg(tmr->ch[0].cnt, tmr->ch[1].cnt);
        next *= tmr->ch[1].clk;
        tmr->ch[0].event = tmr->ch[1].event = EVT_WOVI;
    } else if (tmr->ch[ch].clk > 0) {
        /* Find the next event. */
        min = 0x100 + 1;
        for (evt = EVT_CMIA; evt < EVT_WOVI; evt++) {
            cor = event_cor(&tmr->ch[ch], evt);
            /* event happen in next count up */
            cor++;
            if (tmr->ch[ch].cnt < cor && min > cor &&
                event_enabled(tmr->ch[ch].tcr, evt)) {
                    min = cor;
                    next = cor - tmr->ch[ch].cnt;
                    next *= tmr->ch[ch].clk;
                    tmr->ch[ch].event = evt;
            }
        }
    }
    if (next > 0) {
        tmr->ch[ch].base = tmr->ch[ch].next;
        tmr->ch[ch].next += next;
        printf("%s %ld\n", __func__, next);
        timer_mod(tmr->ch[ch].timer, tmr->ch[ch].next);
    } else {
        timer_del(tmr->ch[ch].timer);
    }
}

static void sent_irq(struct tmr8_ch *ch, enum timer_event evt)
{
    if (is_irq_enabled(ch->tcr, evt)) {
        qemu_irq_pulse(ch->irq[evt - 1]);
    }
}

static void event_countup(struct tmr8_ch *ch)
{
    enum timer_event evt;
    int cor;

    ch->cnt++;
    for (evt = EVT_CMIA; evt < EVT_WOVI; evt++) {
        cor = event_cor(ch, evt) + 1;
        if (ch->cnt == cor) {
            if (is_clr_count(ch->tcr, evt)) {
                ch->cnt = 0;
            }
            sent_irq(ch, evt);
        }
    }
}

static void timer_event(void *opaque)
{
    struct tmr8_ch *ch = opaque;
    RenesasTMR8State *tmr = ch->tmrp;

    switch (ch->event) {
    case EVT_CMIA:
        if (ch->id == 0 && tmr->ch[1].clk == CLK_EVT) {
            /* CH1 event count */
            event_countup(&tmr->ch[1]);
        }
        /* Falls through. */
    case EVT_CMIB:
        if (FIELD_EX8(ch->tcr, TCR, CCLR) == ch->event) {
            ch->cnt = 0;
        } else {
        /* update current value */
            ch->cnt = ch->cor[ch->event] + 1;
        }
        sent_irq(ch, ch->event);
        break;
    case EVT_OVI:
        ch->cnt = 0;
        sent_irq(ch, EVT_OVI);
        if (ch->id == 1 && tmr->ch[0].clk == CLK_EVT) {
            /* CH0 event count */
            event_countup(&tmr->ch[0]);
        }
        break;
    case EVT_WOVI:
        tmr->ch[0].cnt = tmr->ch[1].cnt = 0;
        sent_irq(ch, EVT_OVI);
        break;
    default:
        g_assert_not_reached();
    }
    set_next_event(tmr, ch->id);
}

static uint16_t read_tcnt(RenesasTMR8State *tmr, unsigned int size, int ch)
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
        if (is_word_mode(tmr)) {
            /* 16bit count mode */
            delta = now - tmr->ch[1].base;
            delta /= tmr->ch[1].clk;
            return catreg(tmr->ch[0].cnt, tmr->ch[1].cnt) + delta;
        } else {
            for (i = 0; i < TMR_CH; i++) {
                if (tmr->ch[i].clk > 0) {
                    delta = now - tmr->ch[i].base;
                    delta /= tmr->ch[i].clk;
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

static void tmr_pck_update(void *opaque, ClockEvent evt)
{
    RenesasTMR8State *tmr = RENESAS_TMR8(opaque);
    int i;
    uint16_t tcnt = read_tcnt(tmr, 2, 0);

    tmr->ch[0].cnt = extract16(tcnt, 8, 8);
    tmr->ch[1].cnt = extract16(tcnt, 0, 8);

    tmr->input_freq = clock_get_hz(tmr->pck);
    for (i = 0; i < TMR_CH; i++) {
        if (clock_is_enabled(tmr->pck)) {
            update_clk(tmr, i);
            set_next_event(tmr, i);
        } else {
            if (tmr->ch[i].timer) {
                timer_del(tmr->ch[i].timer);
            }
        }
    }
}

static int validate_access(hwaddr addr, unsigned int size)
{
    /* Byte access always OK */
    if (size == 1) {
        return 1;
    }
    /* word access allowed TCNT / TCOR / TCCR */
    return ((addr & 1) == 0 && addr >= A_TCORA);
}

static uint64_t tmr8_read(void *opaque, hwaddr addr, unsigned int size)
{
    RenesasTMR8State *tmr = RENESAS_TMR8(opaque);
    int ch = addr & 1;
    int cor;

    if (!validate_access(addr, size)) {
        qemu_log_mask(LOG_GUEST_ERROR, "renesas_tmr8: Invalid read size 0x%"
                      HWADDR_PRIX "\n", addr);
        return UINT64_MAX;
    }
    if (!clock_is_enabled(tmr->pck)) {
        qemu_log_mask(LOG_GUEST_ERROR, "renesas_tmr8: Unit %d is stopped.\n",
                      tmr->unit);
        return UINT64_MAX;
    }
    switch (addr & ~1) {
    case A_TCR:
        return tmr->ch[ch].tcr;
    case A_TCSR:
        return tmr->ch[ch].tcsr;
    case A_TCORA:
    case A_TCORB:
        cor = extract32(addr, 1, 1);
        if (size == 1) {
            /* 8bit read - single register */
            return tmr->ch[ch].cor[cor];
        } else {
            /* 16bit read - high byte ch0 reg, low byte ch1 reg */
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
        qemu_log_mask(LOG_UNIMP, "renesas_tmr8: Register 0x%" HWADDR_PRIX
                      " not implemented\n", addr);
        break;
    }
    return UINT64_MAX;
}

static void tmr8_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    RenesasTMR8State *tmr = RENESAS_TMR8(opaque);
    int ch = addr & 1;
    int cor;
    int64_t now;

    if (!validate_access(addr, size)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "renesas_tmr: Invalid write size 0x%" HWADDR_PRIX
                      "\n", addr);
        return;
    }
    if (!clock_is_enabled(tmr->pck)) {
        qemu_log_mask(LOG_GUEST_ERROR, "renesas_tmr8: Unit %d is stopped.\n",
                      tmr->unit);
        return;
    }
    switch (addr & ~1) {
    case A_TCR:
        tmr->ch[ch].tcr = val;
        break;
    case A_TCSR:
        if (ch == 1) {
            /* CH1 ADTR always 1 */
            val = FIELD_DP8(val, TCSR, ADTE, 1);
        }
        tmr->ch[ch].tcsr = val;
        break;
    case A_TCORA:
    case A_TCORB:
        cor = extract32(addr, 1, 1);
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
    if (size == 1) {
        set_next_event(tmr, ch);
    } else {
        set_next_event(tmr, 0);
        set_next_event(tmr, 1);
    }
}

static const MemoryRegionOps tmr_ops = {
    .write = tmr8_write,
    .read  = tmr8_read,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 2,
    },
};

static void tmr8_realize(DeviceState *dev, Error **errp)
{
    RenesasTMR8State *tmr = RENESAS_TMR8(dev);
    int i;

    for (i = 0; i < TMR_CH; i++) {
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

static void tmr8_init(Object *obj)
{
    RenesasTMR8State *tmr = RENESAS_TMR8(obj);
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    int i;

    memory_region_init_io(&tmr->memory, obj, &tmr_ops,
                          tmr, "renesas-tmr8", 0x10);
    sysbus_init_mmio(d, &tmr->memory);

    for (i = 0; i < TMR_CH; i++) {
        sysbus_init_irq(d, &tmr->ch[i].irq[IRQ_CMIA]);
        sysbus_init_irq(d, &tmr->ch[i].irq[IRQ_CMIB]);
        sysbus_init_irq(d, &tmr->ch[i].irq[IRQ_OVI]);
    }
    tmr->pck = qdev_init_clock_in(DEVICE(d), "pck",
                                  tmr_pck_update, tmr, ClockUpdate);
}

static const VMStateDescription vmstate_rtmr = {
    .name = "renesas-8tmr",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static Property tmr8_properties[] = {
    DEFINE_PROP_UINT32("unit", RenesasTMR8State, unit, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void tmr8_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_rtmr;
    dc->realize = tmr8_realize;
    device_class_set_props(dc, tmr8_properties);
}

static const TypeInfo tmr8_info[] = {
    {
        .name       = TYPE_RENESAS_TMR8,
        .parent     = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(RenesasTMR8State),
        .instance_init = tmr8_init,
        .class_init = tmr8_class_init,
    }
};

DEFINE_TYPES(tmr8_info)
