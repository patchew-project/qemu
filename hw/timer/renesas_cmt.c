/*
 * Renesas 16bit Compare-match timer
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
#include "qemu/timer.h"
#include "cpu.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/timer/renesas_cmt.h"
#include "qemu/error-report.h"

#define freq_to_ns(freq) (1000000000LL / freq)
static const int clkdiv[] = {8, 32, 128, 512};

static void update_events(RCMTState *cmt, int ch)
{
    uint16_t diff;

    if ((cmt->cmstr & (1 << ch)) != 0) {
        diff = cmt->cmcor[ch] - cmt->cmcnt[ch];
        timer_mod(cmt->timer[ch],
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
              diff * freq_to_ns(cmt->input_freq) *
              clkdiv[cmt->cmcr[ch] & 3]);
    }
}

static uint64_t read_cmcnt(RCMTState *cmt, int ch)
{
    int64_t delta, now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    if (cmt->cmstr & (1 << ch)) {
        delta = (now - cmt->tick[ch]) / freq_to_ns(cmt->input_freq);
        delta /= clkdiv[cmt->cmcr[ch] & 0x03];
        return cmt->cmcnt[ch] + delta;
    } else {
        return cmt->cmcnt[ch];
    }
}

static uint64_t cmt_read(void *opaque, hwaddr addr, unsigned size)
{
    hwaddr offset = addr & 0x0f;
    RCMTState *cmt = opaque;
    int ch = offset / 0x08;
    int error = 1;

    if (offset == 0) {
        return cmt->cmstr;
        error = 0;
    } else {
        offset &= 0x07;
        if (ch == 0) {
            offset -= 0x02;
        }
        error = 0;
        switch (offset) {
        case 0:
            return cmt->cmcr[ch];
        case 2:
            return read_cmcnt(cmt, ch);
        case 4:
            return cmt->cmcor[ch];
        default:
            error = 1;
        }
    }
    if (error) {
        error_report("rcmt: unsupported read request to %08lx", addr);
    }
    return 0xffffffffffffffffUL;
}

static void start_stop(RCMTState *cmt, int ch, int st)
{
    if (st) {
        update_events(cmt, ch);
    } else {
        timer_del(cmt->timer[ch]);
    }
}

static void cmt_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    hwaddr offset = addr & 0x0f;
    RCMTState *cmt = opaque;
    int ch = offset / 0x08;
    int error = 1;

    if (offset == 0) {
        cmt->cmstr = val;
        start_stop(cmt, 0, cmt->cmstr & 1);
        start_stop(cmt, 1, (cmt->cmstr >> 1) & 1);
        error = 0;
    } else {
        offset &= 0x07;
        if (ch == 0) {
            offset -= 0x02;
        }
        error = 0;
        switch (offset) {
        case 0:
            cmt->cmcr[ch] = val;
            break;
        case 2:
            cmt->cmcnt[ch] = val;
            break;
        case 4:
            cmt->cmcor[ch] = val;
            break;
        default:
            error = 1;
        }
        if (error == 0 && cmt->cmstr & (1 << ch)) {
            update_events(cmt, ch);
        }
    }
    if (error) {
        error_report("rcmt: unsupported write request to %08lx", addr);
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

static void timer_events(RCMTState *cmt, int ch)
{
    cmt->cmcnt[ch] = 0;
    cmt->tick[ch] = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    update_events(cmt, ch);
    if (cmt->cmcr[ch] & 0x40) {
        qemu_irq_pulse(cmt->cmi[ch]);
    }
}

static void timer_event0(void *opaque)
{
    RCMTState *cmt = opaque;

    timer_events(cmt, 0);
}

static void timer_event1(void *opaque)
{
    RCMTState *cmt = opaque;

    timer_events(cmt, 1);
}

static void rcmt_reset(DeviceState *dev)
{
    RCMTState *cmt = RCMT(dev);
    cmt->cmstr = 0;
    cmt->cmcr[0] = cmt->cmcr[1] = 0;
    cmt->cmcnt[0] = cmt->cmcnt[1] = 0;
    cmt->cmcor[0] = cmt->cmcor[1] = 0xffff;
}

static void rcmt_init(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    RCMTState *cmt = RCMT(obj);
    int i;

    memory_region_init_io(&cmt->memory, OBJECT(cmt), &cmt_ops,
                          cmt, "renesas-cmt", 0x10);
    sysbus_init_mmio(d, &cmt->memory);

    for (i = 0; i < 2; i++) {
        sysbus_init_irq(d, &cmt->cmi[i]);
    }
    cmt->timer[0] = timer_new_ns(QEMU_CLOCK_VIRTUAL, timer_event0, cmt);
    cmt->timer[1] = timer_new_ns(QEMU_CLOCK_VIRTUAL, timer_event1, cmt);
}

static const VMStateDescription vmstate_rcmt = {
    .name = "rx-cmt",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static Property rcmt_properties[] = {
    DEFINE_PROP_UINT64("input-freq", RCMTState, input_freq, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void rcmt_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->props = rcmt_properties;
    dc->vmsd = &vmstate_rcmt;
    dc->reset = rcmt_reset;
}

static const TypeInfo rcmt_info = {
    .name       = TYPE_RENESAS_CMT,
    .parent     = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RCMTState),
    .instance_init = rcmt_init,
    .class_init = rcmt_class_init,
};

static void rcmt_register_types(void)
{
    type_register_static(&rcmt_info);
}

type_init(rcmt_register_types)
