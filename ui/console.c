/*
 * QEMU graphical console
 *
 * Copyright (c) 2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "ui/console.h"
#include "hw/qdev-core.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-ui.h"
#include "qapi/visitor.h"
#include "qemu/coroutine.h"
#include "qemu/fifo8.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/timer.h"
#include "chardev/char.h"
#include "trace.h"
#include "exec/memory.h"
#include "qom/object.h"

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
};

struct QemuConsole {
    Object parent;

    int index;
    DisplayState *ds;
    DisplaySurface *surface;
    DisplayScanout scanout;
    int dcls;
    DisplayGLCtx *gl;
    int gl_block;
    QEMUTimer *gl_unblock_timer;
    int window_id;
    QemuUIInfo ui_info;
    QEMUTimer *ui_timer;
    const GraphicHwOps *hw_ops;
    void *hw;
    CoQueue dump_queue;

    QTAILQ_ENTRY(QemuConsole) next;
};

OBJECT_DEFINE_ABSTRACT_TYPE(QemuConsole, qemu_console, QEMU_CONSOLE, OBJECT)

typedef struct QemuGraphicConsole {
    QemuConsole parent;

    Object *device;
    uint32_t head;

    QEMUCursor *cursor;
    int cursor_x, cursor_y, cursor_on;
} QemuGraphicConsole;

typedef QemuConsoleClass QemuGraphicConsoleClass;

OBJECT_DEFINE_TYPE(QemuGraphicConsole, qemu_graphic_console, QEMU_GRAPHIC_CONSOLE, QEMU_CONSOLE)

typedef struct QemuTextConsole {
    QemuConsole parent;

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
    TextAttributes t_attrib; /* currently active text attributes */
    int x_saved, y_saved;
};
typedef struct VCChardev VCChardev;

struct DisplayState {
    QEMUTimer *gui_timer;
    uint64_t last_update;
    uint64_t update_interval;
    bool refreshing;

    QLIST_HEAD(, DisplayChangeListener) listeners;
};

static DisplayState *display_state;
static QemuConsole *active_console;
static QTAILQ_HEAD(, QemuConsole) consoles =
    QTAILQ_HEAD_INITIALIZER(consoles);
static bool cursor_visible_phase;
static QEMUTimer *cursor_timer;

static void dpy_refresh(DisplayState *s);
static DisplayState *get_alloc_displaystate(void);
static void text_console_update_cursor(void *opaque);
static bool displaychangelistener_has_dmabuf(DisplayChangeListener *dcl);
static bool console_compatible_with(QemuConsole *con,
                                    DisplayChangeListener *dcl, Error **errp);
static QemuConsole *qemu_graphic_console_lookup_unused(void);
static void dpy_set_ui_info_timer(void *opaque);

static void gui_update(void *opaque)
{
    uint64_t interval = GUI_REFRESH_INTERVAL_IDLE;
    uint64_t dcl_interval;
    DisplayState *ds = opaque;
    DisplayChangeListener *dcl;

    ds->refreshing = true;
    dpy_refresh(ds);
    ds->refreshing = false;

    QLIST_FOREACH(dcl, &ds->listeners, next) {
        dcl_interval = dcl->update_interval ?
            dcl->update_interval : GUI_REFRESH_INTERVAL_DEFAULT;
        if (interval > dcl_interval) {
            interval = dcl_interval;
        }
    }
    if (ds->update_interval != interval) {
        ds->update_interval = interval;
        trace_console_refresh(interval);
    }
    ds->last_update = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
    timer_mod(ds->gui_timer, ds->last_update + interval);
}

static void gui_setup_refresh(DisplayState *ds)
{
    DisplayChangeListener *dcl;
    bool need_timer = false;

    QLIST_FOREACH(dcl, &ds->listeners, next) {
        if (dcl->ops->dpy_refresh != NULL) {
            need_timer = true;
        }
    }

    if (need_timer && ds->gui_timer == NULL) {
        ds->gui_timer = timer_new_ms(QEMU_CLOCK_REALTIME, gui_update, ds);
        timer_mod(ds->gui_timer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME));
    }
    if (!need_timer && ds->gui_timer != NULL) {
        timer_free(ds->gui_timer);
        ds->gui_timer = NULL;
    }
}

void graphic_hw_update_done(QemuConsole *con)
{
    if (con) {
        qemu_co_enter_all(&con->dump_queue, NULL);
    }
}

void graphic_hw_update(QemuConsole *con)
{
    bool async = false;
    con = con ? con : active_console;
    if (!con) {
        return;
    }
    if (con->hw_ops->gfx_update) {
        con->hw_ops->gfx_update(con->hw);
        async = con->hw_ops->gfx_update_async;
    }
    if (!async) {
        graphic_hw_update_done(con);
    }
}

static void graphic_hw_update_bh(void *con)
{
    graphic_hw_update(con);
}

void qemu_console_co_wait_update(QemuConsole *con)
{
    if (qemu_co_queue_empty(&con->dump_queue)) {
        /* Defer the update, it will restart the pending coroutines */
        aio_bh_schedule_oneshot(qemu_get_aio_context(),
                                graphic_hw_update_bh, con);
    }
    qemu_co_queue_wait(&con->dump_queue, NULL);

}

static void graphic_hw_gl_unblock_timer(void *opaque)
{
    warn_report("console: no gl-unblock within one second");
}

void graphic_hw_gl_block(QemuConsole *con, bool block)
{
    uint64_t timeout;
    assert(con != NULL);

    if (block) {
        con->gl_block++;
    } else {
        con->gl_block--;
    }
    assert(con->gl_block >= 0);
    if (!con->hw_ops->gl_block) {
        return;
    }
    if ((block && con->gl_block != 1) || (!block && con->gl_block != 0)) {
        return;
    }
    con->hw_ops->gl_block(con->hw, block);

    if (block) {
        timeout = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
        timeout += 1000; /* one sec */
        timer_mod(con->gl_unblock_timer, timeout);
    } else {
        timer_del(con->gl_unblock_timer);
    }
}

int qemu_console_get_window_id(QemuConsole *con)
{
    return con->window_id;
}

void qemu_console_set_window_id(QemuConsole *con, int window_id)
{
    con->window_id = window_id;
}

void graphic_hw_invalidate(QemuConsole *con)
{
    if (!con) {
        con = active_console;
    }
    if (con && con->hw_ops->invalidate) {
        con->hw_ops->invalidate(con->hw);
    }
}

void graphic_hw_text_update(QemuConsole *con, console_ch_t *chardata)
{
    if (!con) {
        con = active_console;
    }
    if (con && con->hw_ops->text_update) {
        con->hw_ops->text_update(con->hw, chardata);
    }
}

static void qemu_console_fill_rect(QemuConsole *con, int posx, int posy,
                                   int width, int height, pixman_color_t color)
{
    DisplaySurface *surface = qemu_console_surface(con);
    pixman_rectangle16_t rect = {
        .x = posx, .y = posy, .width = width, .height = height
    };

    assert(surface);
    pixman_image_fill_rectangles(PIXMAN_OP_SRC, surface->image,
                                 &color, 1, &rect);
}

/* copy from (xs, ys) to (xd, yd) a rectangle of size (w, h) */
static void qemu_console_bitblt(QemuConsole *con,
                                int xs, int ys, int xd, int yd, int w, int h)
{
    DisplaySurface *surface = qemu_console_surface(con);

    assert(surface);
    pixman_image_composite(PIXMAN_OP_SRC,
                           surface->image, NULL, surface->image,
                           xs, ys, 0, 0, xd, yd, w, h);
}

/***********************************************************/
/* basic char display */

#define FONT_HEIGHT 16
#define FONT_WIDTH 8

#include "vgafont.h"

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

static void vga_putcharxy(QemuConsole *s, int x, int y, int ch,
                          TextAttributes *t_attrib)
{
    static pixman_image_t *glyphs[256];
    DisplaySurface *surface = qemu_console_surface(s);
    pixman_color_t fgcol, bgcol;

    assert(surface);
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
    qemu_pixman_glyph_render(glyphs[ch], surface->image,
                             &fgcol, &bgcol, x, y, FONT_WIDTH, FONT_HEIGHT);
}

static void text_console_resize(QemuTextConsole *t)
{
    QemuConsole *s = QEMU_CONSOLE(t);
    TextCell *cells, *c, *c1;
    int w1, x, y, last_width, w, h;

    assert(s->scanout.kind == SCANOUT_SURFACE);

    w = surface_width(s->surface) / FONT_WIDTH;
    h = surface_height(s->surface) / FONT_HEIGHT;
    if (w == t->width && h == t->height) {
        return;
    }

    last_width = t->width;
    t->width = w;
    t->height = h;

    w1 = MIN(t->width, last_width);

    cells = g_new(TextCell, t->width * t->total_height + 1);
    for (y = 0; y < t->total_height; y++) {
        c = &cells[y * t->width];
        if (w1 > 0) {
            c1 = &t->cells[y * last_width];
            for (x = 0; x < w1; x++) {
                *c++ = *c1++;
            }
        }
        for (x = w1; x < t->width; x++) {
            c->ch = ' ';
            c->t_attrib = TEXT_ATTRIBUTES_DEFAULT;
            c++;
        }
    }
    g_free(t->cells);
    t->cells = cells;
}

static void invalidate_xy(QemuTextConsole *s, int x, int y)
{
    if (!qemu_console_is_visible(QEMU_CONSOLE(s))) {
        return;
    }
    if (s->update_x0 > x * FONT_WIDTH)
        s->update_x0 = x * FONT_WIDTH;
    if (s->update_y0 > y * FONT_HEIGHT)
        s->update_y0 = y * FONT_HEIGHT;
    if (s->update_x1 < (x + 1) * FONT_WIDTH)
        s->update_x1 = (x + 1) * FONT_WIDTH;
    if (s->update_y1 < (y + 1) * FONT_HEIGHT)
        s->update_y1 = (y + 1) * FONT_HEIGHT;
}

static void vc_update_xy(VCChardev *vc, int x, int y)
{
    QemuTextConsole *s = vc->console;
    TextCell *c;
    int y1, y2;

    s->text_x[0] = MIN(s->text_x[0], x);
    s->text_x[1] = MAX(s->text_x[1], x);
    s->text_y[0] = MIN(s->text_y[0], y);
    s->text_y[1] = MAX(s->text_y[1], y);

    y1 = (s->y_base + y) % s->total_height;
    y2 = y1 - s->y_displayed;
    if (y2 < 0) {
        y2 += s->total_height;
    }
    if (y2 < s->height) {
        if (x >= s->width) {
            x = s->width - 1;
        }
        c = &s->cells[y1 * s->width + x];
        vga_putcharxy(QEMU_CONSOLE(s), x, y2, c->ch,
                      &(c->t_attrib));
        invalidate_xy(s, x, y2);
    }
}

static void console_show_cursor(QemuTextConsole *s, int show)
{
    TextCell *c;
    int y, y1;
    int x = s->x;

    s->cursor_invalidate = 1;

    if (x >= s->width) {
        x = s->width - 1;
    }
    y1 = (s->y_base + s->y) % s->total_height;
    y = y1 - s->y_displayed;
    if (y < 0) {
        y += s->total_height;
    }
    if (y < s->height) {
        c = &s->cells[y1 * s->width + x];
        if (show && cursor_visible_phase) {
            TextAttributes t_attrib = TEXT_ATTRIBUTES_DEFAULT;
            t_attrib.invers = !(t_attrib.invers); /* invert fg and bg */
            vga_putcharxy(QEMU_CONSOLE(s), x, y, c->ch, &t_attrib);
        } else {
            vga_putcharxy(QEMU_CONSOLE(s), x, y, c->ch, &(c->t_attrib));
        }
        invalidate_xy(s, x, y);
    }
}

static void console_refresh(QemuTextConsole *s)
{
    DisplaySurface *surface = qemu_console_surface(QEMU_CONSOLE(s));
    TextCell *c;
    int x, y, y1;

    assert(surface);
    s->text_x[0] = 0;
    s->text_y[0] = 0;
    s->text_x[1] = s->width - 1;
    s->text_y[1] = s->height - 1;
    s->cursor_invalidate = 1;

    qemu_console_fill_rect(QEMU_CONSOLE(s), 0, 0, surface_width(surface), surface_height(surface),
                           color_table_rgb[0][QEMU_COLOR_BLACK]);
    y1 = s->y_displayed;
    for (y = 0; y < s->height; y++) {
        c = s->cells + y1 * s->width;
        for (x = 0; x < s->width; x++) {
            vga_putcharxy(QEMU_CONSOLE(s), x, y, c->ch,
                          &(c->t_attrib));
            c++;
        }
        if (++y1 == s->total_height) {
            y1 = 0;
        }
    }
    console_show_cursor(s, 1);
    dpy_gfx_update(QEMU_CONSOLE(s), 0, 0,
                   surface_width(surface), surface_height(surface));
}

static void console_scroll(QemuTextConsole *s, int ydelta)
{
    int i, y1;

    if (ydelta > 0) {
        for(i = 0; i < ydelta; i++) {
            if (s->y_displayed == s->y_base)
                break;
            if (++s->y_displayed == s->total_height)
                s->y_displayed = 0;
        }
    } else {
        ydelta = -ydelta;
        i = s->backscroll_height;
        if (i > s->total_height - s->height)
            i = s->total_height - s->height;
        y1 = s->y_base - i;
        if (y1 < 0)
            y1 += s->total_height;
        for(i = 0; i < ydelta; i++) {
            if (s->y_displayed == y1)
                break;
            if (--s->y_displayed < 0)
                s->y_displayed = s->total_height - 1;
        }
    }
    console_refresh(s);
}

static void vc_put_lf(VCChardev *vc)
{
    QemuTextConsole *s = vc->console;
    TextCell *c;
    int x, y1;

    s->y++;
    if (s->y >= s->height) {
        s->y = s->height - 1;

        if (s->y_displayed == s->y_base) {
            if (++s->y_displayed == s->total_height)
                s->y_displayed = 0;
        }
        if (++s->y_base == s->total_height)
            s->y_base = 0;
        if (s->backscroll_height < s->total_height)
            s->backscroll_height++;
        y1 = (s->y_base + s->height - 1) % s->total_height;
        c = &s->cells[y1 * s->width];
        for(x = 0; x < s->width; x++) {
            c->ch = ' ';
            c->t_attrib = TEXT_ATTRIBUTES_DEFAULT;
            c++;
        }
        if (s->y_displayed == s->y_base) {
            s->text_x[0] = 0;
            s->text_y[0] = 0;
            s->text_x[1] = s->width - 1;
            s->text_y[1] = s->height - 1;

            qemu_console_bitblt(QEMU_CONSOLE(s), 0, FONT_HEIGHT, 0, 0,
                                s->width * FONT_WIDTH,
                                (s->height - 1) * FONT_HEIGHT);
            qemu_console_fill_rect(QEMU_CONSOLE(s), 0, (s->height - 1) * FONT_HEIGHT,
                                   s->width * FONT_WIDTH, FONT_HEIGHT,
                                   color_table_rgb[0][TEXT_ATTRIBUTES_DEFAULT.bgcol]);
            s->update_x0 = 0;
            s->update_y0 = 0;
            s->update_x1 = s->width * FONT_WIDTH;
            s->update_y1 = s->height * FONT_HEIGHT;
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

static void vc_clear_xy(VCChardev *vc, int x, int y)
{
    QemuTextConsole *s = vc->console;
    int y1 = (s->y_base + y) % s->total_height;
    if (x >= s->width) {
        x = s->width - 1;
    }
    TextCell *c = &s->cells[y1 * s->width + x];
    c->ch = ' ';
    c->t_attrib = TEXT_ATTRIBUTES_DEFAULT;
    vc_update_xy(vc, x, y);
}

static void vc_put_one(VCChardev *vc, int ch)
{
    QemuTextConsole *s = vc->console;
    TextCell *c;
    int y1;
    if (s->x >= s->width) {
        /* line wrap */
        s->x = 0;
        vc_put_lf(vc);
    }
    y1 = (s->y_base + s->y) % s->total_height;
    c = &s->cells[y1 * s->width + s->x];
    c->ch = ch;
    c->t_attrib = vc->t_attrib;
    vc_update_xy(vc, s->x, s->y);
    s->x++;
}

static void vc_respond_str(VCChardev *vc, const char *buf)
{
    while (*buf) {
        vc_put_one(vc, *buf);
        buf++;
    }
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
    if (y >= s->height) {
        y = s->height - 1;
    }
    if (x >= s->width) {
        x = s->width - 1;
    }

    s->x = x;
    s->y = y;
}

static void vc_putchar(VCChardev *vc, int ch)
{
    QemuTextConsole *s = vc->console;
    int i;
    int x, y;
    char response[40];

    switch(vc->state) {
    case TTY_STATE_NORM:
        switch(ch) {
        case '\r':  /* carriage return */
            s->x = 0;
            break;
        case '\n':  /* newline */
            vc_put_lf(vc);
            break;
        case '\b':  /* backspace */
            if (s->x > 0)
                s->x--;
            break;
        case '\t':  /* tabspace */
            if (s->x + (8 - (s->x % 8)) > s->width) {
                s->x = 0;
                vc_put_lf(vc);
            } else {
                s->x = s->x + (8 - (s->x % 8));
            }
            break;
        case '\a':  /* alert aka. bell */
            /* TODO: has to be implemented */
            break;
        case 14:
            /* SI (shift in), character set 0 (ignored) */
            break;
        case 15:
            /* SO (shift out), character set 1 (ignored) */
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
                vc_set_cursor(vc, s->x, s->y - vc->esc_params[0]);
                break;
            case 'B':
                /* move cursor down */
                if (vc->esc_params[0] == 0) {
                    vc->esc_params[0] = 1;
                }
                vc_set_cursor(vc, s->x, s->y + vc->esc_params[0]);
                break;
            case 'C':
                /* move cursor right */
                if (vc->esc_params[0] == 0) {
                    vc->esc_params[0] = 1;
                }
                vc_set_cursor(vc, s->x + vc->esc_params[0], s->y);
                break;
            case 'D':
                /* move cursor left */
                if (vc->esc_params[0] == 0) {
                    vc->esc_params[0] = 1;
                }
                vc_set_cursor(vc, s->x - vc->esc_params[0], s->y);
                break;
            case 'G':
                /* move cursor to column */
                vc_set_cursor(vc, vc->esc_params[0] - 1, s->y);
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
                    for (y = s->y; y < s->height; y++) {
                        for (x = 0; x < s->width; x++) {
                            if (y == s->y && x < s->x) {
                                continue;
                            }
                            vc_clear_xy(vc, x, y);
                        }
                    }
                    break;
                case 1:
                    /* clear from beginning of screen */
                    for (y = 0; y <= s->y; y++) {
                        for (x = 0; x < s->width; x++) {
                            if (y == s->y && x > s->x) {
                                break;
                            }
                            vc_clear_xy(vc, x, y);
                        }
                    }
                    break;
                case 2:
                    /* clear entire screen */
                    for (y = 0; y <= s->height; y++) {
                        for (x = 0; x < s->width; x++) {
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
                    for(x = s->x; x < s->width; x++) {
                        vc_clear_xy(vc, x, s->y);
                    }
                    break;
                case 1:
                    /* clear from beginning of line */
                    for (x = 0; x <= s->x && x < s->width; x++) {
                        vc_clear_xy(vc, x, s->y);
                    }
                    break;
                case 2:
                    /* clear entire line */
                    for(x = 0; x < s->width; x++) {
                        vc_clear_xy(vc, x, s->y);
                    }
                    break;
                }
                break;
            case 'm':
                vc_handle_escape(vc);
                break;
            case 'n':
                switch (vc->esc_params[0]) {
                case 5:
                    /* report console status (always succeed)*/
                    vc_respond_str(vc, "\033[0n");
                    break;
                case 6:
                    /* report cursor position */
                    sprintf(response, "\033[%d;%dR",
                           (s->y_base + s->y) % s->total_height + 1,
                            s->x + 1);
                    vc_respond_str(vc, response);
                    break;
                }
                break;
            case 's':
                /* save cursor position */
                vc->x_saved = s->x;
                vc->y_saved = s->y;
                break;
            case 'u':
                /* restore cursor position */
                s->x = vc->x_saved;
                s->y = vc->y_saved;
                break;
            default:
                trace_console_putchar_unhandled(ch);
                break;
            }
            break;
        }
    }
}

static void displaychangelistener_gfx_switch(DisplayChangeListener *dcl,
                                             struct DisplaySurface *new_surface,
                                             bool update)
{
    if (dcl->ops->dpy_gfx_switch) {
        dcl->ops->dpy_gfx_switch(dcl, new_surface);
    }

    if (update && dcl->ops->dpy_gfx_update) {
        dcl->ops->dpy_gfx_update(dcl, 0, 0,
                                 surface_width(new_surface),
                                 surface_height(new_surface));
    }
}

static void dpy_gfx_create_texture(QemuConsole *con, DisplaySurface *surface)
{
    if (con->gl && con->gl->ops->dpy_gl_ctx_create_texture) {
        con->gl->ops->dpy_gl_ctx_create_texture(con->gl, surface);
    }
}

static void dpy_gfx_destroy_texture(QemuConsole *con, DisplaySurface *surface)
{
    if (con->gl && con->gl->ops->dpy_gl_ctx_destroy_texture) {
        con->gl->ops->dpy_gl_ctx_destroy_texture(con->gl, surface);
    }
}

static void dpy_gfx_update_texture(QemuConsole *con, DisplaySurface *surface,
                                   int x, int y, int w, int h)
{
    if (con->gl && con->gl->ops->dpy_gl_ctx_update_texture) {
        con->gl->ops->dpy_gl_ctx_update_texture(con->gl, surface, x, y, w, h);
    }
}

static void displaychangelistener_display_console(DisplayChangeListener *dcl,
                                                  QemuConsole *con,
                                                  Error **errp)
{
    static const char nodev[] =
        "This VM has no graphic display device.";
    static DisplaySurface *dummy;

    if (!con || !console_compatible_with(con, dcl, errp)) {
        if (!dummy) {
            dummy = qemu_create_placeholder_surface(640, 480, nodev);
        }
        if (con) {
            dpy_gfx_create_texture(con, dummy);
        }
        displaychangelistener_gfx_switch(dcl, dummy, TRUE);
        return;
    }

    dpy_gfx_create_texture(con, con->surface);
    displaychangelistener_gfx_switch(dcl, con->surface,
                                     con->scanout.kind == SCANOUT_SURFACE);

    if (con->scanout.kind == SCANOUT_DMABUF &&
        displaychangelistener_has_dmabuf(dcl)) {
        dcl->ops->dpy_gl_scanout_dmabuf(dcl, con->scanout.dmabuf);
    } else if (con->scanout.kind == SCANOUT_TEXTURE &&
               dcl->ops->dpy_gl_scanout_texture) {
        dcl->ops->dpy_gl_scanout_texture(dcl,
                                         con->scanout.texture.backing_id,
                                         con->scanout.texture.backing_y_0_top,
                                         con->scanout.texture.backing_width,
                                         con->scanout.texture.backing_height,
                                         con->scanout.texture.x,
                                         con->scanout.texture.y,
                                         con->scanout.texture.width,
                                         con->scanout.texture.height,
                                         con->scanout.texture.d3d_tex2d);
    }
}

void console_select(unsigned int index)
{
    DisplayChangeListener *dcl;
    QemuConsole *s;

    trace_console_select(index);
    s = qemu_console_lookup_by_index(index);
    if (s) {
        DisplayState *ds = s->ds;

        active_console = s;
        QLIST_FOREACH (dcl, &ds->listeners, next) {
            if (dcl->con != NULL) {
                continue;
            }
            displaychangelistener_display_console(dcl, s, NULL);
        }

        if (QEMU_IS_TEXT_CONSOLE(s)) {
            dpy_text_resize(s, QEMU_TEXT_CONSOLE(s)->width, QEMU_TEXT_CONSOLE(s)->height);
            text_console_update_cursor(NULL);
        }
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

    s->update_x0 = s->width * FONT_WIDTH;
    s->update_y0 = s->height * FONT_HEIGHT;
    s->update_x1 = 0;
    s->update_y1 = 0;
    console_show_cursor(s, 0);
    for(i = 0; i < len; i++) {
        vc_putchar(drv, buf[i]);
    }
    console_show_cursor(s, 1);
    if (s->update_x0 < s->update_x1) {
        dpy_gfx_update(QEMU_CONSOLE(s), s->update_x0, s->update_y0,
                       s->update_x1 - s->update_x0,
                       s->update_y1 - s->update_y0);
    }
    return len;
}

static void kbd_send_chars(QemuTextConsole *s)
{
    uint32_t len, avail;

    len = qemu_chr_be_can_write(s->chr);
    avail = fifo8_num_used(&s->out_fifo);
    while (len > 0 && avail > 0) {
        const uint8_t *buf;
        uint32_t size;

        buf = fifo8_pop_buf(&s->out_fifo, MIN(len, avail), &size);
        qemu_chr_be_write(s->chr, buf, size);
        len = qemu_chr_be_can_write(s->chr);
        avail -= size;
    }
}

/* called when an ascii key is pressed */
void kbd_put_keysym_console(QemuTextConsole *s, int keysym)
{
    uint8_t buf[16], *q;
    int c;
    uint32_t num_free;

    switch(keysym) {
    case QEMU_KEY_CTRL_UP:
        console_scroll(s, -1);
        break;
    case QEMU_KEY_CTRL_DOWN:
        console_scroll(s, 1);
        break;
    case QEMU_KEY_CTRL_PAGEUP:
        console_scroll(s, -10);
        break;
    case QEMU_KEY_CTRL_PAGEDOWN:
        console_scroll(s, 10);
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
        } else if (s->echo && (keysym == '\r' || keysym == '\n')) {
            qemu_chr_write(s->chr, (uint8_t *)"\r", 1, true);
            *q++ = '\n';
        } else {
            *q++ = keysym;
        }
        if (s->echo) {
            qemu_chr_write(s->chr, buf, q - buf, true);
        }
        num_free = fifo8_num_free(&s->out_fifo);
        fifo8_push_all(&s->out_fifo, buf, MIN(num_free, q - buf));
        kbd_send_chars(s);
        break;
    }
}

static const int qcode_to_keysym[Q_KEY_CODE__MAX] = {
    [Q_KEY_CODE_UP]     = QEMU_KEY_UP,
    [Q_KEY_CODE_DOWN]   = QEMU_KEY_DOWN,
    [Q_KEY_CODE_RIGHT]  = QEMU_KEY_RIGHT,
    [Q_KEY_CODE_LEFT]   = QEMU_KEY_LEFT,
    [Q_KEY_CODE_HOME]   = QEMU_KEY_HOME,
    [Q_KEY_CODE_END]    = QEMU_KEY_END,
    [Q_KEY_CODE_PGUP]   = QEMU_KEY_PAGEUP,
    [Q_KEY_CODE_PGDN]   = QEMU_KEY_PAGEDOWN,
    [Q_KEY_CODE_DELETE] = QEMU_KEY_DELETE,
    [Q_KEY_CODE_TAB]    = QEMU_KEY_TAB,
    [Q_KEY_CODE_BACKSPACE] = QEMU_KEY_BACKSPACE,
};

static const int ctrl_qcode_to_keysym[Q_KEY_CODE__MAX] = {
    [Q_KEY_CODE_UP]     = QEMU_KEY_CTRL_UP,
    [Q_KEY_CODE_DOWN]   = QEMU_KEY_CTRL_DOWN,
    [Q_KEY_CODE_RIGHT]  = QEMU_KEY_CTRL_RIGHT,
    [Q_KEY_CODE_LEFT]   = QEMU_KEY_CTRL_LEFT,
    [Q_KEY_CODE_HOME]   = QEMU_KEY_CTRL_HOME,
    [Q_KEY_CODE_END]    = QEMU_KEY_CTRL_END,
    [Q_KEY_CODE_PGUP]   = QEMU_KEY_CTRL_PAGEUP,
    [Q_KEY_CODE_PGDN]   = QEMU_KEY_CTRL_PAGEDOWN,
};

bool kbd_put_qcode_console(QemuTextConsole *s, int qcode, bool ctrl)
{
    int keysym;

    keysym = ctrl ? ctrl_qcode_to_keysym[qcode] : qcode_to_keysym[qcode];
    if (keysym == 0) {
        return false;
    }
    kbd_put_keysym_console(s, keysym);
    return true;
}

void kbd_put_string_console(QemuTextConsole *s, const char *str, int len)
{
    int i;

    for (i = 0; i < len && str[i]; i++) {
        kbd_put_keysym_console(s, str[i]);
    }
}

void kbd_put_keysym(int keysym)
{
    if (QEMU_IS_TEXT_CONSOLE(active_console)) {
        kbd_put_keysym_console(QEMU_TEXT_CONSOLE(active_console), keysym);
    }
}

static void text_console_invalidate(void *opaque)
{
    QemuTextConsole *s = QEMU_TEXT_CONSOLE(opaque);

    if (!QEMU_IS_FIXED_TEXT_CONSOLE(s)) {
        text_console_resize(QEMU_TEXT_CONSOLE(s));
    }
    console_refresh(s);
}

static void text_console_update(void *opaque, console_ch_t *chardata)
{
    QemuTextConsole *s = QEMU_TEXT_CONSOLE(opaque);
    int i, j, src;

    if (s->text_x[0] <= s->text_x[1]) {
        src = (s->y_base + s->text_y[0]) * s->width;
        chardata += s->text_y[0] * s->width;
        for (i = s->text_y[0]; i <= s->text_y[1]; i ++)
            for (j = 0; j < s->width; j++, src++) {
                console_write_ch(chardata ++,
                                 ATTR2CHTYPE(s->cells[src].ch,
                                             s->cells[src].t_attrib.fgcol,
                                             s->cells[src].t_attrib.bgcol,
                                             s->cells[src].t_attrib.bold));
            }
        dpy_text_update(QEMU_CONSOLE(s), s->text_x[0], s->text_y[0],
                        s->text_x[1] - s->text_x[0], i - s->text_y[0]);
        s->text_x[0] = s->width;
        s->text_y[0] = s->height;
        s->text_x[1] = 0;
        s->text_y[1] = 0;
    }
    if (s->cursor_invalidate) {
        dpy_text_cursor(QEMU_CONSOLE(s), s->x, s->y);
        s->cursor_invalidate = 0;
    }
}

static void
qemu_console_register(QemuConsole *c)
{
    int i;

    if (!active_console || (!QEMU_IS_GRAPHIC_CONSOLE(active_console) &&
                            QEMU_IS_GRAPHIC_CONSOLE(c))) {
        active_console = c;
    }

    if (QTAILQ_EMPTY(&consoles)) {
        c->index = 0;
        QTAILQ_INSERT_TAIL(&consoles, c, next);
    } else if (!QEMU_IS_GRAPHIC_CONSOLE(c) || phase_check(PHASE_MACHINE_READY)) {
        QemuConsole *last = QTAILQ_LAST(&consoles);
        c->index = last->index + 1;
        QTAILQ_INSERT_TAIL(&consoles, c, next);
    } else {
        /*
         * HACK: Put graphical consoles before text consoles.
         *
         * Only do that for coldplugged devices.  After initial device
         * initialization we will not renumber the consoles any more.
         */
        QemuConsole *it = QTAILQ_FIRST(&consoles);

        while (QTAILQ_NEXT(it, next) != NULL && QEMU_IS_GRAPHIC_CONSOLE(it)) {
            it = QTAILQ_NEXT(it, next);
        }
        if (QEMU_IS_GRAPHIC_CONSOLE(it)) {
            /* have no text consoles */
            c->index = it->index + 1;
            QTAILQ_INSERT_AFTER(&consoles, it, c, next);
        } else {
            c->index = it->index;
            QTAILQ_INSERT_BEFORE(it, c, next);
            /* renumber text consoles */
            for (i = c->index + 1; it != NULL; it = QTAILQ_NEXT(it, next), i++) {
                it->index = i;
            }
        }
    }
}

static void
qemu_console_finalize(Object *obj)
{
    QemuConsole *c = QEMU_CONSOLE(obj);

    /* TODO: check this code path, and unregister from consoles */
    g_clear_pointer(&c->surface, qemu_free_displaysurface);
    g_clear_pointer(&c->gl_unblock_timer, timer_free);
    g_clear_pointer(&c->ui_timer, timer_free);
}

static void
qemu_console_class_init(ObjectClass *oc, void *data)
{
}

static void
qemu_console_init(Object *obj)
{
    QemuConsole *c = QEMU_CONSOLE(obj);
    DisplayState *ds = get_alloc_displaystate();

    qemu_co_queue_init(&c->dump_queue);
    c->ds = ds;
    c->window_id = -1;
    c->ui_timer = timer_new_ms(QEMU_CLOCK_REALTIME,
                               dpy_set_ui_info_timer, c);
    qemu_console_register(c);
}

static void
qemu_graphic_console_finalize(Object *obj)
{
    QemuGraphicConsole *c = QEMU_GRAPHIC_CONSOLE(obj);

    g_clear_pointer(&c->device, object_unref);
}

static void
qemu_graphic_console_prop_get_head(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    QemuGraphicConsole *c = QEMU_GRAPHIC_CONSOLE(obj);

    visit_type_uint32(v, name, &c->head, errp);
}

static void
qemu_graphic_console_class_init(ObjectClass *oc, void *data)
{
    object_class_property_add_link(oc, "device", TYPE_DEVICE,
                                   offsetof(QemuGraphicConsole, device),
                                   object_property_allow_set_link,
                                   OBJ_PROP_LINK_STRONG);
    object_class_property_add(oc, "head", "uint32",
                              qemu_graphic_console_prop_get_head,
                              NULL, NULL, NULL);
}

static void
qemu_graphic_console_init(Object *obj)
{
}

static void
qemu_text_console_finalize(Object *obj)
{
}

static void
qemu_text_console_class_init(ObjectClass *oc, void *data)
{
    if (!cursor_timer) {
        cursor_timer = timer_new_ms(QEMU_CLOCK_REALTIME,
                                    text_console_update_cursor, NULL);
    }
}

static const GraphicHwOps text_console_ops = {
    .invalidate  = text_console_invalidate,
    .text_update = text_console_update,
};

static void
qemu_text_console_init(Object *obj)
{
    QemuTextConsole *c = QEMU_TEXT_CONSOLE(obj);

    fifo8_create(&c->out_fifo, 16);
    c->total_height = DEFAULT_BACKSCROLL;
    QEMU_CONSOLE(c)->hw_ops = &text_console_ops;
    QEMU_CONSOLE(c)->hw = c;
}

static void
qemu_fixed_text_console_finalize(Object *obj)
{
}

static void
qemu_fixed_text_console_class_init(ObjectClass *oc, void *data)
{
}

static void
qemu_fixed_text_console_init(Object *obj)
{
}

#ifdef WIN32
void qemu_displaysurface_win32_set_handle(DisplaySurface *surface,
                                          HANDLE h, uint32_t offset)
{
    assert(!surface->handle);

    surface->handle = h;
    surface->handle_offset = offset;
}

static void
win32_pixman_image_destroy(pixman_image_t *image, void *data)
{
    DisplaySurface *surface = data;

    if (!surface->handle) {
        return;
    }

    assert(surface->handle_offset == 0);

    qemu_win32_map_free(
        pixman_image_get_data(surface->image),
        surface->handle,
        &error_warn
    );
}
#endif

DisplaySurface *qemu_create_displaysurface(int width, int height)
{
    DisplaySurface *surface;
    void *bits = NULL;
#ifdef WIN32
    HANDLE handle = NULL;
#endif

    trace_displaysurface_create(width, height);

#ifdef WIN32
    bits = qemu_win32_map_alloc(width * height * 4, &handle, &error_abort);
#endif

    surface = qemu_create_displaysurface_from(
        width, height,
        PIXMAN_x8r8g8b8,
        width * 4, bits
    );
    surface->flags = QEMU_ALLOCATED_FLAG;

#ifdef WIN32
    qemu_displaysurface_win32_set_handle(surface, handle, 0);
#endif
    return surface;
}

DisplaySurface *qemu_create_displaysurface_from(int width, int height,
                                                pixman_format_code_t format,
                                                int linesize, uint8_t *data)
{
    DisplaySurface *surface = g_new0(DisplaySurface, 1);

    trace_displaysurface_create_from(surface, width, height, format);
    surface->format = format;
    surface->image = pixman_image_create_bits(surface->format,
                                              width, height,
                                              (void *)data, linesize);
    assert(surface->image != NULL);
#ifdef WIN32
    pixman_image_set_destroy_function(surface->image,
                                      win32_pixman_image_destroy, surface);
#endif

    return surface;
}

DisplaySurface *qemu_create_displaysurface_pixman(pixman_image_t *image)
{
    DisplaySurface *surface = g_new0(DisplaySurface, 1);

    trace_displaysurface_create_pixman(surface);
    surface->format = pixman_image_get_format(image);
    surface->image = pixman_image_ref(image);

    return surface;
}

DisplaySurface *qemu_create_placeholder_surface(int w, int h,
                                                const char *msg)
{
    DisplaySurface *surface = qemu_create_displaysurface(w, h);
    pixman_color_t bg = QEMU_PIXMAN_COLOR_BLACK;
    pixman_color_t fg = QEMU_PIXMAN_COLOR_GRAY;
    pixman_image_t *glyph;
    int len, x, y, i;

    len = strlen(msg);
    x = (w / FONT_WIDTH  - len) / 2;
    y = (h / FONT_HEIGHT - 1)   / 2;
    for (i = 0; i < len; i++) {
        glyph = qemu_pixman_glyph_from_vgafont(FONT_HEIGHT, vgafont16, msg[i]);
        qemu_pixman_glyph_render(glyph, surface->image, &fg, &bg,
                                 x+i, y, FONT_WIDTH, FONT_HEIGHT);
        qemu_pixman_image_unref(glyph);
    }
    surface->flags |= QEMU_PLACEHOLDER_FLAG;
    return surface;
}

void qemu_free_displaysurface(DisplaySurface *surface)
{
    if (surface == NULL) {
        return;
    }
    trace_displaysurface_free(surface);
    qemu_pixman_image_unref(surface->image);
    g_free(surface);
}

bool console_has_gl(QemuConsole *con)
{
    return con->gl != NULL;
}

static bool displaychangelistener_has_dmabuf(DisplayChangeListener *dcl)
{
    if (dcl->ops->dpy_has_dmabuf) {
        return dcl->ops->dpy_has_dmabuf(dcl);
    }

    if (dcl->ops->dpy_gl_scanout_dmabuf) {
        return true;
    }

    return false;
}

static bool console_compatible_with(QemuConsole *con,
                                    DisplayChangeListener *dcl, Error **errp)
{
    int flags;

    flags = con->hw_ops->get_flags ? con->hw_ops->get_flags(con->hw) : 0;

    if (console_has_gl(con) &&
        !con->gl->ops->dpy_gl_ctx_is_compatible_dcl(con->gl, dcl)) {
        error_setg(errp, "Display %s is incompatible with the GL context",
                   dcl->ops->dpy_name);
        return false;
    }

    if (flags & GRAPHIC_FLAGS_GL &&
        !console_has_gl(con)) {
        error_setg(errp, "The console requires a GL context.");
        return false;

    }

    if (flags & GRAPHIC_FLAGS_DMABUF &&
        !displaychangelistener_has_dmabuf(dcl)) {
        error_setg(errp, "The console requires display DMABUF support.");
        return false;
    }

    return true;
}

void console_handle_touch_event(QemuConsole *con,
                                struct touch_slot touch_slots[INPUT_EVENT_SLOTS_MAX],
                                uint64_t num_slot,
                                int width, int height,
                                double x, double y,
                                InputMultiTouchType type,
                                Error **errp)
{
    struct touch_slot *slot;
    bool needs_sync = false;
    int update;
    int i;

    if (num_slot >= INPUT_EVENT_SLOTS_MAX) {
        error_setg(errp,
                   "Unexpected touch slot number: % " PRId64" >= %d",
                   num_slot, INPUT_EVENT_SLOTS_MAX);
        return;
    }

    slot = &touch_slots[num_slot];
    slot->x = x;
    slot->y = y;

    if (type == INPUT_MULTI_TOUCH_TYPE_BEGIN) {
        slot->tracking_id = num_slot;
    }

    for (i = 0; i < INPUT_EVENT_SLOTS_MAX; ++i) {
        if (i == num_slot) {
            update = type;
        } else {
            update = INPUT_MULTI_TOUCH_TYPE_UPDATE;
        }

        slot = &touch_slots[i];

        if (slot->tracking_id == -1) {
            continue;
        }

        if (update == INPUT_MULTI_TOUCH_TYPE_END) {
            slot->tracking_id = -1;
            qemu_input_queue_mtt(con, update, i, slot->tracking_id);
            needs_sync = true;
        } else {
            qemu_input_queue_mtt(con, update, i, slot->tracking_id);
            qemu_input_queue_btn(con, INPUT_BUTTON_TOUCH, true);
            qemu_input_queue_mtt_abs(con,
                                    INPUT_AXIS_X, (int) slot->x,
                                    0, width,
                                    i, slot->tracking_id);
            qemu_input_queue_mtt_abs(con,
                                    INPUT_AXIS_Y, (int) slot->y,
                                    0, height,
                                    i, slot->tracking_id);
            needs_sync = true;
        }
    }

    if (needs_sync) {
        qemu_input_event_sync();
    }
}

void qemu_console_set_display_gl_ctx(QemuConsole *con, DisplayGLCtx *gl)
{
    /* display has opengl support */
    assert(con);
    if (con->gl) {
        error_report("The console already has an OpenGL context.");
        exit(1);
    }
    con->gl = gl;
}

static void
dcl_set_graphic_cursor(DisplayChangeListener *dcl, QemuGraphicConsole *con)
{
    if (con && con->cursor && dcl->ops->dpy_cursor_define) {
        dcl->ops->dpy_cursor_define(dcl, con->cursor);
    }
    if (con && dcl->ops->dpy_mouse_set) {
        dcl->ops->dpy_mouse_set(dcl, con->cursor_x, con->cursor_y, con->cursor_on);
    }
}
void register_displaychangelistener(DisplayChangeListener *dcl)
{
    QemuConsole *con;

    assert(!dcl->ds);

    trace_displaychangelistener_register(dcl, dcl->ops->dpy_name);
    dcl->ds = get_alloc_displaystate();
    QLIST_INSERT_HEAD(&dcl->ds->listeners, dcl, next);
    gui_setup_refresh(dcl->ds);
    if (dcl->con) {
        dcl->con->dcls++;
        con = dcl->con;
    } else {
        con = active_console;
    }
    displaychangelistener_display_console(dcl, con, dcl->con ? &error_fatal : NULL);
    if (QEMU_IS_GRAPHIC_CONSOLE(con)) {
        dcl_set_graphic_cursor(dcl, QEMU_GRAPHIC_CONSOLE(con));
    }
    text_console_update_cursor(NULL);
}

void update_displaychangelistener(DisplayChangeListener *dcl,
                                  uint64_t interval)
{
    DisplayState *ds = dcl->ds;

    dcl->update_interval = interval;
    if (!ds->refreshing && ds->update_interval > interval) {
        timer_mod(ds->gui_timer, ds->last_update + interval);
    }
}

void unregister_displaychangelistener(DisplayChangeListener *dcl)
{
    DisplayState *ds = dcl->ds;
    trace_displaychangelistener_unregister(dcl, dcl->ops->dpy_name);
    if (dcl->con) {
        dcl->con->dcls--;
    }
    QLIST_REMOVE(dcl, next);
    dcl->ds = NULL;
    gui_setup_refresh(ds);
}

static void dpy_set_ui_info_timer(void *opaque)
{
    QemuConsole *con = opaque;
    uint32_t head = qemu_console_get_head(con);

    con->hw_ops->ui_info(con->hw, head, &con->ui_info);
}

bool dpy_ui_info_supported(QemuConsole *con)
{
    if (con == NULL) {
        con = active_console;
    }

    return con->hw_ops->ui_info != NULL;
}

const QemuUIInfo *dpy_get_ui_info(const QemuConsole *con)
{
    if (con == NULL) {
        con = active_console;
    }

    return &con->ui_info;
}

int dpy_set_ui_info(QemuConsole *con, QemuUIInfo *info, bool delay)
{
    if (con == NULL) {
        con = active_console;
    }

    if (!dpy_ui_info_supported(con)) {
        return -1;
    }
    if (memcmp(&con->ui_info, info, sizeof(con->ui_info)) == 0) {
        /* nothing changed -- ignore */
        return 0;
    }

    /*
     * Typically we get a flood of these as the user resizes the window.
     * Wait until the dust has settled (one second without updates), then
     * go notify the guest.
     */
    con->ui_info = *info;
    timer_mod(con->ui_timer,
              qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + (delay ? 1000 : 0));
    return 0;
}

void dpy_gfx_update(QemuConsole *con, int x, int y, int w, int h)
{
    DisplayState *s = con->ds;
    DisplayChangeListener *dcl;
    int width = qemu_console_get_width(con, x + w);
    int height = qemu_console_get_height(con, y + h);

    x = MAX(x, 0);
    y = MAX(y, 0);
    x = MIN(x, width);
    y = MIN(y, height);
    w = MIN(w, width - x);
    h = MIN(h, height - y);

    if (!qemu_console_is_visible(con)) {
        return;
    }
    dpy_gfx_update_texture(con, con->surface, x, y, w, h);
    QLIST_FOREACH(dcl, &s->listeners, next) {
        if (con != (dcl->con ? dcl->con : active_console)) {
            continue;
        }
        if (dcl->ops->dpy_gfx_update) {
            dcl->ops->dpy_gfx_update(dcl, x, y, w, h);
        }
    }
}

void dpy_gfx_update_full(QemuConsole *con)
{
    int w = qemu_console_get_width(con, 0);
    int h = qemu_console_get_height(con, 0);

    dpy_gfx_update(con, 0, 0, w, h);
}

void dpy_gfx_replace_surface(QemuConsole *con,
                             DisplaySurface *surface)
{
    static const char placeholder_msg[] = "Display output is not active.";
    DisplayState *s = con->ds;
    DisplaySurface *old_surface = con->surface;
    DisplaySurface *new_surface = surface;
    DisplayChangeListener *dcl;
    int width;
    int height;

    if (!surface) {
        if (old_surface) {
            width = surface_width(old_surface);
            height = surface_height(old_surface);
        } else {
            width = 640;
            height = 480;
        }

        new_surface = qemu_create_placeholder_surface(width, height, placeholder_msg);
    }

    assert(old_surface != new_surface);

    con->scanout.kind = SCANOUT_SURFACE;
    con->surface = new_surface;
    dpy_gfx_create_texture(con, new_surface);
    QLIST_FOREACH(dcl, &s->listeners, next) {
        if (con != (dcl->con ? dcl->con : active_console)) {
            continue;
        }
        displaychangelistener_gfx_switch(dcl, new_surface, surface ? FALSE : TRUE);
    }
    dpy_gfx_destroy_texture(con, old_surface);
    qemu_free_displaysurface(old_surface);
}

bool dpy_gfx_check_format(QemuConsole *con,
                          pixman_format_code_t format)
{
    DisplayChangeListener *dcl;
    DisplayState *s = con->ds;

    QLIST_FOREACH(dcl, &s->listeners, next) {
        if (dcl->con && dcl->con != con) {
            /* dcl bound to another console -> skip */
            continue;
        }
        if (dcl->ops->dpy_gfx_check_format) {
            if (!dcl->ops->dpy_gfx_check_format(dcl, format)) {
                return false;
            }
        } else {
            /* default is to allow native 32 bpp only */
            if (format != qemu_default_pixman_format(32, true)) {
                return false;
            }
        }
    }
    return true;
}

static void dpy_refresh(DisplayState *s)
{
    DisplayChangeListener *dcl;

    QLIST_FOREACH(dcl, &s->listeners, next) {
        if (dcl->ops->dpy_refresh) {
            dcl->ops->dpy_refresh(dcl);
        }
    }
}

void dpy_text_cursor(QemuConsole *con, int x, int y)
{
    DisplayState *s = con->ds;
    DisplayChangeListener *dcl;

    if (!qemu_console_is_visible(con)) {
        return;
    }
    QLIST_FOREACH(dcl, &s->listeners, next) {
        if (con != (dcl->con ? dcl->con : active_console)) {
            continue;
        }
        if (dcl->ops->dpy_text_cursor) {
            dcl->ops->dpy_text_cursor(dcl, x, y);
        }
    }
}

void dpy_text_update(QemuConsole *con, int x, int y, int w, int h)
{
    DisplayState *s = con->ds;
    DisplayChangeListener *dcl;

    if (!qemu_console_is_visible(con)) {
        return;
    }
    QLIST_FOREACH(dcl, &s->listeners, next) {
        if (con != (dcl->con ? dcl->con : active_console)) {
            continue;
        }
        if (dcl->ops->dpy_text_update) {
            dcl->ops->dpy_text_update(dcl, x, y, w, h);
        }
    }
}

void dpy_text_resize(QemuConsole *con, int w, int h)
{
    DisplayState *s = con->ds;
    DisplayChangeListener *dcl;

    if (!qemu_console_is_visible(con)) {
        return;
    }
    QLIST_FOREACH(dcl, &s->listeners, next) {
        if (con != (dcl->con ? dcl->con : active_console)) {
            continue;
        }
        if (dcl->ops->dpy_text_resize) {
            dcl->ops->dpy_text_resize(dcl, w, h);
        }
    }
}

void dpy_mouse_set(QemuConsole *c, int x, int y, int on)
{
    QemuGraphicConsole *con = QEMU_GRAPHIC_CONSOLE(c);
    DisplayState *s = c->ds;
    DisplayChangeListener *dcl;

    con->cursor_x = x;
    con->cursor_y = y;
    con->cursor_on = on;
    if (!qemu_console_is_visible(c)) {
        return;
    }
    QLIST_FOREACH(dcl, &s->listeners, next) {
        if (c != (dcl->con ? dcl->con : active_console)) {
            continue;
        }
        if (dcl->ops->dpy_mouse_set) {
            dcl->ops->dpy_mouse_set(dcl, x, y, on);
        }
    }
}

void dpy_cursor_define(QemuConsole *c, QEMUCursor *cursor)
{
    QemuGraphicConsole *con = QEMU_GRAPHIC_CONSOLE(c);
    DisplayState *s = c->ds;
    DisplayChangeListener *dcl;

    cursor_unref(con->cursor);
    con->cursor = cursor_ref(cursor);
    if (!qemu_console_is_visible(c)) {
        return;
    }
    QLIST_FOREACH(dcl, &s->listeners, next) {
        if (c != (dcl->con ? dcl->con : active_console)) {
            continue;
        }
        if (dcl->ops->dpy_cursor_define) {
            dcl->ops->dpy_cursor_define(dcl, cursor);
        }
    }
}

bool dpy_cursor_define_supported(QemuConsole *con)
{
    DisplayState *s = con->ds;
    DisplayChangeListener *dcl;

    QLIST_FOREACH(dcl, &s->listeners, next) {
        if (dcl->ops->dpy_cursor_define) {
            return true;
        }
    }
    return false;
}

QEMUGLContext dpy_gl_ctx_create(QemuConsole *con,
                                struct QEMUGLParams *qparams)
{
    assert(con->gl);
    return con->gl->ops->dpy_gl_ctx_create(con->gl, qparams);
}

void dpy_gl_ctx_destroy(QemuConsole *con, QEMUGLContext ctx)
{
    assert(con->gl);
    con->gl->ops->dpy_gl_ctx_destroy(con->gl, ctx);
}

int dpy_gl_ctx_make_current(QemuConsole *con, QEMUGLContext ctx)
{
    assert(con->gl);
    return con->gl->ops->dpy_gl_ctx_make_current(con->gl, ctx);
}

void dpy_gl_scanout_disable(QemuConsole *con)
{
    DisplayState *s = con->ds;
    DisplayChangeListener *dcl;

    if (con->scanout.kind != SCANOUT_SURFACE) {
        con->scanout.kind = SCANOUT_NONE;
    }
    QLIST_FOREACH(dcl, &s->listeners, next) {
        if (con != (dcl->con ? dcl->con : active_console)) {
            continue;
        }
        if (dcl->ops->dpy_gl_scanout_disable) {
            dcl->ops->dpy_gl_scanout_disable(dcl);
        }
    }
}

void dpy_gl_scanout_texture(QemuConsole *con,
                            uint32_t backing_id,
                            bool backing_y_0_top,
                            uint32_t backing_width,
                            uint32_t backing_height,
                            uint32_t x, uint32_t y,
                            uint32_t width, uint32_t height,
                            void *d3d_tex2d)
{
    DisplayState *s = con->ds;
    DisplayChangeListener *dcl;

    con->scanout.kind = SCANOUT_TEXTURE;
    con->scanout.texture = (ScanoutTexture) {
        backing_id, backing_y_0_top, backing_width, backing_height,
        x, y, width, height, d3d_tex2d,
    };
    QLIST_FOREACH(dcl, &s->listeners, next) {
        if (con != (dcl->con ? dcl->con : active_console)) {
            continue;
        }
        if (dcl->ops->dpy_gl_scanout_texture) {
            dcl->ops->dpy_gl_scanout_texture(dcl, backing_id,
                                             backing_y_0_top,
                                             backing_width, backing_height,
                                             x, y, width, height,
                                             d3d_tex2d);
        }
    }
}

void dpy_gl_scanout_dmabuf(QemuConsole *con,
                           QemuDmaBuf *dmabuf)
{
    DisplayState *s = con->ds;
    DisplayChangeListener *dcl;

    con->scanout.kind = SCANOUT_DMABUF;
    con->scanout.dmabuf = dmabuf;
    QLIST_FOREACH(dcl, &s->listeners, next) {
        if (con != (dcl->con ? dcl->con : active_console)) {
            continue;
        }
        if (dcl->ops->dpy_gl_scanout_dmabuf) {
            dcl->ops->dpy_gl_scanout_dmabuf(dcl, dmabuf);
        }
    }
}

void dpy_gl_cursor_dmabuf(QemuConsole *con, QemuDmaBuf *dmabuf,
                          bool have_hot, uint32_t hot_x, uint32_t hot_y)
{
    DisplayState *s = con->ds;
    DisplayChangeListener *dcl;

    QLIST_FOREACH(dcl, &s->listeners, next) {
        if (con != (dcl->con ? dcl->con : active_console)) {
            continue;
        }
        if (dcl->ops->dpy_gl_cursor_dmabuf) {
            dcl->ops->dpy_gl_cursor_dmabuf(dcl, dmabuf,
                                           have_hot, hot_x, hot_y);
        }
    }
}

void dpy_gl_cursor_position(QemuConsole *con,
                            uint32_t pos_x, uint32_t pos_y)
{
    DisplayState *s = con->ds;
    DisplayChangeListener *dcl;

    QLIST_FOREACH(dcl, &s->listeners, next) {
        if (con != (dcl->con ? dcl->con : active_console)) {
            continue;
        }
        if (dcl->ops->dpy_gl_cursor_position) {
            dcl->ops->dpy_gl_cursor_position(dcl, pos_x, pos_y);
        }
    }
}

void dpy_gl_release_dmabuf(QemuConsole *con,
                          QemuDmaBuf *dmabuf)
{
    DisplayState *s = con->ds;
    DisplayChangeListener *dcl;

    QLIST_FOREACH(dcl, &s->listeners, next) {
        if (con != (dcl->con ? dcl->con : active_console)) {
            continue;
        }
        if (dcl->ops->dpy_gl_release_dmabuf) {
            dcl->ops->dpy_gl_release_dmabuf(dcl, dmabuf);
        }
    }
}

void dpy_gl_update(QemuConsole *con,
                   uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    DisplayState *s = con->ds;
    DisplayChangeListener *dcl;

    assert(con->gl);

    graphic_hw_gl_block(con, true);
    QLIST_FOREACH(dcl, &s->listeners, next) {
        if (con != (dcl->con ? dcl->con : active_console)) {
            continue;
        }
        if (dcl->ops->dpy_gl_update) {
            dcl->ops->dpy_gl_update(dcl, x, y, w, h);
        }
    }
    graphic_hw_gl_block(con, false);
}

/***********************************************************/
/* register display */

/* console.c internal use only */
static DisplayState *get_alloc_displaystate(void)
{
    if (!display_state) {
        display_state = g_new0(DisplayState, 1);
    }
    return display_state;
}

/*
 * Called by main(), after creating QemuConsoles
 * and before initializing ui (sdl/vnc/...).
 */
DisplayState *init_displaystate(void)
{
    gchar *name;
    QemuConsole *con;

    QTAILQ_FOREACH(con, &consoles, next) {
        /* Hook up into the qom tree here (not in object_new()), once
         * all QemuConsoles are created and the order / numbering
         * doesn't change any more */
        name = g_strdup_printf("console[%d]", con->index);
        object_property_add_child(container_get(object_get_root(), "/backend"),
                                  name, OBJECT(con));
        g_free(name);
    }

    return display_state;
}

void graphic_console_set_hwops(QemuConsole *con,
                               const GraphicHwOps *hw_ops,
                               void *opaque)
{
    con->hw_ops = hw_ops;
    con->hw = opaque;
}

QemuConsole *graphic_console_init(DeviceState *dev, uint32_t head,
                                  const GraphicHwOps *hw_ops,
                                  void *opaque)
{
    static const char noinit[] =
        "Guest has not initialized the display (yet).";
    int width = 640;
    int height = 480;
    QemuConsole *s;
    DisplaySurface *surface;

    s = qemu_graphic_console_lookup_unused();
    if (s) {
        trace_console_gfx_reuse(s->index);
        width = qemu_console_get_width(s, 0);
        height = qemu_console_get_height(s, 0);
    } else {
        trace_console_gfx_new();
        s = (QemuConsole *)object_new(TYPE_QEMU_GRAPHIC_CONSOLE);
    }
    QEMU_GRAPHIC_CONSOLE(s)->head = head;
    graphic_console_set_hwops(s, hw_ops, opaque);
    if (dev) {
        object_property_set_link(OBJECT(s), "device", OBJECT(dev),
                                 &error_abort);
    }

    surface = qemu_create_placeholder_surface(width, height, noinit);
    dpy_gfx_replace_surface(s, surface);
    s->gl_unblock_timer = timer_new_ms(QEMU_CLOCK_REALTIME,
                                       graphic_hw_gl_unblock_timer, s);
    return s;
}

static const GraphicHwOps unused_ops = {
    /* no callbacks */
};

void graphic_console_close(QemuConsole *con)
{
    static const char unplugged[] =
        "Guest display has been unplugged";
    DisplaySurface *surface;
    int width = qemu_console_get_width(con, 640);
    int height = qemu_console_get_height(con, 480);

    trace_console_gfx_close(con->index);
    object_property_set_link(OBJECT(con), "device", NULL, &error_abort);
    graphic_console_set_hwops(con, &unused_ops, NULL);

    if (con->gl) {
        dpy_gl_scanout_disable(con);
    }
    surface = qemu_create_placeholder_surface(width, height, unplugged);
    dpy_gfx_replace_surface(con, surface);
}

QemuConsole *qemu_console_lookup_by_index(unsigned int index)
{
    QemuConsole *con;

    QTAILQ_FOREACH(con, &consoles, next) {
        if (con->index == index) {
            return con;
        }
    }
    return NULL;
}

QemuConsole *qemu_console_lookup_by_device(DeviceState *dev, uint32_t head)
{
    QemuConsole *con;
    Object *obj;
    uint32_t h;

    QTAILQ_FOREACH(con, &consoles, next) {
        obj = object_property_get_link(OBJECT(con),
                                       "device", &error_abort);
        if (DEVICE(obj) != dev) {
            continue;
        }
        h = object_property_get_uint(OBJECT(con),
                                     "head", &error_abort);
        if (h != head) {
            continue;
        }
        return con;
    }
    return NULL;
}

QemuConsole *qemu_console_lookup_by_device_name(const char *device_id,
                                                uint32_t head, Error **errp)
{
    DeviceState *dev;
    QemuConsole *con;

    dev = qdev_find_recursive(sysbus_get_default(), device_id);
    if (dev == NULL) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Device '%s' not found", device_id);
        return NULL;
    }

    con = qemu_console_lookup_by_device(dev, head);
    if (con == NULL) {
        error_setg(errp, "Device %s (head %d) is not bound to a QemuConsole",
                   device_id, head);
        return NULL;
    }

    return con;
}

static QemuConsole *qemu_graphic_console_lookup_unused(void)
{
    QemuConsole *con;
    Object *obj;

    QTAILQ_FOREACH(con, &consoles, next) {
        if (!QEMU_IS_GRAPHIC_CONSOLE(con) || con->hw_ops != &unused_ops) {
            continue;
        }
        obj = object_property_get_link(OBJECT(con),
                                       "device", &error_abort);
        if (obj != NULL) {
            continue;
        }
        return con;
    }
    return NULL;
}

QEMUCursor *qemu_console_get_cursor(QemuConsole *con)
{
    if (con == NULL) {
        con = active_console;
    }
    return QEMU_IS_GRAPHIC_CONSOLE(con) ? QEMU_GRAPHIC_CONSOLE(con)->cursor : NULL;
}

bool qemu_console_is_visible(QemuConsole *con)
{
    return (con == active_console) || (con->dcls > 0);
}

bool qemu_console_is_graphic(QemuConsole *con)
{
    if (con == NULL) {
        con = active_console;
    }
    return con && QEMU_IS_GRAPHIC_CONSOLE(con);
}

bool qemu_console_is_fixedsize(QemuConsole *con)
{
    if (con == NULL) {
        con = active_console;
    }
    return con && (QEMU_IS_GRAPHIC_CONSOLE(con) || QEMU_IS_FIXED_TEXT_CONSOLE(con));
}

bool qemu_console_is_gl_blocked(QemuConsole *con)
{
    assert(con != NULL);
    return con->gl_block;
}

bool qemu_console_is_multihead(DeviceState *dev)
{
    QemuConsole *con;
    Object *obj;
    uint32_t f = 0xffffffff;
    uint32_t h;

    QTAILQ_FOREACH(con, &consoles, next) {
        obj = object_property_get_link(OBJECT(con),
                                       "device", &error_abort);
        if (DEVICE(obj) != dev) {
            continue;
        }

        h = object_property_get_uint(OBJECT(con),
                                     "head", &error_abort);
        if (f == 0xffffffff) {
            f = h;
        } else if (h != f) {
            return true;
        }
    }
    return false;
}

char *qemu_console_get_label(QemuConsole *con)
{
    if (QEMU_IS_GRAPHIC_CONSOLE(con)) {
        QemuGraphicConsole *c = QEMU_GRAPHIC_CONSOLE(con);
        if (c->device) {
            DeviceState *dev;
            bool multihead;

            dev = DEVICE(c->device);
            multihead = qemu_console_is_multihead(dev);
            if (multihead) {
                return g_strdup_printf("%s.%d", dev->id ?
                                       dev->id :
                                       object_get_typename(c->device),
                                       c->head);
            } else {
                return g_strdup_printf("%s", dev->id ?
                                       dev->id :
                                       object_get_typename(c->device));
            }
        }
        return g_strdup("VGA");
    } else if (QEMU_IS_TEXT_CONSOLE(con)) {
        QemuTextConsole *c = QEMU_TEXT_CONSOLE(con);
        if (c->chr && c->chr->label) {
            return g_strdup(c->chr->label);
        }
    }

    return g_strdup_printf("vc%d", con->index);
}

int qemu_console_get_index(QemuConsole *con)
{
    if (con == NULL) {
        con = active_console;
    }
    return con ? con->index : -1;
}

uint32_t qemu_console_get_head(QemuConsole *con)
{
    if (con == NULL) {
        con = active_console;
    }
    if (con == NULL) {
        return -1;
    }
    if (QEMU_IS_GRAPHIC_CONSOLE(con)) {
        return QEMU_GRAPHIC_CONSOLE(con)->head;
    }
    return 0;
}

int qemu_console_get_width(QemuConsole *con, int fallback)
{
    if (con == NULL) {
        con = active_console;
    }
    if (con == NULL) {
        return fallback;
    }
    switch (con->scanout.kind) {
    case SCANOUT_DMABUF:
        return con->scanout.dmabuf->width;
    case SCANOUT_TEXTURE:
        return con->scanout.texture.width;
    case SCANOUT_SURFACE:
        return surface_width(con->surface);
    default:
        return fallback;
    }
}

int qemu_console_get_height(QemuConsole *con, int fallback)
{
    if (con == NULL) {
        con = active_console;
    }
    if (con == NULL) {
        return fallback;
    }
    switch (con->scanout.kind) {
    case SCANOUT_DMABUF:
        return con->scanout.dmabuf->height;
    case SCANOUT_TEXTURE:
        return con->scanout.texture.height;
    case SCANOUT_SURFACE:
        return surface_height(con->surface);
    default:
        return fallback;
    }
}

static void vc_chr_accept_input(Chardev *chr)
{
    VCChardev *drv = VC_CHARDEV(chr);

    kbd_send_chars(drv->console);
}

static void vc_chr_set_echo(Chardev *chr, bool echo)
{
    VCChardev *drv = VC_CHARDEV(chr);

    drv->console->echo = echo;
}

int qemu_invalidate_text_consoles(void)
{
    QemuConsole *s;
    int count = 0;

    QTAILQ_FOREACH(s, &consoles, next) {
        if (qemu_console_is_graphic(s) ||
            !qemu_console_is_visible(s)) {
            continue;
        }
        count++;
        graphic_hw_invalidate(s);
    }

    return count;
}

static void text_console_update_cursor(void *opaque)
{
    cursor_visible_phase = !cursor_visible_phase;

    if (qemu_invalidate_text_consoles()) {
        timer_mod(cursor_timer,
                  qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + CONSOLE_CURSOR_PERIOD / 2);
    }
}

static void vc_chr_open(Chardev *chr,
                        ChardevBackend *backend,
                        bool *be_opened,
                        Error **errp)
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
        width = qemu_console_get_width(NULL, 80 * FONT_WIDTH);
        height = qemu_console_get_height(NULL, 24 * FONT_HEIGHT);
    } else {
        s = QEMU_TEXT_CONSOLE(object_new(TYPE_QEMU_FIXED_TEXT_CONSOLE));
    }

    dpy_gfx_replace_surface(QEMU_CONSOLE(s), qemu_create_displaysurface(width, height));

    s->chr = chr;
    drv->console = s;

    /* set current text attributes to default */
    drv->t_attrib = TEXT_ATTRIBUTES_DEFAULT;
    text_console_resize(s);

    if (chr->label) {
        char *msg;

        drv->t_attrib.bgcol = QEMU_COLOR_BLUE;
        msg = g_strdup_printf("%s console\r\n", chr->label);
        qemu_chr_write(chr, (uint8_t *)msg, strlen(msg), true);
        g_free(msg);
        drv->t_attrib = TEXT_ATTRIBUTES_DEFAULT;
    }

    *be_opened = true;
}

void qemu_console_resize(QemuConsole *s, int width, int height)
{
    DisplaySurface *surface = qemu_console_surface(s);

    assert(QEMU_IS_GRAPHIC_CONSOLE(s));

    if ((s->scanout.kind != SCANOUT_SURFACE ||
         (surface && surface->flags & QEMU_ALLOCATED_FLAG)) &&
        qemu_console_get_width(s, -1) == width &&
        qemu_console_get_height(s, -1) == height) {
        return;
    }

    surface = qemu_create_displaysurface(width, height);
    dpy_gfx_replace_surface(s, surface);
}

DisplaySurface *qemu_console_surface(QemuConsole *console)
{
    switch (console->scanout.kind) {
    case SCANOUT_SURFACE:
        return console->surface;
    default:
        return NULL;
    }
}

PixelFormat qemu_default_pixelformat(int bpp)
{
    pixman_format_code_t fmt = qemu_default_pixman_format(bpp, true);
    PixelFormat pf = qemu_pixelformat_from_pixman(fmt);
    return pf;
}

static QemuDisplay *dpys[DISPLAY_TYPE__MAX];

void qemu_display_register(QemuDisplay *ui)
{
    assert(ui->type < DISPLAY_TYPE__MAX);
    dpys[ui->type] = ui;
}

bool qemu_display_find_default(DisplayOptions *opts)
{
    static DisplayType prio[] = {
#if defined(CONFIG_GTK)
        DISPLAY_TYPE_GTK,
#endif
#if defined(CONFIG_SDL)
        DISPLAY_TYPE_SDL,
#endif
#if defined(CONFIG_COCOA)
        DISPLAY_TYPE_COCOA
#endif
    };
    int i;

    for (i = 0; i < (int)ARRAY_SIZE(prio); i++) {
        if (dpys[prio[i]] == NULL) {
            Error *local_err = NULL;
            int rv = ui_module_load(DisplayType_str(prio[i]), &local_err);
            if (rv < 0) {
                error_report_err(local_err);
            }
        }
        if (dpys[prio[i]] == NULL) {
            continue;
        }
        opts->type = prio[i];
        return true;
    }
    return false;
}

void qemu_display_early_init(DisplayOptions *opts)
{
    assert(opts->type < DISPLAY_TYPE__MAX);
    if (opts->type == DISPLAY_TYPE_NONE) {
        return;
    }
    if (dpys[opts->type] == NULL) {
        Error *local_err = NULL;
        int rv = ui_module_load(DisplayType_str(opts->type), &local_err);
        if (rv < 0) {
            error_report_err(local_err);
        }
    }
    if (dpys[opts->type] == NULL) {
        error_report("Display '%s' is not available.",
                     DisplayType_str(opts->type));
        exit(1);
    }
    if (dpys[opts->type]->early_init) {
        dpys[opts->type]->early_init(opts);
    }
}

void qemu_display_init(DisplayState *ds, DisplayOptions *opts)
{
    assert(opts->type < DISPLAY_TYPE__MAX);
    if (opts->type == DISPLAY_TYPE_NONE) {
        return;
    }
    assert(dpys[opts->type] != NULL);
    dpys[opts->type]->init(ds, opts);
}

void qemu_display_help(void)
{
    int idx;

    printf("Available display backend types:\n");
    printf("none\n");
    for (idx = DISPLAY_TYPE_NONE; idx < DISPLAY_TYPE__MAX; idx++) {
        if (!dpys[idx]) {
            Error *local_err = NULL;
            int rv = ui_module_load(DisplayType_str(idx), &local_err);
            if (rv < 0) {
                error_report_err(local_err);
            }
        }
        if (dpys[idx]) {
            printf("%s\n",  DisplayType_str(dpys[idx]->type));
        }
    }
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

static void char_vc_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->parse = vc_chr_parse;
    cc->open = vc_chr_open;
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
        type_register(&char_vc_type_info);
    }
}
