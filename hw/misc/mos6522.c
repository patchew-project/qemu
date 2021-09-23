/*
 * QEMU MOS6522 VIA emulation
 *
 * Copyright (c) 2004-2007 Fabrice Bellard
 * Copyright (c) 2007 Jocelyn Mayer
 * Copyright (c) 2018 Mark Cave-Ayland
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/input/adb.h"
#include "hw/irq.h"
#include "hw/misc/mos6522.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/timer.h"
#include "qemu/cutils.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"

/* XXX: implement all timer modes */

static void mos6522_timer1_update(MOS6522State *s, MOS6522Timer *ti,
                                  int64_t now);
static void mos6522_timer2_update(MOS6522State *s, MOS6522Timer *ti,
                                  int64_t now);

static void mos6522_update_irq(MOS6522State *s)
{
    if (s->ifr & s->ier) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}

static void mos6522_timer_raise_irq(MOS6522State *s, MOS6522Timer *ti)
{
    if (ti->state == irq) {
        return;
    }
    ti->state = irq;
    if (ti->index == 0) {
        s->ifr |= T1_INT;
    } else {
        s->ifr |= T2_INT;
    }
    mos6522_update_irq(s);
}

static unsigned int get_counter(MOS6522State *s, MOS6522Timer *ti, int64_t now)
{
    int64_t d;
    unsigned int counter;
    bool reload;

    /*
     * Timer 1 counts down from the latch value to -1 (period of latch + 2),
     * then raises its interrupt and reloads.
     * Timer 2 counts down from the latch value to -1, then raises its
     * interrupt and continues to -2 and so on without any further interrupts.
     *
     * TODO
     * This implementation deviates from hardware behaviour because it omits
     * the phase two clock. On a real 6522, the counter is decremented on a
     * falling edge and the interrupt is asserted on a rising edge. Register
     * accesses are synchronous with this clock. That means successive
     * accesses to T1CL or T2CL can't yield the same value because
     * they can't happen in the same clock cycle.
     */
    d = muldiv64(now - ti->load_time, ti->frequency, NANOSECONDS_PER_SECOND);

    reload = (d >= ti->counter_value + 2);

    if (ti->index == 0 && reload) {
        int64_t more_reloads;

        d -= ti->counter_value + 2;
        more_reloads = d / (ti->latch + 2);
        d -= more_reloads * (ti->latch + 2);
        ti->load_time += muldiv64(ti->counter_value + 2 +
                                  more_reloads * (ti->latch + 2),
                                  NANOSECONDS_PER_SECOND, ti->frequency);
        ti->counter_value = ti->latch;
    }

    counter = ti->counter_value - d;

    if (reload) {
        mos6522_timer_raise_irq(s, ti);
    }

    return counter & 0xffff;
}

static void set_counter(MOS6522State *s, MOS6522Timer *ti, unsigned int val,
                        int64_t now)
{
    trace_mos6522_set_counter(1 + ti->index, val);
    ti->load_time = now;
    ti->counter_value = val;
    ti->state = decrement;
    if (ti->index == 0) {
        mos6522_timer1_update(s, ti, ti->load_time);
    } else {
        mos6522_timer2_update(s, ti, ti->load_time);
    }
}

static int64_t get_next_irq_time(MOS6522State *s, MOS6522Timer *ti,
                                 int64_t now)
{
    int64_t next_time;

    if (ti->frequency == 0) {
        return INT64_MAX;
    }

    next_time = ti->load_time + muldiv64(ti->counter_value + 2,
                                         NANOSECONDS_PER_SECOND, ti->frequency);
    trace_mos6522_get_next_irq_time(ti->latch, ti->load_time, next_time);
    return next_time;
}

static void mos6522_timer1_update(MOS6522State *s, MOS6522Timer *ti,
                                  int64_t now)
{
    if (!ti->timer) {
        return;
    }
    get_counter(s, ti, now);
    ti->next_irq_time = get_next_irq_time(s, ti, now);
    if ((s->ier & T1_INT) == 0 || (s->acr & T1MODE) != T1MODE_CONT) {
        timer_del(ti->timer);
    } else {
        timer_mod(ti->timer, ti->next_irq_time);
    }
}

static void mos6522_timer2_update(MOS6522State *s, MOS6522Timer *ti,
                                  int64_t now)
{
    if (!ti->timer) {
        return;
    }
    get_counter(s, ti, now);
    ti->next_irq_time = get_next_irq_time(s, ti, now);
    if ((s->ier & T2_INT) == 0) {
        timer_del(ti->timer);
    } else {
        timer_mod(ti->timer, ti->next_irq_time);
    }
}

static void mos6522_timer1_expired(void *opaque)
{
    MOS6522State *s = opaque;
    MOS6522Timer *ti = &s->timers[0];
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    mos6522_timer1_update(s, ti, now);
}

static void mos6522_timer2_expired(void *opaque)
{
    MOS6522State *s = opaque;
    MOS6522Timer *ti = &s->timers[1];
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    mos6522_timer2_update(s, ti, now);
}

static void mos6522_set_sr_int(MOS6522State *s)
{
    trace_mos6522_set_sr_int();
    s->ifr |= SR_INT;
    mos6522_update_irq(s);
}

static void mos6522_portA_write(MOS6522State *s)
{
    qemu_log_mask(LOG_UNIMP, "portA_write unimplemented\n");
}

static void mos6522_portB_write(MOS6522State *s)
{
    qemu_log_mask(LOG_UNIMP, "portB_write unimplemented\n");
}

uint64_t mos6522_read(void *opaque, hwaddr addr, unsigned size)
{
    MOS6522State *s = opaque;
    uint32_t val;
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    switch (addr) {
    case VIA_REG_B:
        val = s->b;
        break;
    case VIA_REG_A:
       qemu_log_mask(LOG_UNIMP, "Read access to register A with handshake");
       /* fall through */
    case VIA_REG_ANH:
        val = s->a;
        break;
    case VIA_REG_DIRB:
        val = s->dirb;
        break;
    case VIA_REG_DIRA:
        val = s->dira;
        break;
    case VIA_REG_T1CL:
        val = get_counter(s, &s->timers[0], now) & 0xff;
        if (s->timers[0].state >= irq) {
            s->timers[0].state = irq_cleared;
            s->ifr &= ~T1_INT;
            mos6522_update_irq(s);
        }
        break;
    case VIA_REG_T1CH:
        val = get_counter(s, &s->timers[0], now) >> 8;
        break;
    case VIA_REG_T1LL:
        val = s->timers[0].latch & 0xff;
        break;
    case VIA_REG_T1LH:
        val = (s->timers[0].latch >> 8) & 0xff;
        break;
    case VIA_REG_T2CL:
        val = get_counter(s, &s->timers[1], now) & 0xff;
        if (s->timers[1].state >= irq) {
            s->timers[1].state = irq_cleared;
            s->ifr &= ~T2_INT;
            mos6522_update_irq(s);
        }
        break;
    case VIA_REG_T2CH:
        val = get_counter(s, &s->timers[1], now) >> 8;
        break;
    case VIA_REG_SR:
        val = s->sr;
        s->ifr &= ~SR_INT;
        mos6522_update_irq(s);
        break;
    case VIA_REG_ACR:
        val = s->acr;
        break;
    case VIA_REG_PCR:
        val = s->pcr;
        break;
    case VIA_REG_IFR:
        val = s->ifr;
        if (s->ifr & s->ier) {
            val |= 0x80;
        }
        break;
    case VIA_REG_IER:
        val = s->ier | 0x80;
        break;
    default:
        g_assert_not_reached();
    }

    if (addr != VIA_REG_IFR || val != 0) {
        trace_mos6522_read(addr, val);
    }

    return val;
}

void mos6522_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    MOS6522State *s = opaque;
    MOS6522DeviceClass *mdc = MOS6522_GET_CLASS(s);
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    trace_mos6522_write(addr, val);

    switch (addr) {
    case VIA_REG_B:
        s->b = (s->b & ~s->dirb) | (val & s->dirb);
        mdc->portB_write(s);
        break;
    case VIA_REG_A:
       qemu_log_mask(LOG_UNIMP, "Write access to register A with handshake");
       /* fall through */
    case VIA_REG_ANH:
        s->a = (s->a & ~s->dira) | (val & s->dira);
        mdc->portA_write(s);
        break;
    case VIA_REG_DIRB:
        s->dirb = val;
        break;
    case VIA_REG_DIRA:
        s->dira = val;
        break;
    case VIA_REG_T1CL:
        get_counter(s, &s->timers[0], now);
        s->timers[0].latch = (s->timers[0].latch & 0xff00) | val;
        break;
    case VIA_REG_T1CH:
        s->timers[0].latch = (s->timers[0].latch & 0xff) | (val << 8);
        s->ifr &= ~T1_INT;
        set_counter(s, &s->timers[0], s->timers[0].latch, now);
        break;
    case VIA_REG_T1LL:
        get_counter(s, &s->timers[0], now);
        s->timers[0].latch = (s->timers[0].latch & 0xff00) | val;
        break;
    case VIA_REG_T1LH:
        get_counter(s, &s->timers[0], now);
        s->timers[0].latch = (s->timers[0].latch & 0xff) | (val << 8);
        s->ifr &= ~T1_INT;
        break;
    case VIA_REG_T2CL:
        get_counter(s, &s->timers[1], now);
        s->timers[1].latch = (s->timers[1].latch & 0xff00) | val;
        break;
    case VIA_REG_T2CH:
        s->timers[1].latch = (s->timers[1].latch & 0xff) | (val << 8);
        s->ifr &= ~T2_INT;
        set_counter(s, &s->timers[1], s->timers[1].latch, now);
        break;
    case VIA_REG_SR:
        s->sr = val;
        break;
    case VIA_REG_ACR:
        s->acr = val;
        mos6522_timer1_update(s, &s->timers[0], now);
        break;
    case VIA_REG_PCR:
        s->pcr = val;
        break;
    case VIA_REG_IFR:
        if (val & T1_INT) {
            get_counter(s, &s->timers[0], now);
            if ((s->ifr & T1_INT) && s->timers[0].state == irq) {
                s->timers[0].state = irq_cleared;
            }
        }
        if (val & T2_INT) {
            get_counter(s, &s->timers[1], now);
            if ((s->ifr & T2_INT) && s->timers[1].state == irq) {
                s->timers[1].state = irq_cleared;
            }
        }
        s->ifr &= ~val;
        mos6522_update_irq(s);
        break;
    case VIA_REG_IER:
        if (val & IER_SET) {
            /* set bits */
            s->ier |= val & 0x7f;
        } else {
            /* reset bits */
            s->ier &= ~val;
        }
        mos6522_update_irq(s);
        /* if IER is modified starts needed timers */
        mos6522_timer1_update(s, &s->timers[0], now);
        mos6522_timer2_update(s, &s->timers[1], now);
        break;
    default:
        g_assert_not_reached();
    }
}

static const MemoryRegionOps mos6522_ops = {
    .read = mos6522_read,
    .write = mos6522_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static const VMStateDescription vmstate_mos6522_timer = {
    .name = "mos6522_timer",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(latch, MOS6522Timer),
        VMSTATE_UINT16(counter_value, MOS6522Timer),
        VMSTATE_INT64(load_time, MOS6522Timer),
        VMSTATE_INT64(next_irq_time, MOS6522Timer),
        VMSTATE_TIMER_PTR(timer, MOS6522Timer),
        VMSTATE_END_OF_LIST()
    }
};

const VMStateDescription vmstate_mos6522 = {
    .name = "mos6522",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(a, MOS6522State),
        VMSTATE_UINT8(b, MOS6522State),
        VMSTATE_UINT8(dira, MOS6522State),
        VMSTATE_UINT8(dirb, MOS6522State),
        VMSTATE_UINT8(sr, MOS6522State),
        VMSTATE_UINT8(acr, MOS6522State),
        VMSTATE_UINT8(pcr, MOS6522State),
        VMSTATE_UINT8(ifr, MOS6522State),
        VMSTATE_UINT8(ier, MOS6522State),
        VMSTATE_STRUCT_ARRAY(timers, MOS6522State, 2, 0,
                             vmstate_mos6522_timer, MOS6522Timer),
        VMSTATE_END_OF_LIST()
    }
};

static void mos6522_reset(DeviceState *dev)
{
    MOS6522State *s = MOS6522(dev);

    s->b = 0;
    s->a = 0;
    s->dirb = 0xff;
    s->dira = 0;
    s->sr = 0;
    s->acr = 0;
    s->pcr = 0;
    s->ifr = 0;
    s->ier = 0;
    /* s->ier = T1_INT | SR_INT; */

    s->timers[0].frequency = s->frequency;
    s->timers[0].latch = 0xffff;
    set_counter(s, &s->timers[0], 0xffff,
                qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
    timer_del(s->timers[0].timer);

    s->timers[1].frequency = s->frequency;
    s->timers[1].latch = 0xffff;
    timer_del(s->timers[1].timer);
}

static void mos6522_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    MOS6522State *s = MOS6522(obj);
    int i;

    memory_region_init_io(&s->mem, obj, &mos6522_ops, s, "mos6522", 0x10);
    sysbus_init_mmio(sbd, &s->mem);
    sysbus_init_irq(sbd, &s->irq);

    for (i = 0; i < ARRAY_SIZE(s->timers); i++) {
        s->timers[i].index = i;
    }

    s->timers[0].timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                      mos6522_timer1_expired, s);
    s->timers[1].timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                      mos6522_timer2_expired, s);
}

static void mos6522_finalize(Object *obj)
{
    MOS6522State *s = MOS6522(obj);

    timer_free(s->timers[0].timer);
    timer_free(s->timers[1].timer);
}

static Property mos6522_properties[] = {
    DEFINE_PROP_UINT64("frequency", MOS6522State, frequency, 0),
    DEFINE_PROP_END_OF_LIST()
};

static void mos6522_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    MOS6522DeviceClass *mdc = MOS6522_CLASS(oc);

    dc->reset = mos6522_reset;
    dc->vmsd = &vmstate_mos6522;
    device_class_set_props(dc, mos6522_properties);
    mdc->parent_reset = dc->reset;
    mdc->set_sr_int = mos6522_set_sr_int;
    mdc->portB_write = mos6522_portB_write;
    mdc->portA_write = mos6522_portA_write;
    mdc->update_irq = mos6522_update_irq;
}

static const TypeInfo mos6522_type_info = {
    .name = TYPE_MOS6522,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MOS6522State),
    .instance_init = mos6522_init,
    .instance_finalize = mos6522_finalize,
    .abstract = true,
    .class_size = sizeof(MOS6522DeviceClass),
    .class_init = mos6522_class_init,
};

static void mos6522_register_types(void)
{
    type_register_static(&mos6522_type_info);
}

type_init(mos6522_register_types)
