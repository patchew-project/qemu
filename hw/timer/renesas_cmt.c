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
#include "cpu.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/timer/renesas_cmt.h"
#include "qemu/error-report.h"

/*
 *  +0 CMSTR - common control
 *  +2 CMCR  - ch0
 *  +4 CMCNT - ch0
 *  +6 CMCOR - ch0
 *  +8 CMCR  - ch1
 * +10 CMCNT - ch1
 * +12 CMCOR - ch1
 * If we think that the address of CH 0 has an offset of +2,
 * we can treat it with the same address as CH 1, so define it like that.
 */
REG16(CMSTR, 0)
  FIELD(CMSTR, STR0, 0, 1)
  FIELD(CMSTR, STR1, 1, 1)
  FIELD(CMSTR, STR,  0, 2)
/* This addeess is channel offset */
REG16(CMCR, 0)
  FIELD(CMCR, CKS, 0, 2)
  FIELD(CMCR, CMIE, 6, 1)
REG16(CMCNT, 2)
REG16(CMCOR, 4)

static void update_events(struct RCMTChannelState *c)
{
    int64_t next_time;

    next_time = c->clk_per_ns * (c->cmcor - c->cmcnt);
    next_time += qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    timer_mod(c->timer, next_time);
}

static int64_t read_cmcnt(struct RCMTChannelState *c)
{
    int64_t delta = 0;
    int64_t now;

    if (c->start) {
        now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        delta = (now - c->tick) / c->clk_per_ns;
        c->tick = now;
    }
    return c->cmcnt + delta;
}

static uint64_t cmt_read(void *opaque, hwaddr addr, unsigned size)
{
    RCMTState *cmt = opaque;
    int ch = addr / 0x08;
    uint64_t ret;

    if (addr == A_CMSTR) {
        ret = 0;
        ret = FIELD_DP16(ret, CMSTR, STR,
                         FIELD_EX16(cmt->cmstr, CMSTR, STR));
        return ret;
    } else {
        addr &= 0x07;
        if (ch == 0) {
            addr -= 0x02;
        }
        switch (addr) {
        case A_CMCR:
            ret = 0;
            ret = FIELD_DP16(ret, CMCR, CKS,
                             FIELD_EX16(cmt->cmstr, CMCR, CKS));
            ret = FIELD_DP16(ret, CMCR, CMIE,
                             FIELD_EX16(cmt->cmstr, CMCR, CMIE));
            return ret;
        case A_CMCNT:
            return read_cmcnt(&cmt->c[ch]);
        case A_CMCOR:
            return cmt->c[ch].cmcor;
        }
    }
    qemu_log_mask(LOG_UNIMP, "renesas_cmt: Register 0x%"
                  HWADDR_PRIX " not implemented\n", addr);
    return UINT64_MAX;
}

static void start_stop(RCMTState *cmt, int ch, int st)
{
    if (st) {
        cmt->c[ch].start = true;
        update_events(&cmt->c[ch]);
    } else {
        cmt->c[ch].start = false;
        timer_del(cmt->c[ch].timer);
    }
}

static void cmt_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    RCMTState *cmt = opaque;
    int ch = addr / 0x08;
    int div;

    if (addr == A_CMSTR) {
        cmt->cmstr = FIELD_EX16(val, CMSTR, STR);
        start_stop(cmt, 0, FIELD_EX16(cmt->cmstr, CMSTR, STR0));
        start_stop(cmt, 1, FIELD_EX16(cmt->cmstr, CMSTR, STR1));
    } else {
        addr &= 0x07;
        if (ch == 0) {
            addr -= 0x02;
        }
        switch (addr) {
        case A_CMCR:
            cmt->c[ch].cmcr = FIELD_DP16(cmt->c[ch].cmcr, CMCR, CKS,
                                       FIELD_EX16(val, CMCR, CKS));
            cmt->c[ch].cmcr = FIELD_DP16(cmt->c[ch].cmcr, CMCR, CMIE,
                                       FIELD_EX16(val, CMCR, CMIE));
            /*
             * CKS -> div rate
             *  0 -> 8 (1 << 3)
             *  1 -> 32 (1 << 5)
             *  2 -> 128 (1 << 7)
             *  3 -> 512 (1 << 9)
             */
            div = 1 << (3 + 2 * FIELD_EX16(cmt->c[ch].cmcr, CMCR, CKS));
            cmt->c[ch].clk_per_ns = NANOSECONDS_PER_SECOND / cmt->input_freq;
            cmt->c[ch].clk_per_ns *= div;
            break;
        case A_CMCNT:
            cmt->c[ch].cmcnt = val;
            break;
        case A_CMCOR:
            cmt->c[ch].cmcor = val;
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "renesas_cmt: Register -0x%" HWADDR_PRIX
                          " not implemented\n", addr);
            return;
        }
        if (FIELD_EX16(cmt->cmstr, CMSTR, STR) & (1 << ch)) {
            update_events(&cmt->c[ch]);
        }
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

static void timer_event(void *opaque)
{
    struct RCMTChannelState *c = opaque;

    c->cmcnt = 0;
    c->tick = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    update_events(c);
    if (FIELD_EX16(c->cmcr, CMCR, CMIE)) {
        qemu_irq_pulse(c->cmi);
    }
}

static void rcmt_reset(DeviceState *dev)
{
    RCMTState *cmt = RCMT(dev);
    cmt->cmstr = 0;
    cmt->c[0].start = cmt->c[1].start = false;
    cmt->c[0].cmcr = cmt->c[1].cmcr = 0;
    cmt->c[0].cmcnt = cmt->c[1].cmcnt = 0;
    cmt->c[0].cmcor = cmt->c[1].cmcor = 0xffff;
}

static void rcmt_init(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    RCMTState *cmt = RCMT(obj);
    int i;

    memory_region_init_io(&cmt->memory, OBJECT(cmt), &cmt_ops,
                          cmt, "renesas-cmt", 0x10);
    sysbus_init_mmio(d, &cmt->memory);

    for (i = 0; i < CMT_CH; i++) {
        sysbus_init_irq(d, &cmt->c[i].cmi);
    }
    cmt->c[0].timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                   timer_event, &cmt->c[0]);
    cmt->c[1].timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                   timer_event, &cmt->c[1]);
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
