#include "apple-gfx.h"
#include "monitor/monitor.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "trace.h"
#import <ParavirtualizedGraphics/ParavirtualizedGraphics.h>

_Static_assert(__aarch64__, "");

/*
 * ParavirtualizedGraphics.Framework only ships header files for the x86
 * variant which does not include IOSFC descriptors and host devices. We add
 * their definitions here so that we can also work with the ARM version.
 */
typedef bool(^IOSFCRaiseInterrupt)(uint32_t vector);
typedef bool(^IOSFCUnmapMemory)(
    void *a, void *b, void *c, void *d, void *e, void *f);
typedef bool(^IOSFCMapMemory)(
    uint64_t phys, uint64_t len, bool ro, void **va, void *e, void *f);

@interface PGDeviceDescriptor (IOSurfaceMapper)
@property (readwrite, nonatomic) bool usingIOSurfaceMapper;
@end

@interface PGIOSurfaceHostDeviceDescriptor : NSObject
-(PGIOSurfaceHostDeviceDescriptor *)init;
@property (readwrite, nonatomic, copy, nullable) IOSFCMapMemory mapMemory;
@property (readwrite, nonatomic, copy, nullable) IOSFCUnmapMemory unmapMemory;
@property (readwrite, nonatomic, copy, nullable) IOSFCRaiseInterrupt raiseInterrupt;
@end

@interface PGIOSurfaceHostDevice : NSObject
-(instancetype)initWithDescriptor:(PGIOSurfaceHostDeviceDescriptor *) desc;
-(uint32_t)mmioReadAtOffset:(size_t) offset;
-(void)mmioWriteAtOffset:(size_t) offset value:(uint32_t)value;
@end

typedef struct AppleGFXVmappleState {
    SysBusDevice parent_obj;

    AppleGFXState common;

    qemu_irq irq_gfx;
    qemu_irq irq_iosfc;
    MemoryRegion iomem_iosfc;
    PGIOSurfaceHostDevice *pgiosfc;
} AppleGFXVmappleState;

OBJECT_DECLARE_SIMPLE_TYPE(AppleGFXVmappleState, APPLE_GFX_VMAPPLE)


static uint64_t apple_iosfc_read(void *opaque, hwaddr offset, unsigned size)
{
    AppleGFXVmappleState *s = opaque;
    uint64_t res = 0;

    bql_unlock();
    res = [s->pgiosfc mmioReadAtOffset:offset];
    bql_lock();

    trace_apple_iosfc_read(offset, res);

    return res;
}

static void apple_iosfc_write(
    void *opaque, hwaddr offset, uint64_t val, unsigned size)
{
    AppleGFXVmappleState *s = opaque;

    trace_apple_iosfc_write(offset, val);

    [s->pgiosfc mmioWriteAtOffset:offset value:val];
}

static const MemoryRegionOps apple_iosfc_ops = {
    .read = apple_iosfc_read,
    .write = apple_iosfc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static PGIOSurfaceHostDevice *apple_gfx_prepare_iosurface_host_device(
    AppleGFXVmappleState *s)
{
    PGIOSurfaceHostDeviceDescriptor *iosfc_desc =
        [PGIOSurfaceHostDeviceDescriptor new];
    PGIOSurfaceHostDevice *iosfc_host_dev = nil;

    iosfc_desc.mapMemory =
        ^(uint64_t phys, uint64_t len, bool ro, void **va, void *e, void *f) {
            trace_apple_iosfc_map_memory(phys, len, ro, va, e, f);
            MemoryRegion *tmp_mr;
            *va = gpa2hva(&tmp_mr, phys, len, NULL);
            return (bool)true;
        };

    iosfc_desc.unmapMemory =
        ^(void *a, void *b, void *c, void *d, void *e, void *f) {
            trace_apple_iosfc_unmap_memory(a, b, c, d, e, f);
            return (bool)true;
        };

    iosfc_desc.raiseInterrupt = ^(uint32_t vector) {
        trace_apple_iosfc_raise_irq(vector);
        bool locked = bql_locked();
        if (!locked) {
            bql_lock();
        }
        qemu_irq_pulse(s->irq_iosfc);
        if (!locked) {
            bql_unlock();
        }
        return (bool)true;
    };

    iosfc_host_dev =
        [[PGIOSurfaceHostDevice alloc] initWithDescriptor:iosfc_desc];
    [iosfc_desc release];
    return iosfc_host_dev;
}

static void apple_gfx_vmapple_realize(DeviceState *dev, Error **errp)
{
    @autoreleasepool {
        AppleGFXVmappleState *s = APPLE_GFX_VMAPPLE(dev);

        PGDeviceDescriptor *desc = [PGDeviceDescriptor new];
        desc.usingIOSurfaceMapper = true;
        desc.raiseInterrupt = ^(uint32_t vector) {
            bool locked;

            trace_apple_gfx_raise_irq(vector);
            locked = bql_locked();
            if (!locked) {
                bql_lock();
            }
            qemu_irq_pulse(s->irq_gfx);
            if (!locked) {
                bql_unlock();
            }
        };

        s->pgiosfc = apple_gfx_prepare_iosurface_host_device(s);

        apple_gfx_common_realize(&s->common, desc);
        [desc release];
        desc = nil;
    }
}

static void apple_gfx_vmapple_reset(DeviceState *d)
{
}

static void apple_gfx_vmapple_init(Object *obj)
{
    AppleGFXVmappleState *s = APPLE_GFX_VMAPPLE(obj);

    apple_gfx_common_init(obj, &s->common, TYPE_APPLE_GFX_VMAPPLE);

    memory_region_init_io(&s->iomem_iosfc, obj, &apple_iosfc_ops, s,
                          TYPE_APPLE_GFX_VMAPPLE, 0x10000);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->common.iomem_gfx);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem_iosfc);
    sysbus_init_irq(SYS_BUS_DEVICE(s), &s->irq_gfx);
    sysbus_init_irq(SYS_BUS_DEVICE(s), &s->irq_iosfc);
}

static void apple_gfx_vmapple_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = apple_gfx_vmapple_reset;
    dc->realize = apple_gfx_vmapple_realize;
}

static TypeInfo apple_gfx_vmapple_types[] = {
    {
        .name          = TYPE_APPLE_GFX_VMAPPLE,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(AppleGFXVmappleState),
        .class_init    = apple_gfx_vmapple_class_init,
        .instance_init = apple_gfx_vmapple_init,
    }
};
DEFINE_TYPES(apple_gfx_vmapple_types)
