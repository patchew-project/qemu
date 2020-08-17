/*
 * QEMU Interleaver device
 *
 * The interleaver device to allow making interleaved memory accesses.
 *
 * This device support using the following configurations (INPUT x OUTPUT):
 * 16x8, 32x8, 32x16, 64x8, 64x16 and 64x32.
 *
 * Copyright (C) 2020 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "hw/misc/interleaver.h"
#include "trace.h"

#define TYPE_INTERLEAVER_DEVICE "interleaver-device"

typedef struct InterleaverDeviceClass {
    /*< private >*/
    SysBusDeviceClass parent_class;
    /*< public >*/
    MemoryRegionOps ops;
    unsigned input_access_size;
    unsigned output_access_size;
    MemOp output_memop;
    unsigned mr_count;
    char *name;
} InterleaverDeviceClass;

#define INTERLEAVER_DEVICE_CLASS(klass) \
    OBJECT_CLASS_CHECK(InterleaverDeviceClass, (klass), TYPE_INTERLEAVER_DEVICE)
#define INTERLEAVER_DEVICE_GET_CLASS(obj) \
    OBJECT_GET_CLASS(InterleaverDeviceClass, (obj), TYPE_INTERLEAVER_DEVICE)

#define INTERLEAVER_REGIONS_MAX 8 /* 64x8 */

typedef struct {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/
    MemoryRegion iomem;
    uint64_t size;
    MemoryRegion *mr[INTERLEAVER_REGIONS_MAX];
} InterleaverDeviceState;

#define INTERLEAVER_DEVICE(obj) \
    OBJECT_CHECK(InterleaverDeviceState, (obj), TYPE_INTERLEAVER_DEVICE)

static const char *memresult_str[] = {"OK", "ERROR", "DECODE_ERROR"};

static const char *emtpy_mr_name = "EMPTY";

static MemTxResult interleaver_read(void *opaque,
                                    hwaddr offset, uint64_t *data,
                                    unsigned size, MemTxAttrs attrs)
{
    InterleaverDeviceState *s = INTERLEAVER_DEVICE(opaque);
    InterleaverDeviceClass *idc = INTERLEAVER_DEVICE_GET_CLASS(s);
    unsigned idx = (offset / idc->output_access_size) & (idc->mr_count - 1);
    hwaddr addr = (offset & ~(idc->input_access_size - 1)) / idc->mr_count;
    MemTxResult r = MEMTX_ERROR;

    trace_interleaver_read_enter(idc->input_access_size,
                                 idc->output_access_size, size,
                                 idc->mr_count, idx,
                                 s->mr[idx] ? memory_region_name(s->mr[idx])
                                            : emtpy_mr_name,
                                 offset, addr);
    if (s->mr[idx]) {
        r = memory_region_dispatch_read(s->mr[idx],
                                        addr,
                                        data,
                                        idc->output_memop,
                                        attrs);
    }
    trace_interleaver_read_exit(size, *data, memresult_str[r]);

    return r;
}

static MemTxResult interleaver_write(void *opaque,
                                     hwaddr offset, uint64_t data,
                                     unsigned size, MemTxAttrs attrs)
{
    InterleaverDeviceState *s = INTERLEAVER_DEVICE(opaque);
    InterleaverDeviceClass *idc = INTERLEAVER_DEVICE_GET_CLASS(s);
    unsigned idx = (offset / idc->output_access_size) & (idc->mr_count - 1);
    hwaddr addr = (offset & ~(idc->input_access_size - 1)) / idc->mr_count;
    MemTxResult r = MEMTX_ERROR;

    trace_interleaver_write_enter(idc->input_access_size,
                                  idc->output_access_size, size,
                                  idc->mr_count, idx,
                                  s->mr[idx] ? memory_region_name(s->mr[idx])
                                             : emtpy_mr_name,
                                  offset, addr);
    if (s->mr[idx]) {
        r = memory_region_dispatch_write(s->mr[idx],
                                         addr,
                                         data,
                                         idc->output_memop,
                                         attrs);
    }
    trace_interleaver_write_exit(size, data, memresult_str[r]);

    return r;
}

static void interleaver_realize(DeviceState *dev, Error **errp)
{
    InterleaverDeviceState *s = INTERLEAVER_DEVICE(dev);
    InterleaverDeviceClass *idc = INTERLEAVER_DEVICE_GET_CLASS(dev);
    uint64_t expected_mr_size;

    if (s->size == 0) {
        error_setg(errp, "property 'size' not specified or zero");
        return;
    }
    if (!QEMU_IS_ALIGNED(s->size, idc->input_access_size)) {
        error_setg(errp, "property 'size' must be multiple of %u",
                   idc->input_access_size);
        return;
    }

    expected_mr_size = s->size / idc->mr_count;
    for (unsigned i = 0; i < idc->mr_count; i++) {
        if (s->mr[i] && memory_region_size(s->mr[i]) != expected_mr_size) {
            error_setg(errp,
                       "memory region #%u (%s) size mismatches interleaver",
                       i, memory_region_name(s->mr[i]));
            return;
        }
    }
    memory_region_init_io(&s->iomem, OBJECT(s), &idc->ops, s,
                          idc->name, s->size);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
}

static Property interleaver_properties[] = {
    DEFINE_PROP_UINT64("size", InterleaverDeviceState, size, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void interleaver_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = interleaver_realize;
    device_class_set_props(dc, interleaver_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void interleaver_class_add_properties(ObjectClass *oc,
                                             unsigned input_bits,
                                             unsigned output_bits)
{
    InterleaverDeviceClass *idc = INTERLEAVER_DEVICE_CLASS(oc);

    idc->name = g_strdup_printf("interleaver-%ux%u", input_bits, output_bits);
    idc->input_access_size = input_bits >> 3;
    idc->output_access_size = output_bits >> 3;
    idc->output_memop = size_memop(idc->output_access_size);
    idc->mr_count = input_bits / output_bits;
    idc->ops = (MemoryRegionOps){
        .read_with_attrs = interleaver_read,
        .write_with_attrs = interleaver_write,
        .valid.min_access_size = 1,
        .valid.max_access_size = idc->input_access_size,
        .impl.min_access_size = idc->output_access_size,
        .impl.max_access_size = idc->output_access_size,
        .endianness = DEVICE_NATIVE_ENDIAN,
    };

    for (unsigned i = 0; i < idc->mr_count; i++) {
        g_autofree char *name = g_strdup_printf("mr%u", i);
        object_class_property_add_link(oc, name, TYPE_MEMORY_REGION,
                                       offsetof(InterleaverDeviceState, mr[i]),
                                       qdev_prop_allow_set_link_before_realize,
                                       OBJ_PROP_LINK_STRONG);
    }
}

static void interleaver_16x8_class_init(ObjectClass *oc, void *data)
{
    interleaver_class_add_properties(oc, 16, 8);
};

static void interleaver_32x8_class_init(ObjectClass *oc, void *data)
{
    interleaver_class_add_properties(oc, 32, 8);
};

static void interleaver_32x16_class_init(ObjectClass *oc, void *data)
{
    interleaver_class_add_properties(oc, 32, 16);
};

static void interleaver_64x8_class_init(ObjectClass *oc, void *data)
{
    interleaver_class_add_properties(oc, 64, 8);
};

static void interleaver_64x16_class_init(ObjectClass *oc, void *data)
{
    interleaver_class_add_properties(oc, 64, 16);
};

static void interleaver_64x32_class_init(ObjectClass *oc, void *data)
{
    interleaver_class_add_properties(oc, 64, 32);
};

static const TypeInfo interleaver_device_types[] = {
    {
        .name           = TYPE_INTERLEAVER_16X8_DEVICE,
        .parent         = TYPE_INTERLEAVER_DEVICE,
        .class_init     = interleaver_16x8_class_init,
    }, {
        .name           = TYPE_INTERLEAVER_32X8_DEVICE,
        .parent         = TYPE_INTERLEAVER_DEVICE,
        .class_init     = interleaver_32x8_class_init,
    }, {
        .name           = TYPE_INTERLEAVER_32X16_DEVICE,
        .parent         = TYPE_INTERLEAVER_DEVICE,
        .class_init     = interleaver_32x16_class_init,
    }, {
        .name           = TYPE_INTERLEAVER_64X8_DEVICE,
        .parent         = TYPE_INTERLEAVER_DEVICE,
        .class_init     = interleaver_64x8_class_init,
    }, {
        .name           = TYPE_INTERLEAVER_64X16_DEVICE,
        .parent         = TYPE_INTERLEAVER_DEVICE,
        .class_init     = interleaver_64x16_class_init,
    }, {
        .name           = TYPE_INTERLEAVER_64X32_DEVICE,
        .parent         = TYPE_INTERLEAVER_DEVICE,
        .class_init     = interleaver_64x32_class_init,
    }, {
        .name           = TYPE_INTERLEAVER_DEVICE,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(InterleaverDeviceState),
        .class_size     = sizeof(InterleaverDeviceClass),
        .class_init     = interleaver_class_init,
        .abstract       = true,
    }
};

DEFINE_TYPES(interleaver_device_types)
