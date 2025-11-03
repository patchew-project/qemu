/*
 * QEMU ATI SVGA emulation
 * 2D engine functions
 *
 * Copyright (c) 2019 BALATON Zoltan
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 */

#include "qemu/osdep.h"
#include "ati_int.h"
#include "ati_regs.h"
#include "qemu/log.h"
#include "ui/pixel_ops.h"
#include "ui/console.h"
#include "ui/rect.h"

/*
 * NOTE:
 * This is 2D _acceleration_ and supposed to be fast. Therefore, don't try to
 * reinvent the wheel (unlikely to get better with a naive implementation than
 * existing libraries) and avoid (poorly) reimplementing gfx primitives.
 * That is unnecessary and would become a performance problem. Instead, try to
 * map to and reuse existing optimised facilities (e.g. pixman) wherever
 * possible.
 */

static int ati_bpp_from_datatype(ATIVGAState *s)
{
    switch (s->regs.dp_datatype & 0xf) {
    case 2:
        return 8;
    case 3:
    case 4:
        return 16;
    case 5:
        return 24;
    case 6:
        return 32;
    default:
        qemu_log_mask(LOG_UNIMP, "Unknown dst datatype %d\n",
                      s->regs.dp_datatype & 0xf);
        return 0;
    }
}

#define DEFAULT_CNTL (s->regs.dp_gui_master_cntl & GMC_DST_PITCH_OFFSET_CNTL)
/* Convert 1bpp monochrome data to 32bpp ARGB using color expansion */
static void expand_colors(uint8_t *color_dst, const uint8_t *mono_src,
                          uint32_t width, uint32_t height,
                          uint32_t fg_color, uint32_t bg_color,
                          bool lsb_to_msb)
{
    uint32_t byte, color;
    uint8_t *pixel;
    int i, j, bit;
    /* Rows are 32-bit aligned */
    int bytes_per_row = ((width + 31) / 32) * 4;

    for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++) {
            byte = mono_src[i * bytes_per_row + (j / 8)];
            bit = lsb_to_msb ? 7 - (j % 8) : j % 8;
            color = (byte >> bit) & 0x1 ? fg_color : bg_color;
            pixel = &color_dst[(i * width + j) * 4];
            memcpy(pixel, &color, sizeof(color));
        }
    }
}

void ati_2d_blt(ATIVGAState *s)
{
    /* FIXME it is probably more complex than this and may need to be */
    /* rewritten but for now as a start just to get some output: */
    DisplaySurface *ds = qemu_console_surface(s->vga.con);
    DPRINTF("%p %u ds: %p %d %d rop: %x\n", s->vga.vram_ptr,
            s->vga.vbe_start_addr, surface_data(ds), surface_stride(ds),
            surface_bits_per_pixel(ds),
            (s->regs.dp_mix & GMC_ROP3_MASK) >> 16);

    QemuRect dst;
    {
        unsigned dst_width = s->regs.dst_width;
        unsigned dst_height = s->regs.dst_height;
        unsigned dst_x = (s->regs.dp_cntl & DST_X_LEFT_TO_RIGHT ?
                          s->regs.dst_x : s->regs.dst_x + 1 - dst_width);
        unsigned dst_y = (s->regs.dp_cntl & DST_Y_TOP_TO_BOTTOM ?
                          s->regs.dst_y : s->regs.dst_y + 1 - dst_height);
        qemu_rect_init(&dst, dst_x, dst_y, dst_width, dst_height);
    }

    QemuRect scissor;
    {
        uint16_t sc_left = s->regs.sc_top_left & 0x3fff;
        uint16_t sc_top = (s->regs.sc_top_left >> 16) & 0x3fff;
        uint16_t sc_right = s->regs.sc_bottom_right & 0x3fff;
        uint16_t sc_bottom = (s->regs.sc_bottom_right >> 16) & 0x3fff;
        qemu_rect_init(&scissor, sc_left, sc_top,
                       sc_right - sc_left + 1, sc_bottom - sc_top + 1);
    }

    QemuRect clipped;
    if (!qemu_rect_intersect(&dst, &scissor, &clipped)) {
        return;
    }
    uint32_t clip_left = clipped.x - dst.x;
    uint32_t clip_top = clipped.y - dst.y;

    int bpp = ati_bpp_from_datatype(s);
    if (!bpp) {
        qemu_log_mask(LOG_GUEST_ERROR, "Invalid bpp\n");
        return;
    }
    int dst_stride = DEFAULT_CNTL ? s->regs.dst_pitch : s->regs.default_pitch;
    if (!dst_stride) {
        qemu_log_mask(LOG_GUEST_ERROR, "Zero dest pitch\n");
        return;
    }
    uint8_t *dst_bits = s->vga.vram_ptr + (DEFAULT_CNTL ?
                        s->regs.dst_offset : s->regs.default_offset);

    if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
        dst_bits += s->regs.crtc_offset & 0x07ffffff;
        dst_stride *= bpp;
    }
    uint8_t *end = s->vga.vram_ptr + s->vga.vram_size;
    if (clipped.x > 0x3fff || clipped.y > 0x3fff || dst_bits >= end
        || dst_bits + clipped.x
         + (clipped.y + clipped.height) * dst_stride >= end) {
        qemu_log_mask(LOG_UNIMP, "blt outside vram not implemented\n");
        return;
    }
    DPRINTF("%d %d %d, %d %d %d, (%d,%d) -> (%d,%d) %dx%d %c %c\n",
            s->regs.src_offset, s->regs.dst_offset, s->regs.default_offset,
            s->regs.src_pitch, s->regs.dst_pitch, s->regs.default_pitch,
            s->regs.src_x, s->regs.src_y, dst.x, dst.y, dst.width, dst.height,
            (s->regs.dp_cntl & DST_X_LEFT_TO_RIGHT ? '>' : '<'),
            (s->regs.dp_cntl & DST_Y_TOP_TO_BOTTOM ? 'v' : '^'));
    switch (s->regs.dp_mix & GMC_ROP3_MASK) {
    case ROP3_SRCCOPY:
    {
        bool fallback = false;
        unsigned src_x = (s->regs.dp_cntl & DST_X_LEFT_TO_RIGHT ?
                         s->regs.src_x + clip_left :
                         s->regs.src_x + 1 - dst.width + clip_left);
        unsigned src_y = (s->regs.dp_cntl & DST_Y_TOP_TO_BOTTOM ?
                         s->regs.src_y + clip_top :
                         s->regs.src_y + 1 - dst.height + clip_top);
        int src_stride = DEFAULT_CNTL ?
                         s->regs.src_pitch : s->regs.default_pitch;
        if (!src_stride) {
            qemu_log_mask(LOG_GUEST_ERROR, "Zero source pitch\n");
            return;
        }
        uint8_t *src_bits = s->vga.vram_ptr + (DEFAULT_CNTL ?
                            s->regs.src_offset : s->regs.default_offset);

        if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
            src_bits += s->regs.crtc_offset & 0x07ffffff;
            src_stride *= bpp;
        }
        if (src_x > 0x3fff || src_y > 0x3fff || src_bits >= end
            || src_bits + src_x
             + (src_y + clipped.height) * src_stride >= end) {
            qemu_log_mask(LOG_UNIMP, "blt outside vram not implemented\n");
            return;
        }

        src_stride /= sizeof(uint32_t);
        dst_stride /= sizeof(uint32_t);
        DPRINTF("pixman_blt(%p, %p, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d)\n",
                src_bits, dst_bits, src_stride, dst_stride, bpp, bpp,
                src_x, src_y, clipped.x, clipped.y,
                clipped.width, clipped.height);
#ifdef CONFIG_PIXMAN
        if ((s->use_pixman & BIT(1)) &&
            s->regs.dp_cntl & DST_X_LEFT_TO_RIGHT &&
            s->regs.dp_cntl & DST_Y_TOP_TO_BOTTOM) {
            fallback = !pixman_blt((uint32_t *)src_bits, (uint32_t *)dst_bits,
                                   src_stride, dst_stride, bpp, bpp,
                                   src_x, src_y, clipped.x, clipped.y,
                                   clipped.width, clipped.height);
        } else if (s->use_pixman & BIT(1)) {
            /* FIXME: We only really need a temporary if src and dst overlap */
            int llb = clipped.width * (bpp / 8);
            int tmp_stride = DIV_ROUND_UP(llb, sizeof(uint32_t));
            uint32_t *tmp = g_malloc(tmp_stride * sizeof(uint32_t) *
                                     clipped.height);
            fallback = !pixman_blt((uint32_t *)src_bits, tmp,
                                   src_stride, tmp_stride, bpp, bpp,
                                   src_x, src_y, 0, 0,
                                   clipped.width, clipped.height);
            if (!fallback) {
                fallback = !pixman_blt(tmp, (uint32_t *)dst_bits,
                                       tmp_stride, dst_stride, bpp, bpp,
                                       0, 0, clipped.x, clipped.y,
                                       clipped.width, clipped.height);
            }
            g_free(tmp);
        } else
#endif
        {
            fallback = true;
        }
        if (fallback) {
            unsigned int y, i, j, bypp = bpp / 8;
            unsigned int src_pitch = src_stride * sizeof(uint32_t);
            unsigned int dst_pitch = dst_stride * sizeof(uint32_t);

            for (y = 0; y < clipped.height; y++) {
                i = clipped.x * bypp;
                j = src_x * bypp;
                if (s->regs.dp_cntl & DST_Y_TOP_TO_BOTTOM) {
                    i += (clipped.y + y) * dst_pitch;
                    j += (src_y + y) * src_pitch;
                } else {
                    i += (clipped.y + clipped.height - 1 - y) * dst_pitch;
                    j += (src_y + clipped.height - 1 - y) * src_pitch;
                }
                memmove(&dst_bits[i], &src_bits[j], clipped.width * bypp);
            }
        }
        if (dst_bits >= s->vga.vram_ptr + s->vga.vbe_start_addr &&
            dst_bits < s->vga.vram_ptr + s->vga.vbe_start_addr +
            s->vga.vbe_regs[VBE_DISPI_INDEX_YRES] * s->vga.vbe_line_offset) {
            memory_region_set_dirty(&s->vga.vram, s->vga.vbe_start_addr +
                                    s->regs.dst_offset +
                                    clipped.y * surface_stride(ds),
                                    clipped.height * surface_stride(ds));
        }
        s->regs.dst_x = (s->regs.dp_cntl & DST_X_LEFT_TO_RIGHT ?
                         clipped.x + clipped.width : clipped.x);
        s->regs.dst_y = (s->regs.dp_cntl & DST_Y_TOP_TO_BOTTOM ?
                         clipped.y + clipped.height : clipped.y);
        break;
    }
    case ROP3_PATCOPY:
    case ROP3_BLACKNESS:
    case ROP3_WHITENESS:
    {
        uint32_t filler = 0;

        switch (s->regs.dp_mix & GMC_ROP3_MASK) {
        case ROP3_PATCOPY:
            filler = s->regs.dp_brush_frgd_clr;
            break;
        case ROP3_BLACKNESS:
            filler = 0xffUL << 24 | rgb_to_pixel32(s->vga.palette[0],
                     s->vga.palette[1], s->vga.palette[2]);
            break;
        case ROP3_WHITENESS:
            filler = 0xffUL << 24 | rgb_to_pixel32(s->vga.palette[3],
                     s->vga.palette[4], s->vga.palette[5]);
            break;
        }

        dst_stride /= sizeof(uint32_t);
        DPRINTF("pixman_fill(%p, %d, %d, %d, %d, %d, %d, %x)\n",
                dst_bits, dst_stride, bpp, clipped.x, clipped.y,
                clipped.width, clipped.height, filler);
#ifdef CONFIG_PIXMAN
        if (!(s->use_pixman & BIT(0)) ||
            !pixman_fill((uint32_t *)dst_bits, dst_stride, bpp,
                         clipped.x, clipped.y, clipped.width, clipped.height,
                         filler))
#endif
        {
            /* fallback when pixman failed or we don't want to call it */
            unsigned int x, y, i, bypp = bpp / 8;
            unsigned int dst_pitch = dst_stride * sizeof(uint32_t);
            for (y = 0; y < clipped.height; y++) {
                i = clipped.x * bypp + (clipped.y + y) * dst_pitch;
                for (x = 0; x < clipped.width; x++, i += bypp) {
                    stn_he_p(&dst_bits[i], bypp, filler);
                }
            }
        }
        if (dst_bits >= s->vga.vram_ptr + s->vga.vbe_start_addr &&
            dst_bits < s->vga.vram_ptr + s->vga.vbe_start_addr +
            s->vga.vbe_regs[VBE_DISPI_INDEX_YRES] * s->vga.vbe_line_offset) {
            memory_region_set_dirty(&s->vga.vram, s->vga.vbe_start_addr +
                                    s->regs.dst_offset +
                                    clipped.y * surface_stride(ds),
                                    clipped.height * surface_stride(ds));
        }
        s->regs.dst_y = (s->regs.dp_cntl & DST_Y_TOP_TO_BOTTOM ?
                         clipped.y + clipped.height : clipped.y);
        break;
    }
    default:
        qemu_log_mask(LOG_UNIMP, "Unimplemented ati_2d blt op %x\n",
                      (s->regs.dp_mix & GMC_ROP3_MASK) >> 16);
    }
}
