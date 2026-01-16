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

typedef struct {
    QemuRect rect;
    int bpp;
    int stride;
    bool top_to_bottom;
    bool left_to_right;
    uint8_t *bits;
} ATIBltDst;

static int ati_bpp_from_datatype(const ATIVGAState *s)
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

static void setup_2d_blt_dst(const ATIVGAState *s, ATIBltDst *dst)
{
    unsigned dst_x, dst_y;
    dst->bpp = ati_bpp_from_datatype(s);
    dst->stride = s->regs.dst_pitch;
    dst->left_to_right = s->regs.dp_cntl & DST_X_LEFT_TO_RIGHT;
    dst->top_to_bottom = s->regs.dp_cntl & DST_Y_TOP_TO_BOTTOM;
    dst->bits = s->vga.vram_ptr + s->regs.dst_offset;
    dst_x = (dst->left_to_right ?
            s->regs.dst_x : s->regs.dst_x + 1 - s->regs.dst_width);
    dst_y = (dst->top_to_bottom ?
            s->regs.dst_y : s->regs.dst_y + 1 - s->regs.dst_height);
    qemu_rect_init(&dst->rect, dst_x, dst_y,
                   s->regs.dst_width, s->regs.dst_height);
    if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
        dst->bits += s->regs.crtc_offset & 0x07ffffff;
        dst->stride *= dst->bpp;
    }
}

void ati_2d_blt(ATIVGAState *s)
{
    /* FIXME it is probably more complex than this and may need to be */
    /* rewritten but for now as a start just to get some output: */
    DisplaySurface *ds = qemu_console_surface(s->vga.con);
    uint8_t *end = s->vga.vram_ptr + s->vga.vram_size;
    int dst_stride_words;
    ATIBltDst _dst; /* TEMP: avoid churn in future patches */
    ATIBltDst *dst = &_dst;

    DPRINTF("%p %u ds: %p %d %d rop: %x\n", s->vga.vram_ptr,
            s->vga.vbe_start_addr, surface_data(ds), surface_stride(ds),
            surface_bits_per_pixel(ds),
            (s->regs.dp_mix & GMC_ROP3_MASK) >> 16);

    setup_2d_blt_dst(s, dst);

    if (!dst->bpp) {
        qemu_log_mask(LOG_GUEST_ERROR, "Invalid bpp\n");
        return;
    }
    if (!dst->stride) {
        qemu_log_mask(LOG_GUEST_ERROR, "Zero dest pitch\n");
        return;
    }
    if (dst->rect.x > 0x3fff || dst->rect.y > 0x3fff || dst->bits >= end
        || dst->bits + dst->rect.x
         + (dst->rect.y + dst->rect.height) * dst->stride >= end) {
        qemu_log_mask(LOG_UNIMP, "blt outside vram not implemented\n");
        return;
    }

    dst_stride_words = dst->stride / sizeof(uint32_t);

    DPRINTF("%d %d %d, %d %d %d, (%d,%d) -> (%d,%d) %dx%d %c %c\n",
            s->regs.src_offset, s->regs.dst_offset, s->regs.default_offset,
            s->regs.src_pitch, dst->stride, s->regs.default_pitch,
            s->regs.src_x, s->regs.src_y, dst->rect.x, dst->rect.y,
            dst->rect.width, dst->rect.height,
            (dst->left_to_right ? '>' : '<'),
            (dst->top_to_bottom ? 'v' : '^'));

    switch (s->regs.dp_mix & GMC_ROP3_MASK) {
    case ROP3_SRCCOPY:
    {
        bool fallback = false;
        unsigned src_x = (dst->left_to_right ?
                       s->regs.src_x : s->regs.src_x + 1 - dst->rect.width);
        unsigned src_y = (dst->top_to_bottom ?
                       s->regs.src_y : s->regs.src_y + 1 - dst->rect.height);
        int src_stride = s->regs.src_pitch;
        if (!src_stride) {
            qemu_log_mask(LOG_GUEST_ERROR, "Zero source pitch\n");
            return;
        }
        uint8_t *src_bits = s->vga.vram_ptr + s->regs.src_offset;

        if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
            src_bits += s->regs.crtc_offset & 0x07ffffff;
            src_stride *= dst->bpp;
        }
        if (src_x > 0x3fff || src_y > 0x3fff || src_bits >= end
            || src_bits + src_x
             + (src_y + dst->rect.height) * src_stride >= end) {
            qemu_log_mask(LOG_UNIMP, "blt outside vram not implemented\n");
            return;
        }

        src_stride /= sizeof(uint32_t);
        DPRINTF("pixman_blt(%p, %p, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d)\n",
                src_bits, dst->bits,
                src_stride, dst_stride_words,
                dst->bpp, dst->bpp,
                src_x, src_y, dst->rect.x, dst->rect.y,
                dst->rect.width, dst->rect.height);
#ifdef CONFIG_PIXMAN
        if ((s->use_pixman & BIT(1)) &&
            dst->left_to_right && dst->top_to_bottom) {
            fallback = !pixman_blt((uint32_t *)src_bits, (uint32_t *)dst->bits,
                                   src_stride, dst_stride_words,
                                   dst->bpp, dst->bpp,
                                   src_x, src_y, dst->rect.x, dst->rect.y,
                                   dst->rect.width, dst->rect.height);
        } else if (s->use_pixman & BIT(1)) {
            /* FIXME: We only really need a temporary if src and dst overlap */
            int llb = dst->rect.width * (dst->bpp / 8);
            int tmp_stride = DIV_ROUND_UP(llb, sizeof(uint32_t));
            uint32_t *tmp = g_malloc(tmp_stride * sizeof(uint32_t) *
                                     dst->rect.height);
            fallback = !pixman_blt((uint32_t *)src_bits, tmp,
                                   src_stride, tmp_stride,
                                   dst->bpp, dst->bpp,
                                   src_x, src_y, 0, 0,
                                   dst->rect.width, dst->rect.height);
            if (!fallback) {
                fallback = !pixman_blt(tmp, (uint32_t *)dst->bits,
                                       tmp_stride, dst_stride_words,
                                       dst->bpp, dst->bpp,
                                       0, 0, dst->rect.x, dst->rect.y,
                                       dst->rect.width, dst->rect.height);
            }
            g_free(tmp);
        } else
#endif
        {
            fallback = true;
        }
        if (fallback) {
            unsigned int y, i, j, bypp = dst->bpp / 8;
            unsigned int src_pitch = src_stride * sizeof(uint32_t);
            for (y = 0; y < dst->rect.height; y++) {
                i = dst->rect.x * bypp;
                j = src_x * bypp;
                if (dst->top_to_bottom) {
                    i += (dst->rect.y + y) * dst->stride;
                    j += (src_y + y) * src_pitch;
                } else {
                    i += (dst->rect.y + dst->rect.height - 1 - y) * dst->stride;
                    j += (src_y + dst->rect.height - 1 - y) * src_pitch;
                }
                memmove(&dst->bits[i], &src_bits[j], dst->rect.width * bypp);
            }
        }
        if (dst->bits >= s->vga.vram_ptr + s->vga.vbe_start_addr &&
            dst->bits < s->vga.vram_ptr + s->vga.vbe_start_addr +
            s->vga.vbe_regs[VBE_DISPI_INDEX_YRES] * s->vga.vbe_line_offset) {
            memory_region_set_dirty(&s->vga.vram, s->vga.vbe_start_addr +
                                    s->regs.dst_offset +
                                    dst->rect.y * surface_stride(ds),
                                    dst->rect.height * surface_stride(ds));
        }
        s->regs.dst_x = (dst->left_to_right ?
                         dst->rect.x + dst->rect.width : dst->rect.x);
        s->regs.dst_y = (dst->top_to_bottom ?
                         dst->rect.y + dst->rect.height : dst->rect.y);
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

        DPRINTF("pixman_fill(%p, %d, %d, %d, %d, %d, %d, %x)\n",
                dst->bits, dst_stride_words, dst->bpp, dst->rect.x, dst->rect.y,
                dst->rect.width, dst->rect.height, filler);
#ifdef CONFIG_PIXMAN
        if (!(s->use_pixman & BIT(0)) ||
            !pixman_fill((uint32_t *)dst->bits,
                    dst_stride_words, dst->bpp, dst->rect.x, dst->rect.y,
                    dst->rect.width, dst->rect.height, filler))
#endif
        {
            /* fallback when pixman failed or we don't want to call it */
            unsigned int x, y, i, bypp = dst->bpp / 8;
            for (y = 0; y < dst->rect.height; y++) {
                i = dst->rect.x * bypp + (dst->rect.y + y) * dst->stride;
                for (x = 0; x < dst->rect.width; x++, i += bypp) {
                    stn_he_p(&dst->bits[i], bypp, filler);
                }
            }
        }
        if (dst->bits >= s->vga.vram_ptr + s->vga.vbe_start_addr &&
            dst->bits < s->vga.vram_ptr + s->vga.vbe_start_addr +
            s->vga.vbe_regs[VBE_DISPI_INDEX_YRES] * s->vga.vbe_line_offset) {
            memory_region_set_dirty(&s->vga.vram, s->vga.vbe_start_addr +
                                    s->regs.dst_offset +
                                    dst->rect.y * surface_stride(ds),
                                    dst->rect.height * surface_stride(ds));
        }
        s->regs.dst_y = (dst->top_to_bottom ?
                         dst->rect.y + dst->rect.height : dst->rect.y);
        break;
    }
    default:
        qemu_log_mask(LOG_UNIMP, "Unimplemented ati_2d blt op %x\n",
                      (s->regs.dp_mix & GMC_ROP3_MASK) >> 16);
    }
}
