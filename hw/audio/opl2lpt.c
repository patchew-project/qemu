/*
 * QEMU Proxy for OPL2LPT
 *
 * Copyright (c) 2018 Vincent Bernat
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

/* TODO: emulate timers */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/audio/soundhw.h"
#include "audio/audio.h"
#include "hw/isa/isa.h"
#include "chardev/char-parallel.h"
#include "chardev/char-fe.h"

#define DEBUG

#define OPL2LPT_DESC "OPL2LPT (Yamaha YM3812 over parallel port)"

#define dolog(...) AUD_log("opl2lpt", __VA_ARGS__)
#ifdef DEBUG
#define ldebug(...) dolog(__VA_ARGS__)
#else
#define ldebug(...)
#endif

#define TYPE_OPL2LPT "opl2lpt"
#define OPL2LPT(obj) OBJECT_CHECK(Opl2lptState, (obj), TYPE_OPL2LPT)

#define PP_NOT_STROBE  0x1
#define PP_NOT_AUTOFD  0x2
#define PP_INIT        0x4
#define PP_NOT_SELECT  0x8

typedef struct {
    ISADevice parent_obj;

    uint8_t address;
    uint8_t timer_reg;
    int64_t last_clock;
    PortioList port_list;
    CharBackend chr;
} Opl2lptState;

static void opl2lpt_lpt_write(Opl2lptState *s, uint8_t d, uint8_t c)
{
    qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_PP_WRITE_DATA, &d);
    qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_PP_WRITE_CONTROL, &c);
    c ^= PP_INIT;
    qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_PP_WRITE_CONTROL, &c);
    c ^= PP_INIT;
    qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_PP_WRITE_CONTROL, &c);
}

static void opl2lpt_write(void *opaque, uint32_t nport, uint32_t val)
{
    Opl2lptState *s = opaque;
    int a = nport & 1;
    uint8_t v = val & 0xff;
    uint8_t c = 0;
#ifdef DEBUG
    int64_t last_clock = get_clock();
    uint64_t diff = last_clock - s->last_clock;
    s->last_clock = last_clock;
#endif

    switch (a) {
    case 0:
        /* address port */
        ldebug("[%10" PRIu64 "]: write 0x%" PRIx32 " (address) = %" PRIx8 "\n",
               diff/1000, nport, v);
        s->address = v;
        c = PP_INIT | PP_NOT_SELECT | PP_NOT_STROBE;
        opl2lpt_lpt_write(s, v, c);
        usleep(3);
        break;
    case 1:
        /* data port */
        if (s->address == 4) {
            /* Timer Control Byte register */
            s->timer_reg = v;
        }
        ldebug("[%10" PRIu64 "]: write 0x%" PRIx32 " (data) = %d\n",
               diff/1000, nport, v);
        c = PP_INIT | PP_NOT_SELECT;
        opl2lpt_lpt_write(s, v, c);
        usleep(23);
        break;
    default:
        assert(0);
    }
}

static uint32_t opl2lpt_read(void *opaque, uint32_t nport)
{
    Opl2lptState *s = opaque;
    int a = nport & 1;
    uint8_t v = 0;

    switch (a) {
    case 0:
        /* address port: only emulate timers. They expire
         * instantaneously: they are generally not used for anything
         * else than a detection feature. */
        if ((s->timer_reg & 0xC1) == 1) {
            v |= 0xC0;
        }
        if ((s->timer_reg & 0xA2) == 2) {
            v |= 0xA0;
        }
        v |= 0x06;              /* Value stolen from opl2lpt DOS driver */

        ldebug("read 0x%" PRIx32 " (address) = 0x%" PRIx8 "\n", nport, v);
        break;
    case 1:
        /* data port: write-only */
        ldebug("read 0x%" PRIx32 " (data) = 0\n", nport);
        break;
    }
    return v;
}

static MemoryRegionPortio opl2lpt_portio_list[] = {
    { 0x388, 2, 1, .read = opl2lpt_read, .write = opl2lpt_write, },
    PORTIO_END_OF_LIST(),
};

static void opl2lpt_reset(DeviceState *dev)
{
    Opl2lptState *s = OPL2LPT(dev);

    ldebug("reset OPL2 chip\n");
    for (int i = 0; i < 256; i ++) {
        opl2lpt_lpt_write(s, i, PP_INIT | PP_NOT_STROBE | PP_NOT_SELECT);
        usleep(4);
        opl2lpt_lpt_write(s, 0, PP_INIT | PP_NOT_SELECT);
        usleep(23);
    }

    s->last_clock = get_clock();
}

static void opl2lpt_realize(DeviceState *dev, Error **errp)
{
    Opl2lptState *s = OPL2LPT(dev);

    if (!qemu_chr_fe_backend_connected(&s->chr)) {
        error_setg(errp, "Can't create OPL2LPT device, empty char device");
        return;
    }

    portio_list_init(&s->port_list, OBJECT(s), opl2lpt_portio_list, s, "opl2lpt");
    portio_list_add(&s->port_list, isa_address_space_io(&s->parent_obj), 0);
}

static Property opl2lpt_properties[] = {
    DEFINE_PROP_CHR("chardev", Opl2lptState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void opl2lpt_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = opl2lpt_realize;
    dc->reset = opl2lpt_reset;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    dc->desc = OPL2LPT_DESC;
    dc->props = opl2lpt_properties;
}

static const TypeInfo opl2lpt_info = {
    .name          = TYPE_OPL2LPT,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(Opl2lptState),
    .class_init    = opl2lpt_class_initfn,
};

static int Opl2lpt_init(ISABus *bus)
{
    isa_create_simple(bus, TYPE_OPL2LPT);
    return 0;
}

static void opl2lpt_register_types(void)
{
    type_register_static(&opl2lpt_info);
    isa_register_soundhw("opl2lpt", OPL2LPT_DESC, Opl2lpt_init);
}

type_init(opl2lpt_register_types)
