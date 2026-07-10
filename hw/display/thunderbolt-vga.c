/*
 * Thunderbolt hotpluggable PCI VGA device.
 *
 * This is a stdvga-compatible endpoint with a separate QOM type for
 * Thunderbolt hotplug experiments.  The regular "VGA" type remains unchanged.
 *
 * Copyright (c) 2003 Fabrice Bellard
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include "hw/core/loader.h"
#include "hw/core/qdev-properties.h"
#include "hw/display/edid.h"
#include "hw/pci/pci_device.h"
#include "qemu/module.h"
#include "qom/object.h"
#include "ui/console.h"
#include "ui/pixel_ops.h"
#include "vga_int.h"

#define TYPE_PCI_VGA "pci-vga"
#define TYPE_THUNDERBOLT_VGA "thunderbolt-vga"

OBJECT_DECLARE_SIMPLE_TYPE(ThunderboltVGAState, THUNDERBOLT_VGA)

enum thunderbolt_vga_pci_flags {
    THUNDERBOLT_VGA_FLAG_ENABLE_MMIO = 1,
    THUNDERBOLT_VGA_FLAG_ENABLE_QEXT = 2,
    THUNDERBOLT_VGA_FLAG_ENABLE_EDID = 3,
};

struct ThunderboltVGAState {
    PCIDevice dev;
    VGACommonState vga;
    uint32_t flags;
    qemu_edid_info edid_info;
    MemoryRegion mmio;
    MemoryRegion mrs[4];
    uint8_t edid[384];
};

static uint64_t thunderbolt_vga_ioport_read(void *ptr, hwaddr addr,
                                            unsigned size)
{
    VGACommonState *s = ptr;
    uint64_t ret = 0;

    switch (size) {
    case 1:
        ret = vga_ioport_read(s, addr + 0x3c0);
        break;
    case 2:
        ret  = vga_ioport_read(s, addr + 0x3c0);
        ret |= vga_ioport_read(s, addr + 0x3c1) << 8;
        break;
    }
    return ret;
}

static void thunderbolt_vga_ioport_write(void *ptr, hwaddr addr,
                                         uint64_t val, unsigned size)
{
    VGACommonState *s = ptr;

    switch (size) {
    case 1:
        vga_ioport_write(s, addr + 0x3c0, val);
        break;
    case 2:
        vga_ioport_write(s, addr + 0x3c0, val & 0xff);
        vga_ioport_write(s, addr + 0x3c1, (val >> 8) & 0xff);
        break;
    }
}

static const MemoryRegionOps thunderbolt_vga_ioport_ops = {
    .read = thunderbolt_vga_ioport_read,
    .write = thunderbolt_vga_ioport_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .impl.min_access_size = 1,
    .impl.max_access_size = 2,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t thunderbolt_vga_bochs_read(void *ptr, hwaddr addr,
                                           unsigned size)
{
    VGACommonState *s = ptr;
    int index = addr >> 1;

    vbe_ioport_write_index(s, 0, index);
    return vbe_ioport_read_data(s, 0);
}

static void thunderbolt_vga_bochs_write(void *ptr, hwaddr addr,
                                        uint64_t val, unsigned size)
{
    VGACommonState *s = ptr;
    int index = addr >> 1;

    vbe_ioport_write_index(s, 0, index);
    vbe_ioport_write_data(s, 0, val);
}

static const MemoryRegionOps thunderbolt_vga_bochs_ops = {
    .read = thunderbolt_vga_bochs_read,
    .write = thunderbolt_vga_bochs_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .impl.min_access_size = 2,
    .impl.max_access_size = 2,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t thunderbolt_vga_qext_read(void *ptr, hwaddr addr,
                                          unsigned size)
{
    VGACommonState *s = ptr;

    switch (addr) {
    case PCI_VGA_QEXT_REG_SIZE:
        return PCI_VGA_QEXT_SIZE;
    case PCI_VGA_QEXT_REG_BYTEORDER:
        return s->big_endian_fb ?
            PCI_VGA_QEXT_BIG_ENDIAN : PCI_VGA_QEXT_LITTLE_ENDIAN;
    default:
        return 0;
    }
}

static void thunderbolt_vga_qext_write(void *ptr, hwaddr addr,
                                       uint64_t val, unsigned size)
{
    VGACommonState *s = ptr;

    switch (addr) {
    case PCI_VGA_QEXT_REG_BYTEORDER:
        if (val == PCI_VGA_QEXT_BIG_ENDIAN) {
            s->big_endian_fb = true;
        }
        if (val == PCI_VGA_QEXT_LITTLE_ENDIAN) {
            s->big_endian_fb = false;
        }
        break;
    }
}

static const MemoryRegionOps thunderbolt_vga_qext_ops = {
    .read = thunderbolt_vga_qext_read,
    .write = thunderbolt_vga_qext_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static bool thunderbolt_vga_get_big_endian_fb(Object *obj, Error **errp)
{
    ThunderboltVGAState *d = THUNDERBOLT_VGA(obj);

    return d->vga.big_endian_fb;
}

static void thunderbolt_vga_set_big_endian_fb(Object *obj, bool value,
                                              Error **errp)
{
    ThunderboltVGAState *d = THUNDERBOLT_VGA(obj);

    d->vga.big_endian_fb = value;
}

static void thunderbolt_vga_mmio_region_init(ThunderboltVGAState *d,
                                             Object *owner,
                                             MemoryRegion *parent)
{
    VGACommonState *s = &d->vga;

    memory_region_init_io(&d->mrs[0], owner, &thunderbolt_vga_ioport_ops, s,
                          "vga ioports remapped", PCI_VGA_IOPORT_SIZE);
    memory_region_add_subregion(parent, PCI_VGA_IOPORT_OFFSET, &d->mrs[0]);

    memory_region_init_io(&d->mrs[1], owner, &thunderbolt_vga_bochs_ops, s,
                          "bochs dispi interface", PCI_VGA_BOCHS_SIZE);
    memory_region_add_subregion(parent, PCI_VGA_BOCHS_OFFSET, &d->mrs[1]);

    if (d->flags & (1 << THUNDERBOLT_VGA_FLAG_ENABLE_QEXT)) {
        memory_region_init_io(&d->mrs[2], owner, &thunderbolt_vga_qext_ops, s,
                              "qemu extended regs", PCI_VGA_QEXT_SIZE);
        memory_region_add_subregion(parent, PCI_VGA_QEXT_OFFSET, &d->mrs[2]);
    }

    if (d->flags & (1 << THUNDERBOLT_VGA_FLAG_ENABLE_EDID)) {
        qemu_edid_generate(d->edid, sizeof(d->edid), &d->edid_info);
        qemu_edid_region_io(&d->mrs[3], owner, d->edid, sizeof(d->edid));
        memory_region_add_subregion(parent, 0, &d->mrs[3]);
    }
}

static void thunderbolt_vga_realize(PCIDevice *dev, Error **errp)
{
    ThunderboltVGAState *d = THUNDERBOLT_VGA(dev);
    VGACommonState *s = &d->vga;

    if (!vga_common_init(s, OBJECT(dev), errp)) {
        return;
    }
    vga_init(s, OBJECT(dev), pci_address_space(dev), pci_address_space_io(dev),
             true);

    s->con = qemu_graphic_console_create(DEVICE(dev), 0, s->hw_ops, s);

    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_MEM_PREFETCH, &s->vram);

    if (d->flags & (1 << THUNDERBOLT_VGA_FLAG_ENABLE_MMIO)) {
        memory_region_init_io(&d->mmio, OBJECT(dev), &unassigned_io_ops, NULL,
                              "vga.mmio", PCI_VGA_MMIO_SIZE);

        if (d->flags & (1 << THUNDERBOLT_VGA_FLAG_ENABLE_QEXT)) {
            pci_set_byte(&dev->config[PCI_REVISION_ID], 2);
        }

        thunderbolt_vga_mmio_region_init(d, OBJECT(dev), &d->mmio);
        pci_register_bar(dev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &d->mmio);
    }
}

static const Property thunderbolt_vga_properties[] = {
    DEFINE_PROP_UINT32("vgamem_mb", ThunderboltVGAState, vga.vram_size_mb, 4),
    DEFINE_PROP_BIT("mmio", ThunderboltVGAState, flags,
                    THUNDERBOLT_VGA_FLAG_ENABLE_MMIO, true),
    DEFINE_PROP_BIT("qemu-extended-regs", ThunderboltVGAState, flags,
                    THUNDERBOLT_VGA_FLAG_ENABLE_QEXT, true),
    DEFINE_PROP_BIT("edid", ThunderboltVGAState, flags,
                    THUNDERBOLT_VGA_FLAG_ENABLE_EDID, true),
    DEFINE_PROP_UINT32("xres", ThunderboltVGAState, edid_info.prefx, 1280),
    DEFINE_PROP_UINT32("yres", ThunderboltVGAState, edid_info.prefy, 800),
    DEFINE_PROP_UINT32("xmax", ThunderboltVGAState, edid_info.maxx, 1280),
    DEFINE_PROP_UINT32("ymax", ThunderboltVGAState, edid_info.maxy, 800),
    DEFINE_PROP_UINT32("refresh_rate", ThunderboltVGAState,
                       edid_info.refresh_rate, 60000),
    DEFINE_PROP_BOOL("global-vmstate", ThunderboltVGAState,
                     vga.global_vmstate, false),
};

static void thunderbolt_vga_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = thunderbolt_vga_realize;
    k->romfile = "vgabios-stdvga.bin";
    k->class_id = PCI_CLASS_DISPLAY_VGA;
    device_class_set_props(dc, thunderbolt_vga_properties);
    dc->desc = "Thunderbolt hotpluggable VGA";
    dc->hotpluggable = true;

    object_class_property_add_bool(klass, "big-endian-framebuffer",
                                   thunderbolt_vga_get_big_endian_fb,
                                   thunderbolt_vga_set_big_endian_fb);
}

static const TypeInfo thunderbolt_vga_info = {
    .name = TYPE_THUNDERBOLT_VGA,
    .parent = TYPE_PCI_VGA,
    .instance_size = sizeof(ThunderboltVGAState),
    .class_init = thunderbolt_vga_class_init,
};

static void thunderbolt_vga_register_types(void)
{
    type_register_static(&thunderbolt_vga_info);
}

type_init(thunderbolt_vga_register_types)
