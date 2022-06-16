/*
 * QEMU PC System Emulator
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
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
#include "exec/memory.h"
#include "qapi/error.h"
#include "qom/object.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "trace.h"
#include "hw/i386/postcard.h"

struct POSTCardState {
    ISADevice parent_obj;

    MemoryRegion io;
    uint16_t port;
    uint8_t mem;
};

#define TYPE_POST_CARD "post-card"
OBJECT_DECLARE_SIMPLE_TYPE(POSTCardState, POST_CARD)

static uint64_t
post_card_read(void *opaque, hwaddr addr, unsigned size)
{
    POSTCardState *s = opaque;
    uint64_t val;

    memset(&val, s->mem, sizeof(val));
    return val;
}

static void
post_card_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    POSTCardState *s = opaque;
    const uint8_t val = data & 0xff;

    if (val != s->mem) {
        trace_post_card_write(val, s->mem);
        s->mem = val;
    }
}

static const MemoryRegionOps post_card_ops = {
    .read = post_card_read,
    .write = post_card_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void
post_card_reset(DeviceState *dev)
{
    POSTCardState *s = POST_CARD(dev);
    s->mem = 0;
}

static void
post_card_realize(DeviceState *dev, Error **errp)
{
    ISADevice *isadev = ISA_DEVICE(dev);
    POSTCardState *s = POST_CARD(dev);

    memory_region_init_io(&s->io, OBJECT(s), &post_card_ops,
                          s, TYPE_POST_CARD, 1);
    isa_register_ioport(isadev, &s->io, s->port);
    post_card_reset(dev);
}

static Property post_card_properties[] = {
    DEFINE_PROP_UINT16("iobase", POSTCardState, port,
                       POST_CARD_PORT_DEFAULT),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription post_card_vmstate = {
    .name = TYPE_POST_CARD,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(port, POSTCardState),
        VMSTATE_UINT8(mem, POSTCardState),
        VMSTATE_END_OF_LIST()
    }
};

static void
post_card_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->desc = "ISA POST card";
    dc->realize = post_card_realize;
    dc->reset = post_card_reset;
    dc->vmsd = &post_card_vmstate;
    device_class_set_props(dc, post_card_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo post_card_info = {
    .name          = TYPE_POST_CARD,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(POSTCardState),
    .class_init    = post_card_class_init,
};

ISADevice *
post_card_init(ISABus *bus, uint16_t iobase)
{
    DeviceState *dev;
    ISADevice *isadev;

    isadev = isa_new(TYPE_POST_CARD);
    dev = DEVICE(isadev);
    qdev_prop_set_uint16(dev, "iobase", iobase);
    isa_realize_and_unref(isadev, bus, &error_fatal);

    return isadev;
}

static void
post_card_register_types(void)
{
    type_register_static(&post_card_info);
}

type_init(post_card_register_types)
