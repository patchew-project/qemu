/*
 * QEMU ATI SVGA emulation
 * 2D engine functions
 *
 * Copyright (c) 2019 BALATON Zoltan
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 */

#include "ati_int.h"
#include "ati_regs.h"
#include "qemu/log.h"

/*
 * NOTE:
 * This is 2D _acceleration_ and supposed to be fast. Therefore, don't try to
 * reinvent the wheel (unlikely to get better with a naive implementation than
 * existing libraries) and avoid (poorly) reimplementing gfx primitives.
 * That is unnecessary and would become a performance problem. Instead, try to
 * map to and reuse existing optimised facilities (e.g. pixman) wherever
 * possible.
 */

void ati_2d_blit(ATIVGAState *s)
{
    /* FIXME it is really much more complex than this and may need to be */
    /* rewritten but for now as a start just to get some output: */
    DisplaySurface *ds = qemu_console_surface(s->vga.con);
    DPRINTF("ds: %p %d %d rop: %x\n", ds->image,
            surface_stride(ds), surface_bits_per_pixel(ds),
            (s->regs.dp_mix & GMC_ROP3_MASK) >> 16);
    DPRINTF("%d %d, %d %d, (%d,%d) -> (%d,%d) %dx%d\n", s->regs.src_offset,
            s->regs.dst_offset, s->regs.src_pitch, s->regs.dst_pitch,
            s->regs.src_x, s->regs.src_y, s->regs.dst_x, s->regs.dst_y,
            s->regs.dst_width, s->regs.dst_height);
    switch ((s->regs.dp_mix & GMC_ROP3_MASK)) {
    case ROP3_SRCCOPY:
    {
        int src_stride = (s->regs.src_pitch & 0x3fff) / 4;
        int dst_stride = (s->regs.dst_pitch & 0x3fff) / 4;
        uint32_t *src_bits, *dst_bits;
        src_bits = dst_bits = pixman_image_get_data(ds->image);
        src_bits += s->regs.src_offset;
        dst_bits += s->regs.dst_offset;
        pixman_blt(src_bits, dst_bits, src_stride, dst_stride,
                   surface_bits_per_pixel(ds), surface_bits_per_pixel(ds),
                   s->regs.src_x, s->regs.src_y,
                   s->regs.dst_x, s->regs.dst_y,
                   s->regs.dst_width, s->regs.dst_height);
        memory_region_set_dirty(&s->vga.vram,
                                s->vga.vbe_start_addr + s->regs.dst_offset +
                                s->regs.dst_y * surface_stride(ds),
                                s->regs.dst_height * surface_stride(ds));
        break;
    }
    case ROP3_PATCOPY:
    {
        int dst_stride = (s->regs.dst_pitch & 0x3fff) / 4;
        uint32_t *dst_bits = pixman_image_get_data(ds->image);
        dst_bits += s->regs.dst_offset;
        pixman_fill(dst_bits, dst_stride, surface_bits_per_pixel(ds),
                   s->regs.dst_x, s->regs.dst_y,
                   s->regs.dst_width, s->regs.dst_height,
                   s->regs.dp_brush_frgd_clr);
        memory_region_set_dirty(&s->vga.vram,
                                s->vga.vbe_start_addr + s->regs.dst_offset +
                                s->regs.dst_y * surface_stride(ds),
                                s->regs.dst_height * surface_stride(ds));
        break;
    }
    default:
        qemu_log_mask(LOG_UNIMP, "Unimplemented ati_2d blit op %x\n",
                      (s->regs.dp_mix & GMC_ROP3_MASK) >> 16);
    }
}
