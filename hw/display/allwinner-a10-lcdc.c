/*
 * Allwinner A10 LCD Control Module emulation
 *
 * Copyright (C) 2023 Strahinja Jankovic <strahinja.p.jankovic@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "hw/qdev-properties.h"
#include "qemu/module.h"
#include "hw/display/allwinner-a10-lcdc.h"
#include "hw/irq.h"
#include "ui/pixel_ops.h"
#include "trace.h"
#include "sysemu/dma.h"
#include "framebuffer.h"

/* LCDC register offsets */
enum {
    REG_TCON_GCTL       = 0x0000, /* TCON Global control register */
    REG_TCON_GINT0      = 0x0004, /* TCON Global interrupt register 0 */
};

/* TCON_GCTL register fields */
#define REG_TCON_GCTL_EN        (1 << 31)

/* TCON_GINT0 register fields */
#define REG_TCON_GINT0_VB_INT_EN    (1 << 31)
#define REG_TCON_GINT0_VB_INT_FLAG  (1 << 14)

#define REG_INDEX(offset)    (offset / sizeof(uint32_t))

static void allwinner_a10_lcdc_tick(void *opaque)
{
    AwA10LcdcState *s = AW_A10_LCDC(opaque);

    if (s->regs[REG_INDEX(REG_TCON_GINT0)] & REG_TCON_GINT0_VB_INT_EN) {
        s->regs[REG_INDEX(REG_TCON_GINT0)] |= REG_TCON_GINT0_VB_INT_FLAG;
        qemu_irq_raise(s->irq);
    }
}

static uint64_t allwinner_a10_lcdc_read(void *opaque, hwaddr offset,
                                       unsigned size)
{
    AwA10LcdcState *s = AW_A10_LCDC(opaque);
    const uint32_t idx = REG_INDEX(offset);
    uint32_t val = s->regs[idx];

    switch (offset) {
    case 0x800 ... AW_A10_LCDC_IOSIZE:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                  __func__, (uint32_t)offset);
        return 0;
    default:
        break;
    }

    trace_allwinner_a10_lcdc_read(offset, val);

    return val;
}

static void allwinner_a10_lcdc_write(void *opaque, hwaddr offset,
                                   uint64_t val, unsigned size)
{
    AwA10LcdcState *s = AW_A10_LCDC(opaque);
    const uint32_t idx = REG_INDEX(offset);

    switch (offset) {
    case REG_TCON_GCTL:
        s->is_enabled = !!REG_TCON_GCTL_EN;
        break;
    case REG_TCON_GINT0:
        if (0 == (val & REG_TCON_GINT0_VB_INT_FLAG)) {
            qemu_irq_lower(s->irq);
        }
        break;
    case 0x800 ... AW_A10_LCDC_IOSIZE:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                  __func__, (uint32_t)offset);
        break;
    default:
        break;
    }

    trace_allwinner_a10_lcdc_write(offset, (uint32_t)val);

    s->regs[idx] = (uint32_t) val;
}

static const MemoryRegionOps allwinner_a10_lcdc_ops = {
    .read = allwinner_a10_lcdc_read,
    .write = allwinner_a10_lcdc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .impl.min_access_size = 1,
};

#define COPY_PIXEL(to, from) do { *(uint32_t *)to = from; to += 4; } while (0)

static void draw_line(void *opaque, uint8_t *d, const uint8_t *src,
                      int width, int deststep)
{
    uint32_t data;
    unsigned int r, g, b;
    while (width > 0) {
        data = *(uint32_t *)src;
        b = data & 0xff;
        g = (data >> 8) & 0xff;
        r = (data >> 16) & 0xff;
        COPY_PIXEL(d, rgb_to_pixel32(r, g, b));
        width--;
        src += 4;
    }
}

static void allwinner_a10_lcdc_invalidate_display(void *opaque)
{
    AwA10LcdcState *s = AW_A10_LCDC(opaque);
    qemu_console_resize(s->con, s->debe->width, s->debe->height);
    s->invalidate = 1;
}

static void allwinner_a10_lcdc_update_display(void *opaque)
{
    AwA10LcdcState *s = AW_A10_LCDC(opaque);
    DisplaySurface *surface;
    int step, width, height, linesize, first = 0, last;

    if (!s->is_enabled || !s->debe->ready) {
        return;
    }

    width = s->debe->width;
    height = s->debe->height;
    step = width * (s->debe->bpp >> 3);

    if (s->debe->invalidate) {
        allwinner_a10_lcdc_invalidate_display(opaque);
        s->debe->invalidate = false;
    }

    surface = qemu_console_surface(s->con);
    linesize = surface_stride(surface);

    if (s->invalidate) {
        framebuffer_update_memory_section(&s->fbsection,
                                    sysbus_address_space(SYS_BUS_DEVICE(s)),
                                    s->debe->framebuffer_offset,
                                    height, step);
    }

    framebuffer_update_display(surface, &s->fbsection,
                               width, height,
                               step, linesize, 0,
                               s->invalidate,
                               draw_line, NULL,
                               &first, &last);

    trace_allwinner_a10_draw(first, last, s->invalidate);

    if (first >= 0) {
        dpy_gfx_update(s->con, 0, first, width, last - first + 1);
    }
    s->invalidate = 0;

}

static const GraphicHwOps allwinner_a10_lcdc_gfx_ops = {
    .invalidate  = allwinner_a10_lcdc_invalidate_display,
    .gfx_update  = allwinner_a10_lcdc_update_display,
};

static void allwinner_a10_lcdc_reset_enter(Object *obj, ResetType type)
{
    AwA10LcdcState *s = AW_A10_LCDC(obj);
    s->invalidate = 1;
}

static void allwinner_a10_lcdc_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    AwA10LcdcState *s = AW_A10_LCDC(obj);

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &allwinner_a10_lcdc_ops, s,
                          TYPE_AW_A10_LCDC, AW_A10_LCDC_IOSIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    s->invalidate = 1;
    s->is_enabled = 0;
}

static void allwinner_a10_lcdc_realize(DeviceState *dev, Error **errp)
{
    AwA10LcdcState *s = AW_A10_LCDC(dev);

    s->timer = ptimer_init(allwinner_a10_lcdc_tick, s,
                           PTIMER_POLICY_NO_IMMEDIATE_TRIGGER |
                           PTIMER_POLICY_NO_IMMEDIATE_RELOAD |
                           PTIMER_POLICY_NO_COUNTER_ROUND_DOWN);

    ptimer_transaction_begin(s->timer);
    /* Set to 60Hz */
    ptimer_set_freq(s->timer, 60);
    ptimer_set_limit(s->timer, 0x1, 1);
    ptimer_run(s->timer, 0);
    ptimer_transaction_commit(s->timer);

    s->invalidate = 1;
    s->con = graphic_console_init(NULL, 0, &allwinner_a10_lcdc_gfx_ops, s);
    qemu_console_resize(s->con, s->debe->width, s->debe->height);
}

static const VMStateDescription allwinner_a10_lcdc_vmstate = {
    .name = "allwinner-a10_lcdc",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AwA10LcdcState, AW_A10_LCDC_REGS_NUM),
        VMSTATE_END_OF_LIST()
    }
};

static Property allwinner_a10_lcdc_properties[] = {
    DEFINE_PROP_LINK("debe", AwA10LcdcState, debe,
                     TYPE_AW_A10_DEBE, AwA10DEBEState *),
    DEFINE_PROP_END_OF_LIST(),
};

static void allwinner_a10_lcdc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.enter = allwinner_a10_lcdc_reset_enter;
    dc->vmsd = &allwinner_a10_lcdc_vmstate;
    dc->realize = allwinner_a10_lcdc_realize;
    device_class_set_props(dc, allwinner_a10_lcdc_properties);
}

static const TypeInfo allwinner_a10_lcdc_info = {
    .name          = TYPE_AW_A10_LCDC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = allwinner_a10_lcdc_init,
    .instance_size = sizeof(AwA10LcdcState),
    .class_init    = allwinner_a10_lcdc_class_init,
};

static void allwinner_a10_lcdc_register(void)
{
    type_register_static(&allwinner_a10_lcdc_info);
}

type_init(allwinner_a10_lcdc_register)
