/*
 * SPDX-License-Identifier: MIT
 * QEMU VC
 */
#include "qemu/osdep.h"

#include "chardev/char.h"
#include "qapi/error.h"
#include "qemu/fifo8.h"
#include "qemu/option.h"
#include "qemu/queue.h"
#include "ui/console.h"
#include "ui/cp437.h"

#include "pixman.h"
#include "trace.h"
#include "console-priv.h"

#define DEFAULT_BACKSCROLL 512
#define CONSOLE_CURSOR_PERIOD 500

typedef struct TextAttributes {
    uint8_t fgcol:4;
    uint8_t bgcol:4;
    uint8_t bold:1;
    uint8_t uline:1;
    uint8_t blink:1;
    uint8_t invers:1;
    uint8_t unvisible:1;
} TextAttributes;

#define TEXT_ATTRIBUTES_DEFAULT ((TextAttributes) { \
    .fgcol = QEMU_COLOR_WHITE,                      \
    .bgcol = QEMU_COLOR_BLACK                       \
})

typedef struct TextCell {
    uint8_t ch;
    TextAttributes t_attrib;
} TextCell;

#define MAX_ESC_PARAMS 3

enum TTYState {
    TTY_STATE_NORM,
    TTY_STATE_ESC,
    TTY_STATE_CSI,
    TTY_STATE_G0,
    TTY_STATE_G1,
    TTY_STATE_OSC,
};

typedef struct QemuVT100 QemuVT100;

struct QemuVT100 {
    pixman_image_t *image;
    void (*image_update)(QemuVT100 *vt, int x, int y, int width, int height);

    int width;
    int height;
    int total_height;
    int backscroll_height;
    int x, y;
    int y_displayed;
    int y_base;
    TextCell *cells;
    int text_x[2], text_y[2], cursor_invalidate;
    int echo;

    int update_x0;
    int update_y0;
    int update_x1;
    int update_y1;

    QTAILQ_ENTRY(QemuVT100) list;
};

static QTAILQ_HEAD(QemuVT100Head, QemuVT100) vt100s =
    QTAILQ_HEAD_INITIALIZER(vt100s);

typedef struct QemuTextConsole {
    QemuConsole parent;

    QemuVT100 vt;
    Chardev *chr;
    /* fifo for key pressed */
    Fifo8 out_fifo;
} QemuTextConsole;

typedef QemuConsoleClass QemuTextConsoleClass;

OBJECT_DEFINE_TYPE(QemuTextConsole, qemu_text_console, QEMU_TEXT_CONSOLE, QEMU_CONSOLE)

typedef struct QemuFixedTextConsole {
    QemuTextConsole parent;
} QemuFixedTextConsole;

typedef QemuTextConsoleClass QemuFixedTextConsoleClass;

OBJECT_DEFINE_TYPE(QemuFixedTextConsole, qemu_fixed_text_console, QEMU_FIXED_TEXT_CONSOLE, QEMU_TEXT_CONSOLE)

struct VCChardev {
    Chardev parent;
    QemuTextConsole *console;

    enum TTYState state;
    int esc_params[MAX_ESC_PARAMS];
    int nb_esc_params;
    uint32_t utf8_state;     /* UTF-8 DFA decoder state */
    uint32_t utf8_codepoint; /* accumulated UTF-8 code point */
    TextAttributes t_attrib; /* currently active text attributes */
    TextAttributes t_attrib_saved;
    int x_saved, y_saved;
};
typedef struct VCChardev VCChardev;

static const pixman_color_t color_table_rgb[2][8] = {
    {   /* dark */
        [QEMU_COLOR_BLACK]   = QEMU_PIXMAN_COLOR_BLACK,
        [QEMU_COLOR_BLUE]    = QEMU_PIXMAN_COLOR(0x00, 0x00, 0xaa),  /* blue */
        [QEMU_COLOR_GREEN]   = QEMU_PIXMAN_COLOR(0x00, 0xaa, 0x00),  /* green */
        [QEMU_COLOR_CYAN]    = QEMU_PIXMAN_COLOR(0x00, 0xaa, 0xaa),  /* cyan */
        [QEMU_COLOR_RED]     = QEMU_PIXMAN_COLOR(0xaa, 0x00, 0x00),  /* red */
        [QEMU_COLOR_MAGENTA] = QEMU_PIXMAN_COLOR(0xaa, 0x00, 0xaa),  /* magenta */
        [QEMU_COLOR_YELLOW]  = QEMU_PIXMAN_COLOR(0xaa, 0xaa, 0x00),  /* yellow */
        [QEMU_COLOR_WHITE]   = QEMU_PIXMAN_COLOR_GRAY,
    },
    {   /* bright */
        [QEMU_COLOR_BLACK]   = QEMU_PIXMAN_COLOR_BLACK,
        [QEMU_COLOR_BLUE]    = QEMU_PIXMAN_COLOR(0x00, 0x00, 0xff),  /* blue */
        [QEMU_COLOR_GREEN]   = QEMU_PIXMAN_COLOR(0x00, 0xff, 0x00),  /* green */
        [QEMU_COLOR_CYAN]    = QEMU_PIXMAN_COLOR(0x00, 0xff, 0xff),  /* cyan */
        [QEMU_COLOR_RED]     = QEMU_PIXMAN_COLOR(0xff, 0x00, 0x00),  /* red */
        [QEMU_COLOR_MAGENTA] = QEMU_PIXMAN_COLOR(0xff, 0x00, 0xff),  /* magenta */
        [QEMU_COLOR_YELLOW]  = QEMU_PIXMAN_COLOR(0xff, 0xff, 0x00),  /* yellow */
        [QEMU_COLOR_WHITE]   = QEMU_PIXMAN_COLOR(0xff, 0xff, 0xff),  /* white */
    }
};

static bool cursor_visible_phase;
static QEMUTimer *cursor_timer;

static char *
qemu_text_console_get_label(QemuConsole *c)
{
    QemuTextConsole *tc = QEMU_TEXT_CONSOLE(c);

    return tc->chr ? g_strdup(tc->chr->label) : NULL;
}

static void image_fill_rect(pixman_image_t *image, int posx, int posy,
                            int width, int height, pixman_color_t color)
{
    pixman_rectangle16_t rect = {
        .x = posx, .y = posy, .width = width, .height = height
    };

    pixman_image_fill_rectangles(PIXMAN_OP_SRC, image, &color, 1, &rect);
}

/* copy from (xs, ys) to (xd, yd) a rectangle of size (w, h) */
static void image_bitblt(pixman_image_t *image,
                         int xs, int ys, int xd, int yd, int w, int h)
{
    pixman_image_composite(PIXMAN_OP_SRC,
                           image, NULL, image,
                           xs, ys, 0, 0, xd, yd, w, h);
}

static void vt100_putcharxy(QemuVT100 *vt, int x, int y, int ch,
                            TextAttributes *t_attrib)
{
    static pixman_image_t *glyphs[256];
    pixman_color_t fgcol, bgcol;

    assert(vt->image);
    if (t_attrib->invers) {
        bgcol = color_table_rgb[t_attrib->bold][t_attrib->fgcol];
        fgcol = color_table_rgb[t_attrib->bold][t_attrib->bgcol];
    } else {
        fgcol = color_table_rgb[t_attrib->bold][t_attrib->fgcol];
        bgcol = color_table_rgb[t_attrib->bold][t_attrib->bgcol];
    }

    if (!glyphs[ch]) {
        glyphs[ch] = qemu_pixman_glyph_from_vgafont(FONT_HEIGHT, vgafont16, ch);
    }
    qemu_pixman_glyph_render(glyphs[ch], vt->image,
                             &fgcol, &bgcol, x, y, FONT_WIDTH, FONT_HEIGHT);
}

static void vt100_invalidate_xy(QemuVT100 *vt, int x, int y)
{
    if (vt->update_x0 > x * FONT_WIDTH) {
        vt->update_x0 = x * FONT_WIDTH;
    }
    if (vt->update_y0 > y * FONT_HEIGHT) {
        vt->update_y0 = y * FONT_HEIGHT;
    }
    if (vt->update_x1 < (x + 1) * FONT_WIDTH) {
        vt->update_x1 = (x + 1) * FONT_WIDTH;
    }
    if (vt->update_y1 < (y + 1) * FONT_HEIGHT) {
        vt->update_y1 = (y + 1) * FONT_HEIGHT;
    }
}

static void vt100_show_cursor(QemuVT100 *vt, int show)
{
    TextCell *c;
    int y, y1;
    int x = vt->x;

    vt->cursor_invalidate = 1;

    if (x >= vt->width) {
        x = vt->width - 1;
    }
    y1 = (vt->y_base + vt->y) % vt->total_height;
    y = y1 - vt->y_displayed;
    if (y < 0) {
        y += vt->total_height;
    }
    if (y < vt->height) {
        c = &vt->cells[y1 * vt->width + x];
        if (show && cursor_visible_phase) {
            TextAttributes t_attrib = TEXT_ATTRIBUTES_DEFAULT;
            t_attrib.invers = !(t_attrib.invers); /* invert fg and bg */
            vt100_putcharxy(vt, x, y, c->ch, &t_attrib);
        } else {
            vt100_putcharxy(vt, x, y, c->ch, &(c->t_attrib));
        }
        vt100_invalidate_xy(vt, x, y);
    }
}

static void vt100_image_update(QemuVT100 *vt, int x, int y, int width, int height)
{
    vt->image_update(vt, x, y, width, height);
}

static void vt100_refresh(QemuVT100 *vt)
{
    TextCell *c;
    int x, y, y1;
    int w = pixman_image_get_width(vt->image);
    int h = pixman_image_get_height(vt->image);

    vt->text_x[0] = 0;
    vt->text_y[0] = 0;
    vt->text_x[1] = vt->width - 1;
    vt->text_y[1] = vt->height - 1;
    vt->cursor_invalidate = 1;

    image_fill_rect(vt->image, 0, 0, w, h,
                    color_table_rgb[0][QEMU_COLOR_BLACK]);
    y1 = vt->y_displayed;
    for (y = 0; y < vt->height; y++) {
        c = vt->cells + y1 * vt->width;
        for (x = 0; x < vt->width; x++) {
            vt100_putcharxy(vt, x, y, c->ch,
                          &(c->t_attrib));
            c++;
        }
        if (++y1 == vt->total_height) {
            y1 = 0;
        }
    }
    vt100_show_cursor(vt, 1);
    vt100_image_update(vt, 0, 0, w, h);
}

static void vt100_scroll(QemuVT100 *vt, int ydelta)
{
    int i, y1;

    if (ydelta > 0) {
        for(i = 0; i < ydelta; i++) {
            if (vt->y_displayed == vt->y_base)
                break;
            if (++vt->y_displayed == vt->total_height)
                vt->y_displayed = 0;
        }
    } else {
        ydelta = -ydelta;
        i = vt->backscroll_height;
        if (i > vt->total_height - vt->height)
            i = vt->total_height - vt->height;
        y1 = vt->y_base - i;
        if (y1 < 0)
            y1 += vt->total_height;
        for(i = 0; i < ydelta; i++) {
            if (vt->y_displayed == y1)
                break;
            if (--vt->y_displayed < 0)
                vt->y_displayed = vt->total_height - 1;
        }
    }
    vt100_refresh(vt);
}

static void qemu_text_console_flush(QemuTextConsole *s)
{
    uint32_t len, avail;

    len = qemu_chr_be_can_write(s->chr);
    avail = fifo8_num_used(&s->out_fifo);
    while (len > 0 && avail > 0) {
        const uint8_t *buf;
        uint32_t size;

        buf = fifo8_pop_bufptr(&s->out_fifo, MIN(len, avail), &size);
        qemu_chr_be_write(s->chr, buf, size);
        len = qemu_chr_be_can_write(s->chr);
        avail -= size;
    }
}

static void qemu_text_console_write(QemuTextConsole *s, const void *buf, size_t len)
{
    uint32_t num_free;

    num_free = fifo8_num_free(&s->out_fifo);
    fifo8_push_all(&s->out_fifo, buf, MIN(num_free, len));
    qemu_text_console_flush(s);
}

/* called when an ascii key is pressed */
void qemu_text_console_handle_keysym(QemuTextConsole *s, int keysym)
{
    uint8_t buf[16], *q;
    int c;

    switch(keysym) {
    case QEMU_KEY_CTRL_UP:
        vt100_scroll(&s->vt, -1);
        break;
    case QEMU_KEY_CTRL_DOWN:
        vt100_scroll(&s->vt, 1);
        break;
    case QEMU_KEY_CTRL_PAGEUP:
        vt100_scroll(&s->vt, -10);
        break;
    case QEMU_KEY_CTRL_PAGEDOWN:
        vt100_scroll(&s->vt, 10);
        break;
    default:
        /* convert the QEMU keysym to VT100 key string */
        q = buf;
        if (keysym >= 0xe100 && keysym <= 0xe11f) {
            *q++ = '\033';
            *q++ = '[';
            c = keysym - 0xe100;
            if (c >= 10)
                *q++ = '0' + (c / 10);
            *q++ = '0' + (c % 10);
            *q++ = '~';
        } else if (keysym >= 0xe120 && keysym <= 0xe17f) {
            *q++ = '\033';
            *q++ = '[';
            *q++ = keysym & 0xff;
        } else if (s->vt.echo && (keysym == '\r' || keysym == '\n')) {
            qemu_chr_write(s->chr, (uint8_t *)"\r", 1, true);
            *q++ = '\n';
        } else {
            *q++ = keysym;
        }
        if (s->vt.echo) {
            qemu_chr_write(s->chr, buf, q - buf, true);
        }
        qemu_text_console_write(s, buf, q - buf);
        break;
    }
}

static void text_console_update(void *opaque, console_ch_t *chardata)
{
    QemuTextConsole *s = QEMU_TEXT_CONSOLE(opaque);
    int i, j, src;

    if (s->vt.text_x[0] <= s->vt.text_x[1]) {
        src = (s->vt.y_base + s->vt.text_y[0]) * s->vt.width;
        chardata += s->vt.text_y[0] * s->vt.width;
        for (i = s->vt.text_y[0]; i <= s->vt.text_y[1]; i ++)
            for (j = 0; j < s->vt.width; j++, src++) {
                console_write_ch(chardata ++,
                                 ATTR2CHTYPE(s->vt.cells[src].ch,
                                             s->vt.cells[src].t_attrib.fgcol,
                                             s->vt.cells[src].t_attrib.bgcol,
                                             s->vt.cells[src].t_attrib.bold));
            }
        dpy_text_update(QEMU_CONSOLE(s), s->vt.text_x[0], s->vt.text_y[0],
                        s->vt.text_x[1] - s->vt.text_x[0], i - s->vt.text_y[0]);
        s->vt.text_x[0] = s->vt.width;
        s->vt.text_y[0] = s->vt.height;
        s->vt.text_x[1] = 0;
        s->vt.text_y[1] = 0;
    }
    if (s->vt.cursor_invalidate) {
        dpy_text_cursor(QEMU_CONSOLE(s), s->vt.x, s->vt.y);
        s->vt.cursor_invalidate = 0;
    }
}

static void vt100_set_image(QemuVT100 *vt, pixman_image_t *image)
{
    TextCell *cells, *c, *c1;
    int w1, x, y, last_width, w, h;

    vt->image = image;
    w = pixman_image_get_width(image) / FONT_WIDTH;
    h = pixman_image_get_height(image) / FONT_HEIGHT;
    if (w == vt->width && h == vt->height) {
        return;
    }

    last_width = vt->width;
    vt->width = w;
    vt->height = h;

    w1 = MIN(vt->width, last_width);

    cells = g_new(TextCell, vt->width * vt->total_height + 1);
    for (y = 0; y < vt->total_height; y++) {
        c = &cells[y * vt->width];
        if (w1 > 0) {
            c1 = &vt->cells[y * last_width];
            for (x = 0; x < w1; x++) {
                *c++ = *c1++;
            }
        }
        for (x = w1; x < vt->width; x++) {
            c->ch = ' ';
            c->t_attrib = TEXT_ATTRIBUTES_DEFAULT;
            c++;
        }
    }
    g_free(vt->cells);
    vt->cells = cells;
}

static void vt100_put_lf(QemuVT100 *vt)
{
    TextCell *c;
    int x, y1;

    vt->y++;
    if (vt->y >= vt->height) {
        vt->y = vt->height - 1;

        if (vt->y_displayed == vt->y_base) {
            if (++vt->y_displayed == vt->total_height)
                vt->y_displayed = 0;
        }
        if (++vt->y_base == vt->total_height)
            vt->y_base = 0;
        if (vt->backscroll_height < vt->total_height)
            vt->backscroll_height++;
        y1 = (vt->y_base + vt->height - 1) % vt->total_height;
        c = &vt->cells[y1 * vt->width];
        for(x = 0; x < vt->width; x++) {
            c->ch = ' ';
            c->t_attrib = TEXT_ATTRIBUTES_DEFAULT;
            c++;
        }
        if (vt->y_displayed == vt->y_base) {
            vt->text_x[0] = 0;
            vt->text_y[0] = 0;
            vt->text_x[1] = vt->width - 1;
            vt->text_y[1] = vt->height - 1;

            image_bitblt(vt->image, 0, FONT_HEIGHT, 0, 0,
                         vt->width * FONT_WIDTH,
                         (vt->height - 1) * FONT_HEIGHT);
            image_fill_rect(vt->image, 0, (vt->height - 1) * FONT_HEIGHT,
                            vt->width * FONT_WIDTH, FONT_HEIGHT,
                            color_table_rgb[0][TEXT_ATTRIBUTES_DEFAULT.bgcol]);
            vt->update_x0 = 0;
            vt->update_y0 = 0;
            vt->update_x1 = vt->width * FONT_WIDTH;
            vt->update_y1 = vt->height * FONT_HEIGHT;
        }
    }
}

/* Set console attributes depending on the current escape codes.
 * NOTE: I know this code is not very efficient (checking every color for it
 * self) but it is more readable and better maintainable.
 */
static void vc_handle_escape(VCChardev *vc)
{
    int i;

    for (i = 0; i < vc->nb_esc_params; i++) {
        switch (vc->esc_params[i]) {
            case 0: /* reset all console attributes to default */
                vc->t_attrib = TEXT_ATTRIBUTES_DEFAULT;
                break;
            case 1:
                vc->t_attrib.bold = 1;
                break;
            case 4:
                vc->t_attrib.uline = 1;
                break;
            case 5:
                vc->t_attrib.blink = 1;
                break;
            case 7:
                vc->t_attrib.invers = 1;
                break;
            case 8:
                vc->t_attrib.unvisible = 1;
                break;
            case 22:
                vc->t_attrib.bold = 0;
                break;
            case 24:
                vc->t_attrib.uline = 0;
                break;
            case 25:
                vc->t_attrib.blink = 0;
                break;
            case 27:
                vc->t_attrib.invers = 0;
                break;
            case 28:
                vc->t_attrib.unvisible = 0;
                break;
            /* set foreground color */
            case 30:
                vc->t_attrib.fgcol = QEMU_COLOR_BLACK;
                break;
            case 31:
                vc->t_attrib.fgcol = QEMU_COLOR_RED;
                break;
            case 32:
                vc->t_attrib.fgcol = QEMU_COLOR_GREEN;
                break;
            case 33:
                vc->t_attrib.fgcol = QEMU_COLOR_YELLOW;
                break;
            case 34:
                vc->t_attrib.fgcol = QEMU_COLOR_BLUE;
                break;
            case 35:
                vc->t_attrib.fgcol = QEMU_COLOR_MAGENTA;
                break;
            case 36:
                vc->t_attrib.fgcol = QEMU_COLOR_CYAN;
                break;
            case 37:
                vc->t_attrib.fgcol = QEMU_COLOR_WHITE;
                break;
            /* set background color */
            case 40:
                vc->t_attrib.bgcol = QEMU_COLOR_BLACK;
                break;
            case 41:
                vc->t_attrib.bgcol = QEMU_COLOR_RED;
                break;
            case 42:
                vc->t_attrib.bgcol = QEMU_COLOR_GREEN;
                break;
            case 43:
                vc->t_attrib.bgcol = QEMU_COLOR_YELLOW;
                break;
            case 44:
                vc->t_attrib.bgcol = QEMU_COLOR_BLUE;
                break;
            case 45:
                vc->t_attrib.bgcol = QEMU_COLOR_MAGENTA;
                break;
            case 46:
                vc->t_attrib.bgcol = QEMU_COLOR_CYAN;
                break;
            case 47:
                vc->t_attrib.bgcol = QEMU_COLOR_WHITE;
                break;
        }
    }
}

static void vc_update_xy(VCChardev *vc, int x, int y)
{
    QemuTextConsole *s = vc->console;
    TextCell *c;
    int y1, y2;

    s->vt.text_x[0] = MIN(s->vt.text_x[0], x);
    s->vt.text_x[1] = MAX(s->vt.text_x[1], x);
    s->vt.text_y[0] = MIN(s->vt.text_y[0], y);
    s->vt.text_y[1] = MAX(s->vt.text_y[1], y);

    y1 = (s->vt.y_base + y) % s->vt.total_height;
    y2 = y1 - s->vt.y_displayed;
    if (y2 < 0) {
        y2 += s->vt.total_height;
    }
    if (y2 < s->vt.height) {
        if (x >= s->vt.width) {
            x = s->vt.width - 1;
        }
        c = &s->vt.cells[y1 * s->vt.width + x];
        vt100_putcharxy(&s->vt, x, y2, c->ch,
                      &(c->t_attrib));
        vt100_invalidate_xy(&s->vt, x, y2);
    }
}

static void vc_clear_xy(VCChardev *vc, int x, int y)
{
    QemuTextConsole *s = vc->console;
    int y1 = (s->vt.y_base + y) % s->vt.total_height;
    if (x >= s->vt.width) {
        x = s->vt.width - 1;
    }
    TextCell *c = &s->vt.cells[y1 * s->vt.width + x];
    c->ch = ' ';
    c->t_attrib = TEXT_ATTRIBUTES_DEFAULT;
    vc_update_xy(vc, x, y);
}

/*
 * UTF-8 DFA decoder by Bjoern Hoehrmann.
 * Copyright (c) 2008-2010 Bjoern Hoehrmann <bjoern@hoehrmann.de>
 * See https://github.com/polijan/utf8_decode for details.
 *
 * SPDX-License-Identifier: MIT
 */
#define UTF8_ACCEPT 0
#define UTF8_REJECT 12

static const uint8_t utf8d[] = {
    /* character class lookup */
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
   10,3,3,3,3,3,3,3,3,3,3,3,3,4,3,3, 11,6,6,6,5,8,8,8,8,8,8,8,8,8,8,8,

    /* state transition lookup */
     0,12,24,36,60,96,84,12,12,12,48,72, 12,12,12,12,12,12,12,12,12,12,12,12,
    12, 0,12,12,12,12,12, 0,12, 0,12,12, 12,24,12,12,12,12,12,24,12,24,12,12,
    12,12,12,12,12,12,12,24,12,12,12,12, 12,24,12,12,12,12,12,12,12,24,12,12,
    12,12,12,12,12,12,12,36,12,36,12,12, 12,36,12,12,12,12,12,36,12,36,12,12,
    12,36,12,12,12,12,12,12,12,12,12,12,
};

static uint32_t utf8_decode(uint32_t *state, uint32_t *codep, uint32_t byte)
{
    uint32_t type = utf8d[byte];

    *codep = (*state != UTF8_ACCEPT) ?
        (byte & 0x3fu) | (*codep << 6) :
        (0xffu >> type) & (byte);

    *state = utf8d[256 + *state + type];
    return *state;
}

static void vc_put_one(VCChardev *vc, int ch)
{
    QemuTextConsole *s = vc->console;
    TextCell *c;
    int y1;
    if (s->vt.x >= s->vt.width) {
        /* line wrap */
        s->vt.x = 0;
        vt100_put_lf(&s->vt);
    }
    y1 = (s->vt.y_base + s->vt.y) % s->vt.total_height;
    c = &s->vt.cells[y1 * s->vt.width + s->vt.x];
    c->ch = ch;
    c->t_attrib = vc->t_attrib;
    vc_update_xy(vc, s->vt.x, s->vt.y);
    s->vt.x++;
}

/* set cursor, checking bounds */
static void vc_set_cursor(VCChardev *vc, int x, int y)
{
    QemuTextConsole *s = vc->console;

    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if (y >= s->vt.height) {
        y = s->vt.height - 1;
    }
    if (x >= s->vt.width) {
        x = s->vt.width - 1;
    }

    s->vt.x = x;
    s->vt.y = y;
}

/**
 * vc_csi_P() - (DCH) deletes one or more characters from the cursor
 * position to the right. As characters are deleted, the remaining
 * characters between the cursor and right margin move to the
 * left. Character attributes move with the characters.
 */
static void vc_csi_P(struct VCChardev *vc, unsigned int nr)
{
    QemuTextConsole *s = vc->console;
    TextCell *c1, *c2;
    unsigned int x1, x2, y;
    unsigned int end, len;

    if (!nr) {
        nr = 1;
    }
    if (nr > s->vt.width - s->vt.x) {
        nr = s->vt.width - s->vt.x;
        if (!nr) {
            return;
        }
    }

    x1 = s->vt.x;
    x2 = s->vt.x + nr;
    len = s->vt.width - x2;
    if (len) {
        y = (s->vt.y_base + s->vt.y) % s->vt.total_height;
        c1 = &s->vt.cells[y * s->vt.width + x1];
        c2 = &s->vt.cells[y * s->vt.width + x2];
        memmove(c1, c2, len * sizeof(*c1));
        for (end = x1 + len; x1 < end; x1++) {
            vc_update_xy(vc, x1, s->vt.y);
        }
    }
    /* Clear the rest */
    for (; x1 < s->vt.width; x1++) {
        vc_clear_xy(vc, x1, s->vt.y);
    }
}

/**
 * vc_csi_at() - (ICH) inserts `nr` blank characters with the default
 * character attribute. The cursor remains at the beginning of the
 * blank characters. Text between the cursor and right margin moves to
 * the right. Characters scrolled past the right margin are lost.
 */
static void vc_csi_at(struct VCChardev *vc, unsigned int nr)
{
    QemuTextConsole *s = vc->console;
    TextCell *c1, *c2;
    unsigned int x1, x2, y;
    unsigned int end, len;

    if (!nr) {
        nr = 1;
    }
    if (nr > s->vt.width - s->vt.x) {
        nr = s->vt.width - s->vt.x;
        if (!nr) {
            return;
        }
    }

    x1 = s->vt.x + nr;
    x2 = s->vt.x;
    len = s->vt.width - x1;
    if (len) {
        y = (s->vt.y_base + s->vt.y) % s->vt.total_height;
        c1 = &s->vt.cells[y * s->vt.width + x1];
        c2 = &s->vt.cells[y * s->vt.width + x2];
        memmove(c1, c2, len * sizeof(*c1));
        for (end = x1 + len; x1 < end; x1++) {
            vc_update_xy(vc, x1, s->vt.y);
        }
    }
    /* Insert blanks */
    for (x1 = s->vt.x; x1 < s->vt.x + nr; x1++) {
        vc_clear_xy(vc, x1, s->vt.y);
    }
}

/**
 * vc_save_cursor() - saves cursor position and character attributes.
 */
static void vc_save_cursor(VCChardev *vc)
{
    QemuTextConsole *s = vc->console;

    vc->x_saved = s->vt.x;
    vc->y_saved = s->vt.y;
    vc->t_attrib_saved = vc->t_attrib;
}

/**
 * vc_restore_cursor() - restores cursor position and character
 * attributes from saved state.
 */
static void vc_restore_cursor(VCChardev *vc)
{
    QemuTextConsole *s = vc->console;

    s->vt.x = vc->x_saved;
    s->vt.y = vc->y_saved;
    vc->t_attrib = vc->t_attrib_saved;
}

static void vc_putchar(VCChardev *vc, int ch)
{
    QemuTextConsole *s = vc->console;
    int i;
    int x, y;
    g_autofree char *response = NULL;

    switch(vc->state) {
    case TTY_STATE_NORM:
        /* Feed byte through the UTF-8 DFA decoder */
        if (ch >= 0x80) {
            switch (utf8_decode(&vc->utf8_state, &vc->utf8_codepoint, ch)) {
            case UTF8_ACCEPT:
                vc_put_one(vc, unicode_to_cp437(vc->utf8_codepoint));
                break;
            case UTF8_REJECT:
                /* Reset state so the decoder can resync */
                vc->utf8_state = UTF8_ACCEPT;
                break;
            default:
                /* Need more bytes */
                break;
            }
            break;
        }
        /* ASCII byte: abort any pending UTF-8 sequence */
        vc->utf8_state = UTF8_ACCEPT;
        switch(ch) {
        case '\r':  /* carriage return */
            s->vt.x = 0;
            break;
        case '\n':  /* newline */
            vt100_put_lf(&s->vt);
            break;
        case '\b':  /* backspace */
            if (s->vt.x > 0)
                s->vt.x--;
            break;
        case '\t':  /* tabspace */
            if (s->vt.x + (8 - (s->vt.x % 8)) > s->vt.width) {
                s->vt.x = 0;
                vt100_put_lf(&s->vt);
            } else {
                s->vt.x = s->vt.x + (8 - (s->vt.x % 8));
            }
            break;
        case '\a':  /* alert aka. bell */
            /* TODO: has to be implemented */
            break;
        case 14:
            /* SO (shift out), character set 1 (ignored) */
            break;
        case 15:
            /* SI (shift in), character set 0 (ignored) */
            break;
        case 27:    /* esc (introducing an escape sequence) */
            vc->state = TTY_STATE_ESC;
            break;
        default:
            vc_put_one(vc, ch);
            break;
        }
        break;
    case TTY_STATE_ESC: /* check if it is a terminal escape sequence */
        if (ch == '[') {
            for(i=0;i<MAX_ESC_PARAMS;i++)
                vc->esc_params[i] = 0;
            vc->nb_esc_params = 0;
            vc->state = TTY_STATE_CSI;
        } else if (ch == '(') {
            vc->state = TTY_STATE_G0;
        } else if (ch == ')') {
            vc->state = TTY_STATE_G1;
        } else if (ch == ']' || ch == 'P' || ch == 'X'
                   || ch == '^' || ch == '_') {
            /* String sequences: OSC, DCS, SOS, PM, APC */
            vc->state = TTY_STATE_OSC;
        } else if (ch == '7') {
            vc_save_cursor(vc);
            vc->state = TTY_STATE_NORM;
        } else if (ch == '8') {
            vc_restore_cursor(vc);
            vc->state = TTY_STATE_NORM;
        } else {
            vc->state = TTY_STATE_NORM;
        }
        break;
    case TTY_STATE_CSI: /* handle escape sequence parameters */
        if (ch >= '0' && ch <= '9') {
            if (vc->nb_esc_params < MAX_ESC_PARAMS) {
                int *param = &vc->esc_params[vc->nb_esc_params];
                int digit = (ch - '0');

                *param = (*param <= (INT_MAX - digit) / 10) ?
                         *param * 10 + digit : INT_MAX;
            }
        } else {
            if (vc->nb_esc_params < MAX_ESC_PARAMS)
                vc->nb_esc_params++;
            if (ch == ';' || ch == '?') {
                break;
            }
            trace_console_putchar_csi(vc->esc_params[0], vc->esc_params[1],
                                      ch, vc->nb_esc_params);
            vc->state = TTY_STATE_NORM;
            switch(ch) {
            case 'A':
                /* move cursor up */
                if (vc->esc_params[0] == 0) {
                    vc->esc_params[0] = 1;
                }
                vc_set_cursor(vc, s->vt.x, s->vt.y - vc->esc_params[0]);
                break;
            case 'B':
                /* move cursor down */
                if (vc->esc_params[0] == 0) {
                    vc->esc_params[0] = 1;
                }
                vc_set_cursor(vc, s->vt.x, s->vt.y + vc->esc_params[0]);
                break;
            case 'C':
                /* move cursor right */
                if (vc->esc_params[0] == 0) {
                    vc->esc_params[0] = 1;
                }
                vc_set_cursor(vc, s->vt.x + vc->esc_params[0], s->vt.y);
                break;
            case 'D':
                /* move cursor left */
                if (vc->esc_params[0] == 0) {
                    vc->esc_params[0] = 1;
                }
                vc_set_cursor(vc, s->vt.x - vc->esc_params[0], s->vt.y);
                break;
            case 'G':
                /* move cursor to column */
                vc_set_cursor(vc, vc->esc_params[0] - 1, s->vt.y);
                break;
            case 'f':
            case 'H':
                /* move cursor to row, column */
                vc_set_cursor(vc, vc->esc_params[1] - 1, vc->esc_params[0] - 1);
                break;
            case 'J':
                switch (vc->esc_params[0]) {
                case 0:
                    /* clear to end of screen */
                    for (y = s->vt.y; y < s->vt.height; y++) {
                        for (x = 0; x < s->vt.width; x++) {
                            if (y == s->vt.y && x < s->vt.x) {
                                continue;
                            }
                            vc_clear_xy(vc, x, y);
                        }
                    }
                    break;
                case 1:
                    /* clear from beginning of screen */
                    for (y = 0; y <= s->vt.y; y++) {
                        for (x = 0; x < s->vt.width; x++) {
                            if (y == s->vt.y && x > s->vt.x) {
                                break;
                            }
                            vc_clear_xy(vc, x, y);
                        }
                    }
                    break;
                case 2:
                    /* clear entire screen */
                    for (y = 0; y < s->vt.height; y++) {
                        for (x = 0; x < s->vt.width; x++) {
                            vc_clear_xy(vc, x, y);
                        }
                    }
                    break;
                }
                break;
            case 'K':
                switch (vc->esc_params[0]) {
                case 0:
                    /* clear to eol */
                    for(x = s->vt.x; x < s->vt.width; x++) {
                        vc_clear_xy(vc, x, s->vt.y);
                    }
                    break;
                case 1:
                    /* clear from beginning of line */
                    for (x = 0; x <= s->vt.x && x < s->vt.width; x++) {
                        vc_clear_xy(vc, x, s->vt.y);
                    }
                    break;
                case 2:
                    /* clear entire line */
                    for(x = 0; x < s->vt.width; x++) {
                        vc_clear_xy(vc, x, s->vt.y);
                    }
                    break;
                }
                break;
            case 'P':
                vc_csi_P(vc, vc->esc_params[0]);
                break;
            case 'm':
                vc_handle_escape(vc);
                break;
            case 'n':
                switch (vc->esc_params[0]) {
                case 5:
                    /* report console status (always succeed)*/
                    qemu_text_console_write(s, "\033[0n", 4);
                    break;
                case 6:
                    /* report cursor position */
                    response = g_strdup_printf("\033[%d;%dR",
                                               s->vt.y + 1, s->vt.x + 1);
                    qemu_text_console_write(s, response, strlen(response));
                    break;
                }
                break;
            case 's':
                vc_save_cursor(vc);
                break;
            case 'u':
                vc_restore_cursor(vc);
                break;
            case '@':
                vc_csi_at(vc, vc->esc_params[0]);
                break;
            default:
                trace_console_putchar_unhandled(ch);
                break;
            }
            break;
        }
        break;
    case TTY_STATE_OSC: /* Operating System Command: ESC ] ... BEL/ST */
        if (ch == '\a') {
            /* BEL terminates OSC */
            vc->state = TTY_STATE_NORM;
        } else if (ch == 27) {
            /* ESC might start ST (ESC \) */
            vc->state = TTY_STATE_ESC;
        }
        /* All other bytes are silently consumed */
        break;
    case TTY_STATE_G0: /* set character sets */
    case TTY_STATE_G1: /* set character sets */
        switch (ch) {
        case 'B':
            /* Latin-1 map */
            break;
        }
        vc->state = TTY_STATE_NORM;
        break;
    }
}

#define TYPE_CHARDEV_VC "chardev-vc"
DECLARE_INSTANCE_CHECKER(VCChardev, VC_CHARDEV,
                         TYPE_CHARDEV_VC)

static int vc_chr_write(Chardev *chr, const uint8_t *buf, int len)
{
    VCChardev *drv = VC_CHARDEV(chr);
    QemuTextConsole *s = drv->console;
    int i;

    s->vt.update_x0 = s->vt.width * FONT_WIDTH;
    s->vt.update_y0 = s->vt.height * FONT_HEIGHT;
    s->vt.update_x1 = 0;
    s->vt.update_y1 = 0;
    vt100_show_cursor(&s->vt, 0);
    for(i = 0; i < len; i++) {
        vc_putchar(drv, buf[i]);
    }
    vt100_show_cursor(&s->vt, 1);
    if (s->vt.update_x0 < s->vt.update_x1) {
        vt100_image_update(&s->vt, s->vt.update_x0, s->vt.update_y0,
                           s->vt.update_x1 - s->vt.update_x0,
                           s->vt.update_y1 - s->vt.update_y0);
    }
    return len;
}

void vt100_update_cursor(void)
{
    QemuVT100 *vt;

    cursor_visible_phase = !cursor_visible_phase;

    if (QTAILQ_EMPTY(&vt100s)) {
        return;
    }

    QTAILQ_FOREACH(vt, &vt100s, list) {
        vt100_refresh(vt);
    }

    timer_mod(cursor_timer,
        qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + CONSOLE_CURSOR_PERIOD / 2);
}

static void
cursor_timer_cb(void *opaque)
{
    vt100_update_cursor();
}

static void text_console_invalidate(void *opaque)
{
    QemuTextConsole *s = QEMU_TEXT_CONSOLE(opaque);

    if (!QEMU_IS_FIXED_TEXT_CONSOLE(s)) {
        vt100_set_image(&s->vt, QEMU_CONSOLE(s)->surface->image);
    }
    vt100_refresh(&s->vt);
}

static void
qemu_text_console_finalize(Object *obj)
{
    QemuTextConsole *s = QEMU_TEXT_CONSOLE(obj);

    QTAILQ_REMOVE(&vt100s, &s->vt, list);
}

static void
qemu_text_console_class_init(ObjectClass *oc, const void *data)
{
    QemuConsoleClass *cc = QEMU_CONSOLE_CLASS(oc);

    if (!cursor_timer) {
        cursor_timer = timer_new_ms(QEMU_CLOCK_REALTIME, cursor_timer_cb, NULL);
    }

    cc->get_label = qemu_text_console_get_label;
}

static const GraphicHwOps text_console_ops = {
    .invalidate  = text_console_invalidate,
    .text_update = text_console_update,
};

static void
qemu_text_console_init(Object *obj)
{
    QemuTextConsole *c = QEMU_TEXT_CONSOLE(obj);

    QTAILQ_INSERT_HEAD(&vt100s, &c->vt, list);
    fifo8_create(&c->out_fifo, 16);
    c->vt.total_height = DEFAULT_BACKSCROLL;
    QEMU_CONSOLE(c)->hw_ops = &text_console_ops;
    QEMU_CONSOLE(c)->hw = c;
}

static void
qemu_fixed_text_console_finalize(Object *obj)
{
}

static void
qemu_fixed_text_console_class_init(ObjectClass *oc, const void *data)
{
}

static void
qemu_fixed_text_console_init(Object *obj)
{
}

static void vc_chr_accept_input(Chardev *chr)
{
    VCChardev *drv = VC_CHARDEV(chr);

    qemu_text_console_flush(drv->console);
}

static void vc_chr_set_echo(Chardev *chr, bool echo)
{
    VCChardev *drv = VC_CHARDEV(chr);

    drv->console->vt.echo = echo;
}

void qemu_text_console_update_size(QemuTextConsole *c)
{
    dpy_text_resize(QEMU_CONSOLE(c), c->vt.width, c->vt.height);
}

static void text_console_image_update(QemuVT100 *vt, int x, int y, int width, int height)
{
    QemuTextConsole *console = container_of(vt, QemuTextConsole, vt);

    dpy_gfx_update(QEMU_CONSOLE(console), x, y, width, height);
}

static bool vc_chr_open(Chardev *chr, ChardevBackend *backend, Error **errp)
{
    ChardevVC *vc = backend->u.vc.data;
    VCChardev *drv = VC_CHARDEV(chr);
    QemuTextConsole *s;
    unsigned width = 0;
    unsigned height = 0;

    if (vc->has_width) {
        width = vc->width;
    } else if (vc->has_cols) {
        width = vc->cols * FONT_WIDTH;
    }

    if (vc->has_height) {
        height = vc->height;
    } else if (vc->has_rows) {
        height = vc->rows * FONT_HEIGHT;
    }

    trace_console_txt_new(width, height);
    if (width == 0 || height == 0) {
        s = QEMU_TEXT_CONSOLE(object_new(TYPE_QEMU_TEXT_CONSOLE));
        width = 80 * FONT_WIDTH;
        height = 24 * FONT_HEIGHT;
    } else {
        s = QEMU_TEXT_CONSOLE(object_new(TYPE_QEMU_FIXED_TEXT_CONSOLE));
    }

    dpy_gfx_replace_surface(QEMU_CONSOLE(s), qemu_create_displaysurface(width, height));
    s->vt.image_update = text_console_image_update;

    s->chr = chr;
    drv->console = s;

    /* set current text attributes to default */
    drv->t_attrib = TEXT_ATTRIBUTES_DEFAULT;
    vt100_set_image(&s->vt, QEMU_CONSOLE(s)->surface->image);

    if (chr->label) {
        char *msg;

        drv->t_attrib.bgcol = QEMU_COLOR_BLUE;
        msg = g_strdup_printf("%s console\r\n", chr->label);
        qemu_chr_write(chr, (uint8_t *)msg, strlen(msg), true);
        g_free(msg);
        drv->t_attrib = TEXT_ATTRIBUTES_DEFAULT;
    }

    qemu_chr_be_event(chr, CHR_EVENT_OPENED);
    return true;
}

static void vc_chr_parse(QemuOpts *opts, ChardevBackend *backend, Error **errp)
{
    int val;
    ChardevVC *vc;

    backend->type = CHARDEV_BACKEND_KIND_VC;
    vc = backend->u.vc.data = g_new0(ChardevVC, 1);
    qemu_chr_parse_common(opts, qapi_ChardevVC_base(vc));

    val = qemu_opt_get_number(opts, "width", 0);
    if (val != 0) {
        vc->has_width = true;
        vc->width = val;
    }

    val = qemu_opt_get_number(opts, "height", 0);
    if (val != 0) {
        vc->has_height = true;
        vc->height = val;
    }

    val = qemu_opt_get_number(opts, "cols", 0);
    if (val != 0) {
        vc->has_cols = true;
        vc->cols = val;
    }

    val = qemu_opt_get_number(opts, "rows", 0);
    if (val != 0) {
        vc->has_rows = true;
        vc->rows = val;
    }
}

static void char_vc_class_init(ObjectClass *oc, const void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->chr_parse = vc_chr_parse;
    cc->chr_open = vc_chr_open;
    cc->chr_write = vc_chr_write;
    cc->chr_accept_input = vc_chr_accept_input;
    cc->chr_set_echo = vc_chr_set_echo;
}

static const TypeInfo char_vc_type_info = {
    .name = TYPE_CHARDEV_VC,
    .parent = TYPE_CHARDEV,
    .instance_size = sizeof(VCChardev),
    .class_init = char_vc_class_init,
};

void qemu_console_early_init(void)
{
    /* set the default vc driver */
    if (!object_class_by_name(TYPE_CHARDEV_VC)) {
        type_register_static(&char_vc_type_info);
    }
}
