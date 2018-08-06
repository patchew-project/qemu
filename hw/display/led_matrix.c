/*
 * LED Matrix Demultiplexer
 *
 * Copyright 2018 Steffen Görtz <contrib@steffen-goertz.de>
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "ui/console.h"
#include "hw/display/led_matrix.h"
#include "ui/pixel_ops.h"

static bool led_was_on(LEDMatrixState *s, size_t x, size_t y)
{
    /* TODO implying Direction is ROW |-> COL */
    /* TODO add direction flag and generalize */
    bool row_level = extract64(s->row, x, 1);
    bool col_level = extract64(s->col, y, 1);

    return row_level && !col_level;
}

static void update_on_times(LEDMatrixState *s)
{
    size_t x;
    int64_t now = qemu_clock_get_us(QEMU_CLOCK_VIRTUAL);
    int64_t diff = now - s->timestamp;
    s->timestamp = now;

    for (x = 0; x < s->nrows ; x++) {
        for (size_t y = 0; y < s->ncols; y++) {
            if (led_was_on(s, x, y)) {
                s->led_working_dc[x * s->ncols + y] += diff;
            }
        }
    }
}

static void led_timer_expire(void *opaque)
{
    LEDMatrixState *s = LED_MATRIX(opaque);
/*
    uint8_t divider = s->strobe_row ? s->nrows : s->ncols;
    int64_t max_on = (s->refresh_period * 1000) / divider;
*/

    update_on_times(s);

    memcpy(s->led_frame_dc, s->led_working_dc,
           sizeof(int64_t) * s->nrows * s->ncols);
    memset(s->led_working_dc, 0x00, sizeof(int64_t) * s->nrows * s->ncols);

    timer_mod(&s->timer,
            qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + s->refresh_period);
    s->redraw = true;
}

static void set_row(void *opaque, int line, int value)
{
    LEDMatrixState *s = LED_MATRIX(opaque);

    update_on_times(s);

    s->row = deposit32(s->row, line, 1, value > 0);
}

static void set_column(void *opaque, int line, int value)
{
    LEDMatrixState *s = LED_MATRIX(opaque);

    update_on_times(s);

    s->col = deposit32(s->col, line, 1, value > 0);
}

static void draw_pixel(DisplaySurface *ds, int x, int y, uint32_t color)
{
    int bpp;
    uint8_t *d;
    bpp = (surface_bits_per_pixel(ds) + 7) >> 3;
    d = surface_data(ds) + surface_stride(ds) * y + bpp * x;
    switch (bpp) {
    case 1:
        *((uint8_t *) d) = color;
        d++;
        break;
    case 2:
        *((uint16_t *) d) = color;
        d += 2;
        break;
    case 4:
        *((uint32_t *) d) = color;
        d += 4;
        break;
    }
}

static void draw_box(DisplaySurface *ds,
                     int x0, int y0, int w, int h, uint32_t color)
{
    int x, y;
    for (x = 0; x < w; x++) {
        for (y = 0; y < h; y++) {
            draw_pixel(ds, x0 + x, y0 + y, color);
        }
    }
}

typedef unsigned int (*color_func)(unsigned int, unsigned int, unsigned int);

static void led_invalidate_display(void *opaque)
{
    LEDMatrixState *s = LED_MATRIX(opaque);
    s->redraw = true;
}

static void led_update_display(void *opaque)
{
    LEDMatrixState *s = LED_MATRIX(opaque);
    DisplaySurface *surface = qemu_console_surface(s->con);
    color_func colorfunc;
    uint32_t color_led;
    int bpp;
    uint8_t *d1;
    uint8_t red;
    size_t x, y, idx;

    if (!s->redraw) {
        return;
    }

    /* clear screen */
    bpp = (surface_bits_per_pixel(surface) + 7) >> 3;
    d1 = surface_data(surface);
    for (y = 0; y < surface_height(surface); y++) {
        memset(d1, 0x00, surface_width(surface) * bpp);
        d1 += surface_stride(surface);
    }

    /* set colors according to bpp */
    switch (surface_bits_per_pixel(surface)) {
    case 8:
        colorfunc = rgb_to_pixel8;
        break;
    case 15:
        colorfunc = rgb_to_pixel15;
        break;
    case 16:
        colorfunc = rgb_to_pixel16;
        break;
    case 24:
        colorfunc = rgb_to_pixel24;
        break;
    case 32:
        colorfunc = rgb_to_pixel32;
        break;
    default:
        return;
    }

    for (x = 0; x < s->nrows ; x++) {
        for (y = 0; y < s->ncols; y++) {
            idx = x * s->ncols + y;
            red = (s->led_frame_dc[idx] * 0xFF) / (s->refresh_period * 1000);
            color_led = colorfunc(red, 0x00, 0x00);

            draw_box(surface, idx * 10, 0, 5, 10, color_led);
        }
    }

    s->redraw = 0;
    dpy_gfx_update(s->con, 0, 0, 500, 500);
}

static const GraphicHwOps graphic_ops = {
    .invalidate  = led_invalidate_display,
    .gfx_update  = led_update_display,
};

static void led_matrix_init(Object *obj)
{
    LEDMatrixState *s = LED_MATRIX(obj);

    timer_init_ms(&s->timer, QEMU_CLOCK_VIRTUAL, led_timer_expire, s);
}

static void led_matrix_realize(DeviceState *dev, Error **errp)
{
    LEDMatrixState *s = LED_MATRIX(dev);
    if (!s->nrows || (s->nrows > 64)) {
        error_setg(errp, "rows not set or larger than 64");
        return;
    }

    if (!s->ncols || (s->ncols > 64)) {
        error_setg(errp, "cols not set or larger than 64");
        return;
    }

    s->led_working_dc = g_malloc0_n(s->ncols * s->nrows, sizeof(int64_t));
    s->led_frame_dc = g_malloc0_n(s->ncols * s->nrows, sizeof(int64_t));

    qdev_init_gpio_in_named(dev, set_row, "row", s->nrows);
    qdev_init_gpio_in_named(dev, set_column, "col", s->ncols);

    s->con = graphic_console_init(NULL, 0, &graphic_ops, s);
    qemu_console_resize(s->con, 500, 500);
}

static void led_matrix_reset(DeviceState *dev)
{
    LEDMatrixState *s = LED_MATRIX(dev);

    timer_mod(&s->timer,
            qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + s->refresh_period);
}

static void led_matrix_unrealize(DeviceState *dev, Error **errp)
{
    LEDMatrixState *s = LED_MATRIX(dev);

    g_free(s->led_working_dc);
    s->led_working_dc = NULL;
}

static Property led_matrix_properties[] = {
    DEFINE_PROP_UINT32("refresh_period", LEDMatrixState, refresh_period, 500),
    DEFINE_PROP_UINT8("rows", LEDMatrixState, nrows, 0),
    DEFINE_PROP_UINT8("cols", LEDMatrixState, ncols, 0),
    DEFINE_PROP_BOOL("strobe_row", LEDMatrixState, strobe_row, true),
    /* TODO Save/restore state of led_state matrix */
    DEFINE_PROP_END_OF_LIST(),
};

static void led_matrix_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->props = led_matrix_properties;
    dc->realize = led_matrix_realize;
    dc->reset = led_matrix_reset;
    dc->unrealize = led_matrix_unrealize;
}

static const TypeInfo led_matrix_info = {
    .name = TYPE_LED_MATRIX,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LEDMatrixState),
    .instance_init = led_matrix_init,
    .class_init = led_matrix_class_init
};

static void led_matrix_register_types(void)
{
    type_register_static(&led_matrix_info);
}

type_init(led_matrix_register_types)
