/*
 * ASPEED GFX Controller
 *
 * Copyright (C) 2023 IBM Corp.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "hw/misc/aspeed_gfx.h"
#include "qapi/error.h"
#include "migration/vmstate.h"

#include "trace.h"

static uint64_t aspeed_gfx_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    AspeedGFXState *s = ASPEED_GFX(opaque);
    uint64_t val = 0;

    addr >>= 2;

    if (addr >= ASPEED_GFX_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds read at offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr << 2);
    } else {
        val = s->regs[addr];
    }

    trace_aspeed_gfx_read(addr << 2, val);

    return val;
}

static void aspeed_gfx_write(void *opaque, hwaddr addr, uint64_t data,
                              unsigned int size)
{
    AspeedGFXState *s = ASPEED_GFX(opaque);

    trace_aspeed_gfx_write(addr, data);

    addr >>= 2;

    if (addr >= ASPEED_GFX_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds write at offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr << 2);
        return;
    }

    s->regs[addr] = data;
}

static const MemoryRegionOps aspeed_gfx_ops = {
    .read = aspeed_gfx_read,
    .write = aspeed_gfx_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void aspeed_gfx_reset(DeviceState *dev)
{
    struct AspeedGFXState *s = ASPEED_GFX(dev);

    memset(s->regs, 0, sizeof(s->regs));
}

static void aspeed_gfx_realize(DeviceState *dev, Error **errp)
{
    AspeedGFXState *s = ASPEED_GFX(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    sysbus_init_irq(sbd, &s->irq);

    memory_region_init_io(&s->iomem, OBJECT(s), &aspeed_gfx_ops, s,
            TYPE_ASPEED_GFX, 0x1000);

    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_aspeed_gfx = {
    .name = TYPE_ASPEED_GFX,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AspeedGFXState, ASPEED_GFX_NR_REGS),
        VMSTATE_END_OF_LIST(),
    }
};

static void aspeed_gfx_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = aspeed_gfx_realize;
    dc->reset = aspeed_gfx_reset;
    dc->desc = "Aspeed GFX Controller";
    dc->vmsd = &vmstate_aspeed_gfx;
}

static const TypeInfo aspeed_gfx_info = {
    .name = TYPE_ASPEED_GFX,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AspeedGFXState),
    .class_init = aspeed_gfx_class_init,
};

static void aspeed_gfx_register_types(void)
{
    type_register_static(&aspeed_gfx_info);
}

type_init(aspeed_gfx_register_types);
