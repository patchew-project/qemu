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
    /* FIXME it is really much more complex than this and will need to be */
    /* rewritten but for now as a start just to get some console output: */
    if ((s->regs.dp_gui_master_cntl & GMC_ROP3_MASK) == ROP3_SRCCOPY) {
        DisplaySurface *ds = qemu_console_surface(s->vga.con);
        DPRINTF("%p %p, %d %d, (%d,%d) -> (%d,%d) %dx%d\n", ds->image,
                ds->image, surface_stride(ds), surface_bits_per_pixel(ds),
                s->regs.src_x, s->regs.src_y, s->regs.dst_x, s->regs.dst_y,
                s->regs.dst_width, s->regs.dst_height);
        pixman_image_composite(PIXMAN_OP_SRC, ds->image, NULL, ds->image,
                               s->regs.src_x, s->regs.src_y, 0, 0,
                               s->regs.dst_x, s->regs.dst_y,
                               s->regs.dst_width, s->regs.dst_height);
        memory_region_set_dirty(&s->vga.vram, s->vga.vbe_start_addr +
                                s->regs.dst_y * surface_stride(ds),
                                s->regs.dst_height * surface_stride(ds));
    } else {
        qemu_log_mask(LOG_UNIMP, "Unimplemented ati_2d blit op");
    }
}
