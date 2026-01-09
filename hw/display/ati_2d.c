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
    bool valid;
    uint8_t *bits;
} ATIBlitSrc;

typedef struct {
    QemuRect rect;
    int bpp;
    int stride;
    bool top_to_bottom;
    bool left_to_right;
    bool valid;
    uint8_t *bits;
} ATIBlitDst;

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

static ATIBlitDst setup_2d_blt_dst(ATIVGAState *s)
{
    ATIBlitDst dst = {
        .valid = false,
        .bpp = ati_bpp_from_datatype(s),
        .stride = DEFAULT_CNTL ? s->regs.dst_pitch : s->regs.default_pitch,
        .left_to_right = s->regs.dp_cntl & DST_X_LEFT_TO_RIGHT,
        .top_to_bottom = s->regs.dp_cntl & DST_Y_TOP_TO_BOTTOM,
        .bits = s->vga.vram_ptr + (DEFAULT_CNTL ?
                s->regs.dst_offset : s->regs.default_offset),
    };
    uint8_t *end = s->vga.vram_ptr + s->vga.vram_size;
    unsigned dst_x = (dst.left_to_right ?
                     s->regs.dst_x : s->regs.dst_x + 1 - s->regs.dst_width);
    unsigned dst_y = (dst.top_to_bottom ?
                     s->regs.dst_y : s->regs.dst_y + 1 - s->regs.dst_height);
    qemu_rect_init(&dst.rect, dst_x, dst_y,
                   s->regs.dst_width, s->regs.dst_height);

    if (!dst.bpp) {
        qemu_log_mask(LOG_GUEST_ERROR, "Invalid bpp\n");
        return dst;
    }
    if (!dst.stride) {
        qemu_log_mask(LOG_GUEST_ERROR, "Zero dest pitch\n");
        return dst;
    }
    if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
        dst.bits += s->regs.crtc_offset & 0x07ffffff;
        dst.stride *= dst.bpp;
    }
    if (dst.rect.x > 0x3fff || dst.rect.y > 0x3fff || dst.bits >= end
        || dst.bits + dst.rect.x
         + (dst.rect.y + dst.rect.height) * dst.stride >= end) {
        qemu_log_mask(LOG_UNIMP, "blt outside vram not implemented\n");
        return dst;
    }

    dst.valid = true;

    return dst;
}

static ATIBlitSrc setup_2d_blt_src(ATIVGAState *s, const ATIBlitDst *dst)
{
    ATIBlitSrc src = {
        .valid = false,
        .x = (dst->left_to_right ?
             s->regs.src_x : s->regs.src_x + 1 - dst->rect.width),
        .y = (dst->top_to_bottom ?
             s->regs.src_y : s->regs.src_y + 1 - dst->rect.height),
        .stride = DEFAULT_CNTL ? s->regs.src_pitch : s->regs.default_pitch,
        .bits = s->vga.vram_ptr + (DEFAULT_CNTL ?
                s->regs.src_offset : s->regs.default_offset),
    };
    uint8_t *end = s->vga.vram_ptr + s->vga.vram_size;

    if (!src.stride) {
        qemu_log_mask(LOG_GUEST_ERROR, "Zero source pitch\n");
        return src;
    }

    if (s->dev_id == PCI_DEVICE_ID_ATI_RAGE128_PF) {
        src.bits += s->regs.crtc_offset & 0x07ffffff;
        src.stride *= dst->bpp;
    }

    if (src.x > 0x3fff || src.y > 0x3fff || src.bits >= end
        || src.bits + src.x
         + (src.y + dst->rect.height) * src.stride >= end) {
        qemu_log_mask(LOG_UNIMP, "blt outside vram not implemented\n");
        return src;
    }

    src.valid = true;

    return src;
}

static void ati_set_dirty(ATIVGAState *s, const ATIBlitDst *dst)
{
    DisplaySurface *ds = qemu_console_surface(s->vga.con);
    if (dst->bits >= s->vga.vram_ptr + s->vga.vbe_start_addr &&
        dst->bits < s->vga.vram_ptr + s->vga.vbe_start_addr +
        s->vga.vbe_regs[VBE_DISPI_INDEX_YRES] * s->vga.vbe_line_offset) {
        memory_region_set_dirty(&s->vga.vram, s->vga.vbe_start_addr +
                                s->regs.dst_offset +
                                dst->rect.y * surface_stride(ds),
                                dst->rect.height * surface_stride(ds));
    }
}

static void ati_2d_blt(ATIVGAState *s, ATIBlitSrc src, ATIBlitDst dst)
{
    /* FIXME it is probably more complex than this and may need to be */
    /* rewritten but for now as a start just to get some output: */
    uint32_t rop3 = s->regs.dp_mix & GMC_ROP3_MASK;
    bool use_pixman = s->use_pixman & BIT(1);
    int dst_stride_words = dst.stride / sizeof(uint32_t);
    int src_stride_words = src.stride / sizeof(uint32_t);

    DPRINTF("%p %u ds: %p %d %d rop: %x\n", s->vga.vram_ptr,
            s->vga.vbe_start_addr, surface_data(ds), surface_stride(ds),
            surface_bits_per_pixel(ds), rop3 >> 16);
    DPRINTF("%d %d %d, %d %d %d, (%d,%d) -> (%d,%d) %dx%d %c %c\n",
            s->regs.src_offset, s->regs.dst_offset, s->regs.default_offset,
            s->regs.src_pitch, s->regs.dst_pitch, s->regs.default_pitch,
            s->regs.src_x, s->regs.src_y,
            dst.rect.x, dst.rect.y, dst.rect.width, dst.rect.height,
            (dst.left_to_right ? '>' : '<'),
            (dst.top_to_bottom ? 'v' : '^'));

    switch (rop3) {
    case ROP3_SRCCOPY:
    {
        bool fallback = false;

        DPRINTF("pixman_blt(%p, %p, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d)\n",
                src.bits, dst.bits, src_stride_words, dst_stride_words,
                dst.bpp, dst.bpp, src.x, src.y,
                dst.rect.x, dst.rect.y,
                dst.rect.width, dst.rect.height);
#ifdef CONFIG_PIXMAN
        if (use_pixman &&
            dst.left_to_right && dst.top_to_bottom) {
            fallback = !pixman_blt((uint32_t *)src.bits, (uint32_t *)dst.bits,
                                   src_stride_words, dst_stride_words,
                                   dst.bpp, dst.bpp,
                                   src.x, src.y, dst.rect.x, dst.rect.y,
                                   dst.rect.width, dst.rect.height);
        } else if (use_pixman) {
            /* FIXME: We only really need a temporary if src and dst overlap */
            int llb = dst.rect.width * (dst.bpp / 8);
            int tmp_stride = DIV_ROUND_UP(llb, sizeof(uint32_t));
            uint32_t *tmp = g_malloc(tmp_stride * sizeof(uint32_t) *
                                     dst.rect.height);
            fallback = !pixman_blt((uint32_t *)src.bits, tmp,
                                   src_stride_words, tmp_stride, dst.bpp, dst.bpp,
                                   src.x, src.y, 0, 0,
                                   dst.rect.width, dst.rect.height);
            if (!fallback) {
                fallback = !pixman_blt(tmp, (uint32_t *)dst.bits,
                                       tmp_stride, dst_stride_words,
                                       dst.bpp, dst.bpp,
                                       0, 0, dst.rect.x, dst.rect.y,
                                       dst.rect.width, dst.rect.height);
            }
            g_free(tmp);
        } else
#endif
        {
            fallback = true;
        }
        if (fallback) {
            unsigned int y, i, j, bypp = dst.bpp / 8;
            for (y = 0; y < dst.rect.height; y++) {
                i = dst.rect.x * bypp;
                j = src.x * bypp;
                if (dst.top_to_bottom) {
                    i += (dst.rect.y + y) * dst.stride;
                    j += (src.y + y) * src.stride;
                } else {
                    i += (dst.rect.y + dst.rect.height - 1 - y) * dst.stride;
                    j += (src.y + dst.rect.height - 1 - y) * src.stride;
                }
                memmove(&dst.bits[i], &src.bits[j], dst.rect.width * bypp);
            }
        }
        ati_set_dirty(s, &dst);
        s->regs.dst_x = (dst.left_to_right ?
                         dst.rect.x + dst.rect.width : dst.rect.x);
        s->regs.dst_y = (dst.top_to_bottom ?
                         dst.rect.y + dst.rect.height : dst.rect.y);
        break;
    }
    case ROP3_PATCOPY:
    case ROP3_BLACKNESS:
    case ROP3_WHITENESS:
    {
        uint32_t filler = 0;

        switch (rop3) {
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
                dst.bits, dst_stride_words, dst.bpp, dst.rect.x, dst.rect.y,
                dst.rect.width, dst.rect.height, filler);
#ifdef CONFIG_PIXMAN
        if (!use_pixman ||
            !pixman_fill((uint32_t *)dst.bits, dst_stride_words, dst.bpp,
                         dst.rect.x, dst.rect.y,
                         dst.rect.width, dst.rect.height, filler))
#endif
        {
            /* fallback when pixman failed or we don't want to call it */
            unsigned int x, y, i, bypp = dst.bpp / 8;
            for (y = 0; y < dst.rect.height; y++) {
                i = dst.rect.x * bypp + (dst.rect.y + y) * dst.stride;
                for (x = 0; x < dst.rect.width; x++, i += bypp) {
                    stn_he_p(&dst.bits[i], bypp, filler);
                }
            }
        }
        ati_set_dirty(s, &dst);
        s->regs.dst_y = (dst.top_to_bottom ?
                         dst.rect.y + dst.rect.height : dst.rect.y);
        break;
    }
    default:
        qemu_log_mask(LOG_UNIMP, "Unimplemented ati_2d blt op %x\n",
                      rop3 >> 16);
    }
}

void ati_2d_blt_from_memory(ATIVGAState *s)
{
    if ((s->regs.dp_mix & DP_SRC_SOURCE) != DP_SRC_RECT) {
        return;
    }
    ATIBlitDst dst = setup_2d_blt_dst(s);
    ATIBlitSrc src = setup_2d_blt_src(s, &dst);
    ati_2d_blt(s, src, dst);
}
