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
    int x;
    int y;
    int stride;
    uint8_t *bits;
} ATIBltSrc;

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

static void ati_2d_do_blt(ATIVGAState *s, const ATIBltSrc *src, ATIBltDst *dst)
{
    /* FIXME it is probably more complex than this and may need to be */
    /* rewritten but for now as a start just to get some output: */
    DisplaySurface *ds = qemu_console_surface(s->vga.con);
    uint8_t *end = s->vga.vram_ptr + s->vga.vram_size;
    int dst_stride_words, src_stride_words, vis_src_x, vis_src_y;
    QemuRect scissor, vis_dst;

    DPRINTF("%p %u ds: %p %d %d rop: %x\n", s->vga.vram_ptr,
            s->vga.vbe_start_addr, surface_data(ds), surface_stride(ds),
            surface_bits_per_pixel(ds),
            (s->regs.dp_mix & GMC_ROP3_MASK) >> 16);

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

    qemu_rect_init(&scissor,
                   s->regs.sc_left, s->regs.sc_top,
                   s->regs.sc_right - s->regs.sc_left + 1,
                   s->regs.sc_bottom - s->regs.sc_top + 1);
    qemu_rect_intersect(&dst->rect, &scissor, &vis_dst);
    if (!vis_dst.height || !vis_dst.width) {
        /* Nothing to do, completely clipped */
        return;
    }

    dst_stride_words = dst->stride / sizeof(uint32_t);
    src_stride_words = src->stride / sizeof(uint32_t);
    /*
     * The src must be offset if clipping is applied to the dst.
     * This is so that when the source is blit to a dst clipped
     * on the top or left the src image is not shifted into the
     * clipped region but actually clipped.
     */
    vis_src_x = src->x + (vis_dst.x - dst->rect.x);
    vis_src_y = src->y + (vis_dst.y - dst->rect.y);

    DPRINTF("%d %d %d, %d %d %d, (%d,%d) -> (%d,%d) %dx%d %c %c\n",
            s->regs.src_offset, s->regs.dst_offset, s->regs.default_offset,
            src->stride, dst->stride, s->regs.default_pitch,
            vis_src_x, vis_src_y, vis_dst.x, vis_dst.y,
            vis_dst.width, vis_dst.height,
            (dst->left_to_right ? '>' : '<'),
            (dst->top_to_bottom ? 'v' : '^'));

    switch (s->regs.dp_mix & GMC_ROP3_MASK) {
    case ROP3_SRCCOPY:
    {
        bool fallback = false;

        if (!src->stride) {
            qemu_log_mask(LOG_GUEST_ERROR, "Zero source pitch\n");
            return;
        }

        DPRINTF("pixman_blt(%p, %p, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d)\n",
                src->bits, dst->bits,
                src_stride_words, dst_stride_words,
                dst->bpp, dst->bpp,
                vis_src_x, vis_src_y, vis_dst.x, vis_dst.y,
                vis_dst.width, vis_dst.height);
#ifdef CONFIG_PIXMAN
        if ((s->use_pixman & BIT(1)) &&
            dst->left_to_right && dst->top_to_bottom) {
            fallback = !pixman_blt((uint32_t *)src->bits, (uint32_t *)dst->bits,
                                   src_stride_words, dst_stride_words,
                                   dst->bpp, dst->bpp,
                                   vis_src_x, vis_src_y, vis_dst.x, vis_dst.y,
                                   vis_dst.width, vis_dst.height);
        } else if (s->use_pixman & BIT(1)) {
            /* FIXME: We only really need a temporary if src and dst overlap */
            int llb = vis_dst.width * (dst->bpp / 8);
            int tmp_stride = DIV_ROUND_UP(llb, sizeof(uint32_t));
            uint32_t *tmp = g_malloc(tmp_stride * sizeof(uint32_t) *
                                     vis_dst.height);
            fallback = !pixman_blt((uint32_t *)src->bits, tmp,
                                   src_stride_words, tmp_stride,
                                   dst->bpp, dst->bpp,
                                   vis_src_x, vis_src_y, 0, 0,
                                   vis_dst.width, vis_dst.height);
            if (!fallback) {
                fallback = !pixman_blt(tmp, (uint32_t *)dst->bits,
                                       tmp_stride, dst_stride_words,
                                       dst->bpp, dst->bpp,
                                       0, 0, vis_dst.x, vis_dst.y,
                                       vis_dst.width, vis_dst.height);
            }
            g_free(tmp);
        } else
#endif
        {
            fallback = true;
        }
        if (fallback) {
            unsigned int y, i, j, bypp = dst->bpp / 8;
            for (y = 0; y < vis_dst.height; y++) {
                i = vis_dst.x * bypp;
                j = vis_src_x * bypp;
                if (dst->top_to_bottom) {
                    i += (vis_dst.y + y) * dst->stride;
                    j += (vis_src_y + y) * src->stride;
                } else {
                    i += (vis_dst.y + vis_dst.height - 1 - y) * dst->stride;
                    j += (vis_src_y + vis_dst.height - 1 - y) * src->stride;
                }
                memmove(&dst->bits[i], &src->bits[j], vis_dst.width * bypp);
            }
        }
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
                dst->bits, dst_stride_words, dst->bpp, vis_dst.x, vis_dst.y,
                vis_dst.width, vis_dst.height, filler);
#ifdef CONFIG_PIXMAN
        if (!(s->use_pixman & BIT(0)) ||
            !pixman_fill((uint32_t *)dst->bits,
                    dst_stride_words, dst->bpp, vis_dst.x, vis_dst.y,
                    vis_dst.width, vis_dst.height, filler))
#endif
        {
            /* fallback when pixman failed or we don't want to call it */
            unsigned int x, y, i, bypp = dst->bpp / 8;
            for (y = 0; y < vis_dst.height; y++) {
                i = vis_dst.x * bypp + (vis_dst.y + y) * dst->stride;
                for (x = 0; x < vis_dst.width; x++, i += bypp) {
                    stn_he_p(&dst->bits[i], bypp, filler);
                }
            }
        }
        break;
    }
    default:
        qemu_log_mask(LOG_UNIMP, "Unimplemented ati_2d blt op %x\n",
                      (s->regs.dp_mix & GMC_ROP3_MASK) >> 16);
    }

    if (s->dev_id != PCI_DEVICE_ID_ATI_RAGE128_PF) {
        /*
         * Hardware testing shows that dst is _not_ updated for Rage 128.
         * The M6 (R100/Radeon) docs state however that dst_y is updated.
         * This has not yet been validated on R100 hardware.
         */
        s->regs.dst_y = (dst->top_to_bottom ?
                        vis_dst.y + vis_dst.height : vis_dst.y);
    }

    if (dst->bits >= s->vga.vram_ptr + s->vga.vbe_start_addr &&
        dst->bits < s->vga.vram_ptr + s->vga.vbe_start_addr +
        s->vga.vbe_regs[VBE_DISPI_INDEX_YRES] * s->vga.vbe_line_offset) {
        memory_region_set_dirty(&s->vga.vram, s->vga.vbe_start_addr +
                                s->regs.dst_offset +
                                vis_dst.y * surface_stride(ds),
                                vis_dst.height * surface_stride(ds));
    }
}

void ati_2d_blt(ATIVGAState *s)
{
    uint8_t *end = s->vga.vram_ptr + s->vga.vram_size;
    ATIBltDst dst;
    ATIBltSrc src;

    setup_2d_blt_dst(s, &dst);

    /* Setup src to point at VRAM */
    src.x = (dst.left_to_right ?
             s->regs.src_x :
             s->regs.src_x + 1 - dst.rect.width);
    src.y = (dst.top_to_bottom ?
             s->regs.src_y :
             s->regs.src_y + 1 - dst.rect.height);
    src.stride = s->regs.src_pitch;
    src.bits = s->vga.vram_ptr + s->regs.src_offset;
    if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
        src.bits += s->regs.crtc_offset & 0x07ffffff;
        src.stride *= dst.bpp;
    }

    if (src.x > 0x3fff || src.y > 0x3fff || src.bits >= end
        || src.bits + src.x
         + (src.y + dst.rect.height) * src.stride >= end) {
        qemu_log_mask(LOG_UNIMP, "blt src outside vram not implemented\n");
        return;
    }

    ati_2d_do_blt(s, &src, &dst);
}
