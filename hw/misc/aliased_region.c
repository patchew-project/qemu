/*
 * Aliased memory regions
 *
 * Copyright (c) 2018  Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/misc/aliased_region.h"
#include "hw/qdev-properties.h"

static void aliased_mem_realize(AliasedRegionState *s, const char *mr_name)
{
    uint64_t subregion_size;
    int subregion_bits;

    memory_region_init(&s->container, OBJECT(s), mr_name, s->region_size);

    subregion_bits = 64 - clz64(s->span_size - 1);
    s->mem.count = s->region_size >> subregion_bits;
    assert(s->mem.count > 1);
    subregion_size = 1ULL << subregion_bits;

    s->mem.alias = g_new(MemoryRegion, s->mem.count);
    for (size_t i = 0; i < s->mem.count; i++) {
        g_autofree char *name = g_strdup_printf("%s [#%zu/%zu]",
                                                memory_region_name(s->mr),
                                                i, s->mem.count);
        memory_region_init_alias(&s->mem.alias[i], OBJECT(s), name,
                                 s->mr, 0, s->span_size);
        memory_region_add_subregion(&s->container, i * subregion_size,
                                    &s->mem.alias[i]);
    }
}

static void aliased_mr_realize(DeviceState *dev, Error **errp)
{
    AliasedRegionState *s = ALIASED_REGION(dev);
    g_autofree char *name = NULL, *span = NULL;

    if (s->region_size == 0) {
        error_setg(errp, "property 'region-size' not specified or zero");
        return;
    }

    if (s->mr == NULL) {
        error_setg(errp, "property 'iomem' not specified");
        return;
    }

    if (!s->span_size) {
        s->span_size = pow2ceil(memory_region_size(s->mr));
    } else if (!is_power_of_2(s->span_size)) {
        error_setg(errp, "property 'span-size' must be a power of 2");
        return;
    }

    span = size_to_str(s->span_size);
    name = g_strdup_printf("masked %s [span of %s]",
                           memory_region_name(s->mr), span);
    aliased_mem_realize(s, name);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->container);
}

static void aliased_mr_unrealize(DeviceState *dev)
{
    AliasedRegionState *s = ALIASED_REGION(dev);

    g_free(s->mem.alias);
}

static Property aliased_mr_properties[] = {
    DEFINE_PROP_UINT64("region-size", AliasedRegionState, region_size, 0),
    DEFINE_PROP_UINT64("span-size", AliasedRegionState, span_size, 0),
    DEFINE_PROP_LINK("iomem", AliasedRegionState, mr,
                     TYPE_MEMORY_REGION, MemoryRegion *),
    DEFINE_PROP_END_OF_LIST(),
};

static void aliased_mr_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = aliased_mr_realize;
    dc->unrealize = aliased_mr_unrealize;
    /* Reason: needs to be wired up to work */
    dc->user_creatable = false;
    device_class_set_props(dc, aliased_mr_properties);
}

static const TypeInfo aliased_mr_info = {
    .name = TYPE_ALIASED_REGION,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AliasedRegionState),
    .class_init = aliased_mr_class_init,
};

static void aliased_mr_register_types(void)
{
    type_register_static(&aliased_mr_info);
}

type_init(aliased_mr_register_types)

void memory_region_add_subregion_aliased(MemoryRegion *container,
                                         hwaddr offset,
                                         uint64_t region_size,
                                         MemoryRegion *subregion,
                                         uint64_t span_size)
{
    DeviceState *dev;

    if (!region_size) {
        region_size = pow2ceil(memory_region_size(container));
    } else {
        assert(region_size <= memory_region_size(container));
    }

    dev = qdev_new(TYPE_ALIASED_REGION);
    qdev_prop_set_uint64(dev, "region-size", region_size);
    qdev_prop_set_uint64(dev, "span-size", span_size);
    object_property_set_link(OBJECT(dev), "iomem", OBJECT(subregion),
                             &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(dev), &error_abort);

    memory_region_add_subregion(container, offset,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0));
}
