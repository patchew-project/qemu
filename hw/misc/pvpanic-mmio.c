#include "qemu/osdep.h"
#include "sysemu/sysemu.h"
#include "qemu/log.h"
#include "hw/misc/pvpanic-mmio.h"

#define PVPANIC_MMIO_FEAT_CRASHED      0

#define PVPANIC_MMIO_CRASHED        (1 << PVPANIC_MMIO_FEAT_CRASHED)

static void handle_mmio_event(int event)
{
    static bool logged;

    if (event & ~PVPANIC_MMIO_CRASHED && !logged) {
        qemu_log_mask(LOG_GUEST_ERROR, "pvpanic-mmio: unknown event %#x.\n", event);
        logged = true;
    }

    if (event & PVPANIC_MMIO_CRASHED) {
        qemu_system_guest_panicked(NULL);
        return;
    }
}

static uint64_t pvpanic_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return -1;
}

static void pvpanic_mmio_write(void *opaque, hwaddr addr, uint64_t value,
                                 unsigned size)
{
   handle_mmio_event(value);
}

static const MemoryRegionOps pvpanic_mmio_ops = {
    .read = pvpanic_mmio_read,
    .write = pvpanic_mmio_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 2,
    },
};


static void pvpanic_mmio_initfn(Object *obj)
{
    PVPanicState *s = PVPANIC_MMIO_DEVICE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, OBJECT(s), &pvpanic_mmio_ops, s,
                          "pvpanic-mmio", 2);
    sysbus_init_mmio(sbd, &s->mmio);
}

static void pvpanic_mmio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static TypeInfo pvpanic_mmio_info = {
    .name          = TYPE_PVPANIC_MMIO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PVPanicState),
    .instance_init = pvpanic_mmio_initfn,
    .class_init    = pvpanic_mmio_class_init,
};

static void pvpanic_mmio_register_types(void)
{
    type_register_static(&pvpanic_mmio_info);
}

type_init(pvpanic_mmio_register_types)
