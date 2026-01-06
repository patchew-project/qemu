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
    switch (s->regs.dp_dst_datatype) {
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
                      s->regs.dp_dst_datatype);
        return 0;
    }
}

static QemuRect dst_rect(ATIVGAState *s)
{
    QemuRect dst;
    unsigned dst_x = (s->regs.dp_cntl & DST_X_LEFT_TO_RIGHT ?
                     s->regs.dst_x :
                     s->regs.dst_x + 1 - s->regs.dst_width);
    unsigned dst_y = (s->regs.dp_cntl & DST_Y_TOP_TO_BOTTOM ?
                     s->regs.dst_y :
                     s->regs.dst_y + 1 - s->regs.dst_height);
    qemu_rect_init(&dst, dst_x, dst_y, s->regs.dst_width, s->regs.dst_height);
    return dst;
}

typedef struct {
    QemuRect rect;
    QemuRect visible;
    uint32_t src_left_offset;
    uint32_t src_top_offset;
    int bpp;
    int stride;
    bool top_to_bottom;
    bool left_to_right;
    bool valid;
    uint8_t *bits;
} ATIBlitDest;

static ATIBlitDest setup_2d_blt_dst(ATIVGAState *s)
{
    ATIBlitDest dst = { .valid = false };
    uint8_t *end = s->vga.vram_ptr + s->vga.vram_size;
    QemuRect scissor;
    qemu_rect_init(&scissor,
                   s->regs.sc_left, s->regs.sc_top,
                   s->regs.sc_right - s->regs.sc_left + 1,
                   s->regs.sc_bottom - s->regs.sc_top + 1);

    dst.rect = dst_rect(s);
    if (!qemu_rect_intersect(&dst.rect, &scissor, &dst.visible)) {
        /* Destination is completely clipped, nothing to draw */
        return dst;
    }
    dst.bpp = ati_bpp_from_datatype(s);
    if (!dst.bpp) {
        qemu_log_mask(LOG_GUEST_ERROR, "Invalid bpp\n");
        return dst;
    }
    dst.stride = s->regs.dst_pitch;
    if (!dst.stride) {
        qemu_log_mask(LOG_GUEST_ERROR, "Zero dest pitch\n");
        return dst;
    }
    dst.bits = s->vga.vram_ptr + s->regs.dst_offset;
    if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
        dst.bits += s->regs.crtc_offset & 0x07ffffff;
        dst.stride *= dst.bpp;
    }
    if (dst.visible.x > 0x3fff || dst.visible.y > 0x3fff || dst.bits >= end
        || dst.bits + dst.visible.x
         + (dst.visible.y + dst.visible.height) * dst.stride >= end) {
        qemu_log_mask(LOG_UNIMP, "blt outside vram not implemented\n");
        return dst;
    }
    dst.src_left_offset = dst.visible.x - dst.rect.x;
    dst.src_top_offset = dst.visible.y - dst.rect.y;
    dst.left_to_right = s->regs.dp_cntl & DST_X_LEFT_TO_RIGHT;
    dst.top_to_bottom = s->regs.dp_cntl & DST_Y_TOP_TO_BOTTOM;
    dst.valid = true;

    return dst;
}

void ati_2d_blt(ATIVGAState *s)
{
    /* FIXME it is probably more complex than this and may need to be */
    /* rewritten but for now as a start just to get some output: */
    DisplaySurface *ds = qemu_console_surface(s->vga.con);
    DPRINTF("%p %u ds: %p %d %d rop: %x\n", s->vga.vram_ptr,
            s->vga.vbe_start_addr, surface_data(ds), surface_stride(ds),
            surface_bits_per_pixel(ds),
            s->regs.dp_rop3);
    ATIBlitDest dst = setup_2d_blt_dst(s);
    uint8_t *end = s->vga.vram_ptr + s->vga.vram_size;

    DPRINTF("%d %d %d, %d %d %d, (%d,%d) -> (%d,%d) %dx%d %c %c\n",
            s->regs.src_offset, s->regs.dst_offset, s->regs.default_offset,
            s->regs.src_pitch, s->regs.dst_pitch, s->regs.default_pitch,
            s->regs.src_x, s->regs.src_y,
            dst.rect.x, dst.rect.y, dst.rect.width, dst.rect.height,
            (dst.left_to_right ? '>' : '<'),
            (dst.top_to_bottom ? 'v' : '^'));

    switch (s->regs.dp_rop3) {
    case ROP3_SRCCOPY:
    {
        bool fallback = false;
        unsigned src_x = (dst.left_to_right ?
                         s->regs.src_x + dst.src_left_offset :
                         s->regs.src_x + 1 -
                         dst.rect.width + dst.src_left_offset);
        unsigned src_y = (dst.top_to_bottom ?
                         s->regs.src_y + dst.src_top_offset :
                         s->regs.src_y + 1 -
                         dst.rect.height + dst.src_top_offset);
        int src_stride = s->regs.src_pitch;
        if (!src_stride) {
            qemu_log_mask(LOG_GUEST_ERROR, "Zero source pitch\n");
            return;
        }
        uint8_t *src_bits = s->vga.vram_ptr + s->regs.src_offset;

        if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
            src_bits += s->regs.crtc_offset & 0x07ffffff;
            src_stride *= dst.bpp;
        }
        if (src_x > 0x3fff || src_y > 0x3fff || src_bits >= end
            || src_bits + src_x
             + (src_y + dst.visible.height) * src_stride >= end) {
            qemu_log_mask(LOG_UNIMP, "blt outside vram not implemented\n");
            return;
        }

        src_stride /= sizeof(uint32_t);
        dst.stride /= sizeof(uint32_t);
        DPRINTF("pixman_blt(%p, %p, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d)\n",
                src_bits, dst.bits, src_stride, dst.stride, dst.bpp, dst.bpp,
                src_x, src_y, dst.visible.x, dst.visible.y,
                dst.visible.width, dst.visible.height);
#ifdef CONFIG_PIXMAN
        if ((s->use_pixman & BIT(1)) &&
            dst.left_to_right &&
            dst.top_to_bottom) {
            fallback = !pixman_blt((uint32_t *)src_bits, (uint32_t *)dst.bits,
                                   src_stride, dst.stride, dst.bpp, dst.bpp,
                                   src_x, src_y, dst.visible.x, dst.visible.y,
                                   dst.visible.width, dst.visible.height);
        } else if (s->use_pixman & BIT(1)) {
            /* FIXME: We only really need a temporary if src and dst overlap */
            int llb = dst.visible.width * (dst.bpp / 8);
            int tmp_stride = DIV_ROUND_UP(llb, sizeof(uint32_t));
            uint32_t *tmp = g_malloc(tmp_stride * sizeof(uint32_t) *
                                     dst.visible.height);
            fallback = !pixman_blt((uint32_t *)src_bits, tmp,
                                   src_stride, tmp_stride, dst.bpp, dst.bpp,
                                   src_x, src_y, 0, 0,
                                   dst.visible.width, dst.visible.height);
            if (!fallback) {
                fallback = !pixman_blt(tmp, (uint32_t *)dst.bits,
                                       tmp_stride, dst.stride, dst.bpp, dst.bpp,
                                       0, 0, dst.visible.x, dst.visible.y,
                                       dst.visible.width, dst.visible.height);
            }
            g_free(tmp);
        } else
#endif
        {
            fallback = true;
        }
        if (fallback) {
            unsigned int y, i, j, bypp = dst.bpp / 8;
            unsigned int src_pitch = src_stride * sizeof(uint32_t);
            unsigned int dst_pitch = dst.stride * sizeof(uint32_t);

            for (y = 0; y < dst.visible.height; y++) {
                i = dst.visible.x * bypp;
                j = src_x * bypp;
                if (dst.top_to_bottom) {
                    i += (dst.visible.y + y) * dst_pitch;
                    j += (src_y + y) * src_pitch;
                } else {
                    i += (dst.visible.y + dst.visible.height - 1 - y) *
                         dst_pitch;
                    j += (src_y + dst.visible.height - 1 - y) * src_pitch;
                }
                memmove(&dst.bits[i], &src_bits[j], dst.visible.width * bypp);
            }
        }
        if (dst.bits >= s->vga.vram_ptr + s->vga.vbe_start_addr &&
            dst.bits < s->vga.vram_ptr + s->vga.vbe_start_addr +
            s->vga.vbe_regs[VBE_DISPI_INDEX_YRES] * s->vga.vbe_line_offset) {
            memory_region_set_dirty(&s->vga.vram, s->vga.vbe_start_addr +
                                    s->regs.dst_offset +
                                    dst.visible.y * surface_stride(ds),
                                    dst.visible.height * surface_stride(ds));
        }
        s->regs.dst_x = (dst.left_to_right ?
                         dst.visible.x + dst.visible.width : dst.visible.x);
        s->regs.dst_y = (dst.top_to_bottom ?
                         dst.visible.y + dst.visible.height : dst.visible.y);
        break;
    }
    case ROP3_PATCOPY:
    case ROP3_BLACKNESS:
    case ROP3_WHITENESS:
    {
        uint32_t filler = 0;

        switch (s->regs.dp_rop3) {
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

        dst.stride /= sizeof(uint32_t);
        DPRINTF("pixman_fill(%p, %d, %d, %d, %d, %d, %d, %x)\n",
                dst.bits, dst.stride, dst.bpp, dst.visible.x, dst.visible.y,
                dst.visible.width, dst.visible.height, filler);
#ifdef CONFIG_PIXMAN
        if (!(s->use_pixman & BIT(0)) ||
            !pixman_fill((uint32_t *)dst.bits, dst.stride, dst.bpp,
                         dst.visible.x, dst.visible.y,
                         dst.visible.width, dst.visible.height,
                         filler))
#endif
        {
            /* fallback when pixman failed or we don't want to call it */
            unsigned int x, y, i, bypp = dst.bpp / 8;
            unsigned int dst_pitch = dst.stride * sizeof(uint32_t);
            for (y = 0; y < dst.visible.height; y++) {
                i = dst.visible.x * bypp + (dst.visible.y + y) * dst_pitch;
                for (x = 0; x < dst.visible.width; x++, i += bypp) {
                    stn_he_p(&dst.bits[i], bypp, filler);
                }
            }
        }
        if (dst.bits >= s->vga.vram_ptr + s->vga.vbe_start_addr &&
            dst.bits < s->vga.vram_ptr + s->vga.vbe_start_addr +
            s->vga.vbe_regs[VBE_DISPI_INDEX_YRES] * s->vga.vbe_line_offset) {
            memory_region_set_dirty(&s->vga.vram, s->vga.vbe_start_addr +
                                    s->regs.dst_offset +
                                    dst.visible.y * surface_stride(ds),
                                    dst.visible.height * surface_stride(ds));
        }
        s->regs.dst_y = (dst.top_to_bottom ?
                         dst.visible.y + dst.visible.height : dst.visible.y);
        break;
    }
    default:
        qemu_log_mask(LOG_UNIMP, "Unimplemented ati_2d blt op %x\n",
                      s->regs.dp_rop3);
    }
}
