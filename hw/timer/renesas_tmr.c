/*
 * Renesas 8bit timer
 *
 * Copyright (c) 2019 Yoshinori Sato
 *
 * This code is licensed under the GPL version 2 or later.
 *
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/timer.h"
#include "cpu.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/timer/renesas_tmr.h"
#include "qemu/error-report.h"

#define freq_to_ns(freq) (1000000000LL / freq)
static const int clkdiv[] = {0, 1, 2, 8, 32, 64, 1024, 8192};

static void update_events(RTMRState *tmr, int ch)
{
    uint16_t diff[3];
    uint16_t tcnt, tcora, tcorb;
    int i, min, event;

    if (tmr->tccr[ch] == 0) {
        return ;
    }
    if ((tmr->tccr[ch] & 0x08) == 0) {
        error_report("rtmr: unsupported count mode %02x", tmr->tccr[ch]);
        return ;
    }
    if ((tmr->tccr[0] & 0x18) == 0x18) {
        if (ch == 1) {
            tmr->next[ch] = none;
            return ;
        }
        tcnt = (tmr->tcnt[0] << 8) + tmr->tcnt[1];
        tcora = (tmr->tcora[0] << 8) | tmr->tcora[1];
        tcorb = (tmr->tcorb[0] << 8) | tmr->tcorb[1];
        diff[0] = tcora - tcnt;
        diff[1] = tcorb - tcnt;
        diff[2] = 0x10000 - tcnt;
    } else {
        diff[0] = tmr->tcora[ch] - tmr->tcnt[ch];
        diff[1] = tmr->tcorb[ch] - tmr->tcnt[ch];
        diff[2] = 0x100 - tmr->tcnt[ch];
    }
    for (event = 0, min = diff[0], i = 1; i < 3; i++) {
        if (min > diff[i]) {
            event = i;
            min = diff[i];
        }
    }
    tmr->next[ch] = event + 1;
    timer_mod(tmr->timer[ch],
              diff[event] * freq_to_ns(tmr->input_freq) *
              clkdiv[tmr->tccr[ch] & 7]);
}

#define UPDATE_TIME(tmr, ch, upd, delta)                                \
    do {                                                                \
        tmr->div_round[ch] += delta;                                    \
        if (clkdiv[tmr->tccr[ch] & 0x07] > 0) {                         \
            upd = tmr->div_round[ch] / clkdiv[tmr->tccr[ch] & 0x07];    \
            tmr->div_round[ch] %= clkdiv[tmr->tccr[ch] & 0x07];         \
        } else                                                          \
            upd = 0;                                                    \
    } while (0)

static uint64_t read_tcnt(RTMRState *tmr, unsigned size, int ch)
{
    int64_t delta, now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    int upd, ovf = 0;
    uint16_t tcnt[2];

    delta = (now - tmr->tick) / freq_to_ns(tmr->input_freq);
    if (delta > 0) {
        tmr->tick = now;

        if ((tmr->tccr[1] & 0x18) == 0x08) {
            UPDATE_TIME(tmr, 1, upd, delta);
            if (upd >= 0x100) {
                ovf = upd >> 8;
                upd -= ovf;
            }
            tcnt[1] = tmr->tcnt[1] + upd;
        }
        switch (tmr->tccr[0] & 0x18) {
        case 0x08:
            UPDATE_TIME(tmr, 0, upd, delta);
            tcnt[0] = tmr->tcnt[0] + upd;
            break;
        case 0x18:
            if (ovf > 0) {
                tcnt[0] = tmr->tcnt[0] + ovf;
            }
            break;
        }
    } else {
        tcnt[0] = tmr->tcnt[0];
        tcnt[1] = tmr->tcnt[1];
    }
    if (size == 1) {
        return tcnt[ch];
    } else {
        return (tmr->tcnt[0] << 8) | (tmr->tcnt[1] & 0xff);
    }
}

static uint64_t tmr_read(void *opaque, hwaddr addr, unsigned size)
{
    hwaddr offset = addr & 0x1f;
    RTMRState *tmr = opaque;
    int ch = offset & 1;
    int error = 0;

    if (size == 1) {
        switch (offset & 0x0e) {
        case 0x00:
            return tmr->tcr[ch] & 0xf8;
        case 0x02:
            return tmr->tcsr[ch] & 0xf8;
        case 0x04:
            return tmr->tcora[ch];
        case 0x06:
            return tmr->tcorb[ch];
        case 0x08:
            return read_tcnt(tmr, size, ch);
        case 0x0a:
            return tmr->tccr[ch];
        default:
            error = 1;
        }
    } else if (ch == 0) {
        switch (offset & 0x0e) {
        case 0x04:
            return tmr->tcora[0] << 8 | tmr->tcora[1];
        case 0x06:
            return tmr->tcorb[0] << 8 | tmr->tcorb[1];;
        case 0x08:
            return read_tcnt(tmr, size, 0) & 0xff;
        case 0x0a:
            return tmr->tccr[0] << 8 | tmr->tccr[1];
        default:
            error = 1;
        }
    } else {
        error = 1;
    }
    if (error) {
        error_report("rtmr: unsupported read request to %08lx", addr);
    }
    return 0xffffffffffffffffULL;
}

static void tmr_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    hwaddr offset = addr & 0x1f;
    RTMRState *tmr = opaque;
    int ch = offset & 1;
    int error = 0;

    if (size == 1) {
        switch (offset & 0x0e) {
        case 0x00:
            tmr->tcr[ch] = val;
            break;
        case 0x02:
            tmr->tcsr[ch] = val;
            break;
        case 0x04:
            tmr->tcora[ch] = val;
            update_events(tmr, ch);
            break;
        case 0x06:
            tmr->tcora[ch] = val;
            update_events(tmr, ch);
            break;
        case 0x08:
            tmr->tcnt[ch] = val;
            update_events(tmr, ch);
            break;
        case 0x0a:
            tmr->tccr[ch] = val;
            update_events(tmr, ch);
            break;
        default:
            error = 1;
        }
    } else if (ch == 0) {
        switch (offset & 0x0e) {
        case 0x04:
            tmr->tcora[0] = (val >> 8) & 0xff;
            tmr->tcora[1] = val & 0xff;
            update_events(tmr, 0);
            update_events(tmr, 1);
        case 0x06:
            tmr->tcorb[0] = (val >> 8) & 0xff;
            tmr->tcorb[1] = val & 0xff;
            update_events(tmr, 0);
            update_events(tmr, 1);
            break;
        case 0x08:
            tmr->tcnt[0] = (val >> 8) & 0xff;
            tmr->tcnt[1] = val & 0xff;
            update_events(tmr, 0);
            update_events(tmr, 1);
            break;
        case 0x0a:
            tmr->tccr[0] = (val >> 8) & 0xff;
            tmr->tccr[1] = val & 0xff;
            update_events(tmr, 0);
            update_events(tmr, 1);
            break;
        default:
            error = 1;
        }
    } else {
        error = 1;
    }
    if (error) {
        error_report("rtmr: unsupported write request to %08lx", addr);
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

static void timer_events(RTMRState *tmr, int ch)
{
    tmr->tcnt[ch] = read_tcnt(tmr, 1, ch);
    if ((tmr->tccr[0] & 0x18) != 0x18) {
        switch (tmr->next[ch]) {
        case none:
            break;
        case cmia:
            if (tmr->tcnt[ch] >= tmr->tcora[ch]) {
                if ((tmr->tcr[ch] & 0x18) == 0x08) {
                    tmr->tcnt[ch] = 0;
                }
                if ((tmr->tcr[ch] & 0x40)) {
                    qemu_irq_pulse(tmr->cmia[ch]);
                }
                if (ch == 0 && (tmr->tccr[1] & 0x18) == 0x18) {
                    tmr->tcnt[1]++;
                    timer_events(tmr, 1);
                }
            }
            break;
        case cmib:
            if (tmr->tcnt[ch] >= tmr->tcorb[ch]) {
                if ((tmr->tcr[ch] & 0x18) == 0x10) {
                    tmr->tcnt[ch] = 0;
                }
                if ((tmr->tcr[ch] & 0x80)) {
                    qemu_irq_pulse(tmr->cmib[ch]);
                }
            }
            break;
        case ovi:
            if ((tmr->tcnt[ch] >= 0x100) &&
                (tmr->tcr[ch] & 0x20)) {
                qemu_irq_pulse(tmr->ovi[ch]);
            }
            break;
        }
        tmr->tcnt[ch] &= 0xff;
    } else {
        uint32_t tcnt, tcora, tcorb;
        if (ch == 1) {
            return ;
        }
        tcnt = (tmr->tcnt[0] << 8) + tmr->tcnt[1];
        tcora = (tmr->tcora[0] << 8) | tmr->tcora[1];
        tcorb = (tmr->tcorb[0] << 8) | tmr->tcorb[1];
        switch (tmr->next[ch]) {
        case none:
            break;
        case cmia:
            if (tcnt >= tcora) {
                if ((tmr->tcr[ch] & 0x18) == 0x08) {
                    tcnt = 0;
                }
                if ((tmr->tcr[ch] & 0x40)) {
                    qemu_irq_pulse(tmr->cmia[ch]);
                }
            }
            break;
        case cmib:
            if (tcnt >= tcorb) {
                if ((tmr->tcr[ch] & 0x18) == 0x10) {
                    tcnt = 0;
                }
                if ((tmr->tcr[ch] & 0x80)) {
                    qemu_irq_pulse(tmr->cmib[ch]);
                }
            }
            break;
        case ovi:
            if ((tcnt >= 0x10000) &&
                (tmr->tcr[ch] & 0x20)) {
                qemu_irq_pulse(tmr->ovi[ch]);
            }
            break;
        }
        tmr->tcnt[0] = (tcnt >> 8) & 0xff;
        tmr->tcnt[1] = tcnt & 0xff;
    }
    update_events(tmr, ch);
}

static void timer_event0(void *opaque)
{
    RTMRState *tmr = opaque;

    timer_events(tmr, 0);
}

static void timer_event1(void *opaque)
{
    RTMRState *tmr = opaque;

    timer_events(tmr, 1);
}

static void rtmr_reset(DeviceState *dev)
{
    RTMRState *tmr = RTMR(dev);
    tmr->tcora[0] = tmr->tcora[1] = 0xff;
    tmr->tcorb[0] = tmr->tcorb[1] = 0xff;
    tmr->tcsr[0] = 0x00;
    tmr->tcsr[1] = 0x10;
    tmr->tick = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
}

static void rtmr_init(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    RTMRState *tmr = RTMR(obj);
    int i;

    memory_region_init_io(&tmr->memory, OBJECT(tmr), &tmr_ops,
                          tmr, "rx-tmr", 0x10);
    sysbus_init_mmio(d, &tmr->memory);

    for (i = 0; i < 2; i++) {
        sysbus_init_irq(d, &tmr->cmia[i]);
        sysbus_init_irq(d, &tmr->cmib[i]);
        sysbus_init_irq(d, &tmr->ovi[i]);
    }
    tmr->timer[0] = timer_new_ns(QEMU_CLOCK_VIRTUAL, timer_event0, tmr);
    tmr->timer[1] = timer_new_ns(QEMU_CLOCK_VIRTUAL, timer_event1, tmr);
}

static const VMStateDescription vmstate_rtmr = {
    .name = "rx-cmt",
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

    dc->props = rtmr_properties;
    dc->vmsd = &vmstate_rtmr;
    dc->reset = rtmr_reset;
}

static const TypeInfo rtmr_info = {
    .name       = TYPE_RENESAS_TMR,
    .parent     = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RTMRState),
    .instance_init = rtmr_init,
    .class_init = rtmr_class_init,
};

static void rtmr_register_types(void)
{
    type_register_static(&rtmr_info);
}

type_init(rtmr_register_types)
