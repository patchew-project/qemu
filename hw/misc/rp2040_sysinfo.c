/*
 * RP2040 sysinfo emulation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/misc/rp2040_nyi.h"
#include "hw/misc/rp2040_sysinfo.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define SYSINFO_CHIP_ID        0x00
#define SYSINFO_PLATFORM       0x04
#define SYSINFO_GITREF_RP2040  0x40

#define SYSINFO_PLATFORM_ASIC  BIT(1)

/*
 * The datasheet documents this as the chip source git hash, but does not
 * require software-visible semantics beyond a stable chip-version value.
 */
#define SYSINFO_GITREF_QEMU    0x00000000

static uint64_t rp2040_sysinfo_read(void *opaque, hwaddr addr, unsigned size)
{
    hwaddr offset = addr & 0xfff;
    uint64_t value;

    switch (offset) {
    case SYSINFO_CHIP_ID:
        value = 0;
        break;
    case SYSINFO_PLATFORM:
        value = SYSINFO_PLATFORM_ASIC;
        break;
    case SYSINFO_GITREF_RP2040:
        value = SYSINFO_GITREF_QEMU;
        break;
    default:
        value = 0;
        rp2040_log_unimplemented_read("sysinfo", size,
                                      RP2040_SYSINFO_BASE + addr, offset,
                                      value);
        break;
    }

    return value;
}

static void rp2040_sysinfo_write(void *opaque, hwaddr addr,
                                 uint64_t value, unsigned size)
{
    hwaddr offset = addr & 0xfff;

    qemu_log_mask(LOG_GUEST_ERROR, "rp2040.sysinfo: write to read-only "
                  "register (size %d, addr 0x%08" HWADDR_PRIx
                  ", offset 0x%04" HWADDR_PRIx
                  ", value 0x%0*" PRIx64 ")\n",
                  size, RP2040_SYSINFO_BASE + addr, offset, size << 1, value);
}

static const MemoryRegionOps rp2040_sysinfo_ops = {
    .read = rp2040_sysinfo_read,
    .write = rp2040_sysinfo_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void rp2040_sysinfo_init(Object *obj)
{
    RP2040SysInfoState *s = RP2040_SYSINFO(obj);

    memory_region_init_io(&s->iomem, obj, &rp2040_sysinfo_ops, s,
                          "rp2040.sysinfo", RP2040_SYSINFO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription rp2040_sysinfo_vmstate = {
    .name = TYPE_RP2040_SYSINFO,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static void rp2040_sysinfo_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &rp2040_sysinfo_vmstate;
}

static const TypeInfo rp2040_sysinfo_info = {
    .name          = TYPE_RP2040_SYSINFO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RP2040SysInfoState),
    .instance_init = rp2040_sysinfo_init,
    .class_init    = rp2040_sysinfo_class_init,
};

static void rp2040_sysinfo_register_types(void)
{
    type_register_static(&rp2040_sysinfo_info);
}
type_init(rp2040_sysinfo_register_types)
