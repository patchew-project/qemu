/*
 * Timer block model of Microsemi SmartFusion2.
 *
 * Copyright (c) 2017 Subbaraya Sundeep <sundeep.lkml@gmail.com>.
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
#include "hw/timer/msf2_timer.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"

#ifndef MSF2_TIMER_ERR_DEBUG
#define MSF2_TIMER_ERR_DEBUG  0
#endif

#define DB_PRINT_L(lvl, fmt, args...) do { \
    if (MSF2_TIMER_ERR_DEBUG >= lvl) { \
        qemu_log("%s: " fmt, __func__, ## args); \
    } \
} while (0);

#define DB_PRINT(fmt, args...) DB_PRINT_L(1, fmt, ## args)

static void timer_update_irq(struct Msf2Timer *st)
{
    bool isr, ier;

    isr = !!(st->regs[R_TIM_RIS] & TIMER_RIS_ACK);
    ier = !!(st->regs[R_TIM_CTRL] & TIMER_CTRL_INTR);

    qemu_set_irq(st->irq, (ier && isr));
}

static void timer_update(struct Msf2Timer *st)
{
    uint64_t count;

    DB_PRINT("timer=%d\n", st->nr);

    if (!(st->regs[R_TIM_CTRL] & TIMER_CTRL_ENBL)) {
        ptimer_stop(st->ptimer);
        return;
    }

    count = st->regs[R_TIM_LOADVAL];
    ptimer_set_limit(st->ptimer, count, 1);
    ptimer_run(st->ptimer, 1);
}

static uint64_t
timer_read(void *opaque, hwaddr addr, unsigned int size)
{
    MSF2TimerState *t = opaque;
    struct Msf2Timer *st;
    uint32_t r = 0;
    unsigned int timer = 0;
    int isr;
    int ier;

    addr >>= 2;
    /*
     * Two independent timers has same base address.
     * Based on addr passed figure out which timer is being used.
     */
    if (addr >= R_TIM1_MAX) {
        timer = 1;
        addr -= R_TIM1_MAX;
    }

    st = &t->timers[timer];

    switch (addr) {
    case R_TIM_VAL:
        r = ptimer_get_count(st->ptimer);
        DB_PRINT("msf2_timer t=%d read counter=%x\n", timer, r);
        break;

    case R_TIM_MIS:
        isr = !!(st->regs[R_TIM_RIS] & TIMER_RIS_ACK);
        ier = !!(st->regs[R_TIM_CTRL] & TIMER_CTRL_INTR);
        r = ier && isr;
        break;

    default:
        if (addr < ARRAY_SIZE(st->regs)) {
            r = st->regs[addr];
        }
        break;
    }
    DB_PRINT("timer=%d %lu=%x\n", timer, addr * 4, r);
    return r;
}

static void
timer_write(void *opaque, hwaddr addr,
            uint64_t val64, unsigned int size)
{
    MSF2TimerState *t = opaque;
    struct Msf2Timer *st;
    unsigned int timer = 0;
    uint32_t value = val64;

    addr >>= 2;
    /*
     * Two independent timers has same base address.
     * Based on addr passed figure out which timer is being used.
     */
    if (addr >= R_TIM1_MAX) {
        timer = 1;
        addr -= R_TIM1_MAX;
    }

    st = &t->timers[timer];

    DB_PRINT("addr=%lu val=%x (timer=%d)\n", addr * 4, value, timer);

    switch (addr) {
    case R_TIM_CTRL:
        st->regs[R_TIM_CTRL] = value;
        timer_update(st);
        break;

    case R_TIM_RIS:
        if (value & TIMER_RIS_ACK) {
            st->regs[R_TIM_RIS] &= ~TIMER_RIS_ACK;
        }
        break;

    case R_TIM_LOADVAL:
        st->regs[R_TIM_LOADVAL] = value;
        if (st->regs[R_TIM_CTRL] & TIMER_CTRL_ENBL) {
            timer_update(st);
        }
        break;

    case R_TIM_BGLOADVAL:
        st->regs[R_TIM_BGLOADVAL] = value;
        st->regs[R_TIM_LOADVAL] = value;
        break;

    case R_TIM_VAL:
    case R_TIM_MIS:
        break;

    case R_TIM_MODE:
        if (value & TIMER_MODE) {
            DB_PRINT("64-bit mode not supported\n");
        }
        break;

    default:
        if (addr < ARRAY_SIZE(st->regs)) {
            st->regs[addr] = value;
        }
        break;
    }
    timer_update_irq(st);
}

static const MemoryRegionOps timer_ops = {
    .read = timer_read,
    .write = timer_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static void timer_hit(void *opaque)
{
    struct Msf2Timer *st = opaque;

    DB_PRINT("%d\n", st->nr);
    st->regs[R_TIM_RIS] |= TIMER_RIS_ACK;

    if (!(st->regs[R_TIM_CTRL] & TIMER_CTRL_ONESHOT)) {
        timer_update(st);
    }
    timer_update_irq(st);
}

static void msf2_timer_init(Object *obj)
{
    MSF2TimerState *t = MSF2_TIMER(obj);
    unsigned int i;

    /* Init all the ptimers.  */
    t->timers = g_malloc0((sizeof t->timers[0]) * NUM_TIMERS);
    for (i = 0; i < NUM_TIMERS; i++) {
        struct Msf2Timer *st = &t->timers[i];

        st->nr = i;
        st->bh = qemu_bh_new(timer_hit, st);
        st->ptimer = ptimer_init(st->bh, PTIMER_POLICY_DEFAULT);
        ptimer_set_freq(st->ptimer, t->freq_hz);
        sysbus_init_irq(SYS_BUS_DEVICE(obj), &st->irq);
    }

    memory_region_init_io(&t->mmio, OBJECT(t), &timer_ops, t, TYPE_MSF2_TIMER,
                          R_TIM_MAX * 4);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &t->mmio);
}

static Property msf2_timer_properties[] = {
    DEFINE_PROP_UINT32("clock-frequency", MSF2TimerState, freq_hz,
                       83 * 1000000),
    DEFINE_PROP_END_OF_LIST(),
};

static void msf2_timer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->props = msf2_timer_properties;
}

static const TypeInfo msf2_timer_info = {
    .name          = TYPE_MSF2_TIMER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MSF2TimerState),
    .instance_init = msf2_timer_init,
    .class_init    = msf2_timer_class_init,
};

static void msf2_timer_register_types(void)
{
    type_register_static(&msf2_timer_info);
}

type_init(msf2_timer_register_types)
