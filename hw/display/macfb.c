/*
 * QEMU Motorola 680x0 Macintosh Video Card Emulation
 *                 Copyright (c) 2012-2018 Laurent Vivier
 *
 * some parts from QEMU G364 framebuffer Emulator.
 *                 Copyright (c) 2007-2011 Herve Poussineau
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "ui/console.h"
#include "ui/pixel_ops.h"
#include "hw/display/macfb.h"

#define VIDEO_BASE 0x00001000
#define DAFB_BASE  0x00800000

#define MACFB_PAGE_SIZE 4096
#define MACFB_VRAM_SIZE (4 * 1024 * 1024)

#define DAFB_RESET      0x200
#define DAFB_LUT        0x213

#include "macfb-template.h"

static int macfb_check_dirty(MacfbState *s, DirtyBitmapSnapshot *snap,
                             ram_addr_t addr, int len)
{
    return memory_region_snapshot_get_dirty(&s->mem_vram, snap, addr, len);
}

static void macfb_draw_graphic(MacfbState *s)
{
    DisplaySurface *surface = qemu_console_surface(s->con);
    DirtyBitmapSnapshot *snap = NULL;
    ram_addr_t page;
    int y, ymin;
    int macfb_stride = (s->depth * s->width + 7) / 8;
    macfb_draw_line_func_t draw_line;

    if (s->depth > 24) {
        hw_error("macfb: unknown guest depth %d", s->depth);
        return;
    }
    if (surface_bits_per_pixel(surface) > 32) {
        hw_error("macfb: unknown host depth %d",
                 surface_bits_per_pixel(surface));
        return;
    }
    draw_line = macfb_draw_line[s->depth - 1][surface_bits_per_pixel(surface)
                                              - 1];

    if (draw_line == NULL) {
        hw_error("macfb: unknown guest/host depth combination %d/%d", s->depth,
                 surface_bits_per_pixel(surface));
        return;
    }

    snap = memory_region_snapshot_and_clear_dirty(&s->mem_vram, 0x0,
                                             memory_region_size(&s->mem_vram),
                                             DIRTY_MEMORY_VGA);

    ymin = -1;
    page = 0;
    for (y = 0; y < s->height; y++, page += macfb_stride) {
        if (macfb_check_dirty(s, snap, page, macfb_stride)) {
            uint8_t *data_display;

            data_display = surface_data(surface) + y * surface_stride(surface);
            draw_line(s, data_display, s->vram + page, s->width);

            if (ymin < 0) {
                ymin = y;
            }
        } else {
            if (ymin >= 0) {
                dpy_gfx_update(s->con, 0, ymin, s->width, y - ymin);
                ymin = -1;
            }
        }
    }

    if (ymin >= 0) {
        dpy_gfx_update(s->con, 0, ymin, s->width, y - ymin);
    }

    g_free(snap);
}

static void macfb_invalidate_display(void *opaque)
{
    MacfbState *s = opaque;

    memory_region_set_dirty(&s->mem_vram, 0, MACFB_VRAM_SIZE);
}

static void macfb_update_display(void *opaque)
{
    MacfbState *s = opaque;
    DisplaySurface *surface = qemu_console_surface(s->con);

    qemu_flush_coalesced_mmio_buffer();

    if (s->width == 0 || s->height == 0) {
        return;
    }

    if (s->width != surface_width(surface) ||
        s->height != surface_height(surface)) {
        qemu_console_resize(s->con, s->width, s->height);
    }

    macfb_draw_graphic(s);
}

static void macfb_reset(MacfbState *s)
{
    int i;

    s->palette_current = 0;
    for (i = 0; i < 256; i++) {
        s->color_palette[i * 3] = 255 - i;
        s->color_palette[i * 3 + 1] = 255 - i;
        s->color_palette[i * 3 + 2] = 255 - i;
    }
    memset(s->vram, 0, MACFB_VRAM_SIZE);
    macfb_invalidate_display(s);
}

static uint64_t macfb_ctrl_read(void *opaque,
                                hwaddr addr,
                                unsigned int size)
{
    return 0;
}

static void macfb_ctrl_write(void *opaque,
                             hwaddr addr,
                             uint64_t val,
                             unsigned int size)
{
    MacfbState *s = opaque;
    switch (addr) {
    case DAFB_RESET:
        s->palette_current = 0;
        break;
    case DAFB_LUT:
        s->color_palette[s->palette_current++] = val;
        if (s->palette_current % 3) {
            macfb_invalidate_display(s);
        }
        break;
    }
}

static const MemoryRegionOps macfb_ctrl_ops = {
    .read = macfb_ctrl_read,
    .write = macfb_ctrl_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .impl.min_access_size = 1,
    .impl.max_access_size = 4,
};

static int macfb_post_load(void *opaque, int version_id)
{
    macfb_invalidate_display(opaque);
    return 0;
}

static const VMStateDescription vmstate_macfb = {
    .name = "macfb",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .post_load = macfb_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY(color_palette, MacfbState, 256 * 3),
        VMSTATE_UINT32(palette_current, MacfbState),
        VMSTATE_END_OF_LIST()
    }
};

static const GraphicHwOps macfb_ops = {
    .invalidate = macfb_invalidate_display,
    .gfx_update = macfb_update_display,
};

static void macfb_common_realize(DeviceState *dev, MacfbState *s)
{
    s->vram = g_malloc0(MACFB_VRAM_SIZE);

    s->con = graphic_console_init(dev, 0, &macfb_ops, s);

    memory_region_init_io(&s->mem_ctrl, NULL, &macfb_ctrl_ops, s, "macfb-ctrl",
                          0x1000);
    memory_region_init_ram_ptr(&s->mem_vram, NULL, "macfb-vram",
                               MACFB_VRAM_SIZE, s->vram);
    vmstate_register_ram(&s->mem_vram, dev);
    memory_region_set_coalescing(&s->mem_vram);
}

static void macfb_sysbus_realize(DeviceState *dev, Error **errp)
{
    MacfbSysBusState *s = MACFB(dev);
    MacfbState *ms = &s->macfb;

    macfb_common_realize(dev, ms);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &ms->mem_ctrl);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &ms->mem_vram);
}

static void macfb_sysbus_reset(DeviceState *d)
{
    MacfbSysBusState *s = MACFB(d);
    macfb_reset(&s->macfb);
}

static Property macfb_sysbus_properties[] = {
    DEFINE_PROP_UINT32("width", MacfbSysBusState, macfb.width, 640),
    DEFINE_PROP_UINT32("height", MacfbSysBusState, macfb.height, 480),
    DEFINE_PROP_UINT8("depth", MacfbSysBusState, macfb.depth, 8),
    DEFINE_PROP_END_OF_LIST(),
};

static void macfb_sysbus_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = macfb_sysbus_realize;
    dc->desc = "SysBus Macintosh framebuffer";
    dc->reset = macfb_sysbus_reset;
    dc->vmsd = &vmstate_macfb;
    dc->props = macfb_sysbus_properties;
}

static TypeInfo macfb_sysbus_info = {
    .name          = TYPE_MACFB,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MacfbSysBusState),
    .class_init    = macfb_sysbus_class_init,
};

static void macfb_register_types(void)
{
    type_register_static(&macfb_sysbus_info);
}

type_init(macfb_register_types)
