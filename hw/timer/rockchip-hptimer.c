/*
 * Rockchip RK3588 HPTIMER boot control register
 *
 * Copyright (C) 2026 Process Mission
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "hw/core/resettable.h"
#include "hw/core/sysbus.h"
#include "hw/timer/rockchip-hptimer.h"
#include "migration/vmstate.h"

/* Model the enable state used at firmware handoff; the CPU provides timing. */
struct RockchipHPTimerState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t ctrl;
};

static uint64_t rockchip_hptimer_read(void *opaque, hwaddr offset,
                                     unsigned size)
{
    RockchipHPTimerState *s = ROCKCHIP_HPTIMER(opaque);

    switch (offset) {
    case ROCKCHIP_HPTIMER_CTRL:
        return s->ctrl;
    default:
        qemu_log_mask(LOG_UNIMP, "rockchip-hptimer: unimplemented read at "
                      "0x%" HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void rockchip_hptimer_write(void *opaque, hwaddr offset,
                                  uint64_t value, unsigned size)
{
    RockchipHPTimerState *s = ROCKCHIP_HPTIMER(opaque);

    switch (offset) {
    case ROCKCHIP_HPTIMER_CTRL:
        s->ctrl = value & ROCKCHIP_HPTIMER_ENABLE;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "rockchip-hptimer: unimplemented write at "
                      "0x%" HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps rockchip_hptimer_ops = {
    .read = rockchip_hptimer_read,
    .write = rockchip_hptimer_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void rockchip_hptimer_reset_enter(Object *obj, ResetType type)
{
    RockchipHPTimerState *s = ROCKCHIP_HPTIMER(obj);

    s->ctrl = 0;
}

static void rockchip_hptimer_init(Object *obj)
{
    RockchipHPTimerState *s = ROCKCHIP_HPTIMER(obj);

    memory_region_init_io(&s->iomem, obj, &rockchip_hptimer_ops, s,
                          TYPE_ROCKCHIP_HPTIMER, 16 * KiB);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription vmstate_rockchip_hptimer = {
    .name = TYPE_ROCKCHIP_HPTIMER,
    .unmigratable = true,
};

static void rockchip_hptimer_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    rc->phases.enter = rockchip_hptimer_reset_enter;
    dc->desc = "Rockchip RK3588 HPTIMER boot control";
    dc->vmsd = &vmstate_rockchip_hptimer;
    dc->user_creatable = false;
}

static const TypeInfo rockchip_hptimer_typeinfo = {
    .name = TYPE_ROCKCHIP_HPTIMER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RockchipHPTimerState),
    .instance_init = rockchip_hptimer_init,
    .class_init = rockchip_hptimer_class_init,
};

static void rockchip_hptimer_register_types(void)
{
    type_register_static(&rockchip_hptimer_typeinfo);
}

type_init(rockchip_hptimer_register_types)
