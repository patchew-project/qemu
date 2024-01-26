/*
 * QEMU DM163 8x3-channel constant current led driver
 * driving columns of associated 8x8 RGB matrix.
 *
 * Copyright (C) 2024 Samuel Tardieu <sam@rfc1149.net>
 * Copyright (C) 2024 Arnaud Minier <arnaud.minier@telecom-paris.fr>
 * Copyright (C) 2024 Inès Varhol <ines.varhol@telecom-paris.fr>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * The reference used for the DM163 is the following :
 * http://www.siti.com.tw/product/spec/LED/DM163.pdf
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/display/dm163.h"
#include "ui/console.h"
#include "trace.h"

#define LED_SQUARE_SIZE 100
/* Number of frames a row stays visible after being turned off. */
#define ROW_PERSISTANCE 2

static const VMStateDescription vmstate_dm163 = {
    .name = TYPE_DM163,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(activated_rows, DM163State),
        VMSTATE_UINT64_ARRAY(bank0_shift_register, DM163State, 3),
        VMSTATE_UINT64_ARRAY(bank1_shift_register, DM163State, 3),
        VMSTATE_UINT16_ARRAY(latched_outputs, DM163State, DM163_NUM_LEDS),
        VMSTATE_UINT16_ARRAY(outputs, DM163State, DM163_NUM_LEDS),
        VMSTATE_UINT8(dck, DM163State),
        VMSTATE_UINT8(en_b, DM163State),
        VMSTATE_UINT8(lat_b, DM163State),
        VMSTATE_UINT8(rst_b, DM163State),
        VMSTATE_UINT8(selbk, DM163State),
        VMSTATE_UINT8(sin, DM163State),
        VMSTATE_UINT32_2DARRAY(buffer, DM163State,
            COLOR_BUFFER_SIZE + 1, RGB_MATRIX_NUM_COLS),
        VMSTATE_UINT8(last_buffer_idx, DM163State),
        VMSTATE_UINT8_ARRAY(buffer_idx_of_row, DM163State, RGB_MATRIX_NUM_ROWS),
        VMSTATE_UINT8_ARRAY(age_of_row, DM163State, RGB_MATRIX_NUM_ROWS),
        VMSTATE_END_OF_LIST()
    }
};

static void dm163_reset_hold(Object *obj)
{
    DM163State *s = DM163(obj);

    /* Reset only stops the PWM. */
    memset(s->outputs, 0, sizeof(s->outputs));

    /* The last row of the buffer stores a turned off row */
    memset(s->buffer[COLOR_BUFFER_SIZE], 0, sizeof(s->buffer[0]));
}

static void dm163_dck_gpio_handler(void *opaque, int line, int new_state)
{
    DM163State *s = DM163(opaque);

    if (new_state && !s->dck) {
        /*
         * On raising dck, sample selbk to get the bank to use, and
         * sample sin for the bit to enter into the bank shift buffer.
         */
        uint64_t *sb =
            s->selbk ? s->bank1_shift_register : s->bank0_shift_register;
        /* Output the outgoing bit on sout */
        const bool sout = (s->selbk ? sb[2] & MAKE_64BIT_MASK(63, 1) :
                           sb[2] & MAKE_64BIT_MASK(15, 1)) != 0;
        qemu_set_irq(s->sout, sout);
        /* Enter sin into the shift buffer */
        sb[2] = (sb[2] << 1) | ((sb[1] >> 63) & 1);
        sb[1] = (sb[1] << 1) | ((sb[0] >> 63) & 1);
        sb[0] = (sb[0] << 1) | s->sin;
    }

    s->dck = new_state;
    trace_dm163_dck(new_state);
}

static void dm163_propagate_outputs(DM163State *s)
{
    s->last_buffer_idx = (s->last_buffer_idx + 1) % COLOR_BUFFER_SIZE;
    /* Values are output when reset and enable are both high. */
    if (s->rst_b && !s->en_b) {
        memcpy(s->outputs, s->latched_outputs, sizeof(s->outputs));
    } else {
        memset(s->outputs, 0, sizeof(s->outputs));
    }
    for (unsigned x = 0; x < RGB_MATRIX_NUM_COLS; x++) {
        trace_dm163_channels(3 * x, (uint8_t)(s->outputs[3 * x] >> 6));
        trace_dm163_channels(3 * x + 1, (uint8_t)(s->outputs[3 * x + 1] >> 6));
        trace_dm163_channels(3 * x + 2, (uint8_t)(s->outputs[3 * x + 2] >> 6));
        s->buffer[s->last_buffer_idx][x] =
            (s->outputs[3 * x + 2] >> 6) |
            ((s->outputs[3 * x + 1] << 2) & 0xFF00) |
            (((uint32_t)s->outputs[3 * x] << 10) & 0xFF0000);
    }
    for (unsigned row = 0; row < RGB_MATRIX_NUM_ROWS; row++) {
        if (s->activated_rows & (1 << row)) {
            s->buffer_idx_of_row[row] = s->last_buffer_idx;
        }
    }
}

static void dm163_en_b_gpio_handler(void *opaque, int line, int new_state)
{
    DM163State *s = DM163(opaque);

    s->en_b = new_state;
    dm163_propagate_outputs(s);
    trace_dm163_en_b(new_state);
}

static inline uint8_t dm163_bank0(const DM163State *s, uint8_t led)
{
    /*
     * Bank 1 uses 6 bits per led, so a value may be stored accross
     * two uint64_t entries.
     */
    const uint8_t low_bit = 6 * led;
    const uint8_t low_word = low_bit / 64;
    const uint8_t high_word = (low_bit + 5) / 64;
    const uint8_t low_shift = low_bit % 64;

    if (low_word == high_word) {
        /* Simple case: the value belongs to one entry. */
        return (s->bank0_shift_register[low_word] &
                MAKE_64BIT_MASK(low_shift, 6)) >> low_shift;
    }

    const uint8_t bits_in_low_word = 64 - low_shift;
    const uint8_t bits_in_high_word = 6 - bits_in_low_word;
    return ((s->bank0_shift_register[low_word] &
             MAKE_64BIT_MASK(low_shift, bits_in_low_word)) >>
            low_shift) |
           ((s->bank0_shift_register[high_word] &
             MAKE_64BIT_MASK(0, bits_in_high_word))
         << bits_in_low_word);
}

static inline uint8_t dm163_bank1(const DM163State *s, uint8_t led)
{
    const uint64_t entry = s->bank1_shift_register[led / 8];
    const unsigned shift = 8 * (led % 8);
    return (entry & MAKE_64BIT_MASK(shift, 8)) >> shift;
}

static void dm163_lat_b_gpio_handler(void *opaque, int line, int new_state)
{
    DM163State *s = DM163(opaque);

    if (s->lat_b && !new_state) {
        for (int led = 0; led < DM163_NUM_LEDS; led++) {
            s->latched_outputs[led] = dm163_bank0(s, led) * dm163_bank1(s, led);
        }
        dm163_propagate_outputs(s);
    }

    s->lat_b = new_state;
    trace_dm163_lat_b(new_state);
}

static void dm163_rst_b_gpio_handler(void *opaque, int line, int new_state)
{
    DM163State *s = DM163(opaque);

    s->rst_b = new_state;
    dm163_propagate_outputs(s);
    trace_dm163_rst_b(new_state);
}

static void dm163_selbk_gpio_handler(void *opaque, int line, int new_state)
{
    DM163State *s = DM163(opaque);

    s->selbk = new_state;
    trace_dm163_selbk(new_state);
}

static void dm163_sin_gpio_handler(void *opaque, int line, int new_state)
{
    DM163State *s = DM163(opaque);

    s->sin = new_state;
    trace_dm163_sin(new_state);
}

static void dm163_rows_gpio_handler(void *opaque, int line, int new_state)
{
    DM163State *s = DM163(opaque);

    if (new_state) {
        s->activated_rows |= (1 << line);
        s->buffer_idx_of_row[line] = s->last_buffer_idx;
        s->age_of_row[line] = 0;
    } else {
        s->activated_rows &= ~(1 << line);
        s->age_of_row[line] = ROW_PERSISTANCE;
    }
    trace_dm163_activated_rows(s->activated_rows);
}

static void dm163_invalidate_display(void *opaque)
{
}

static void dm163_update_display(void *opaque)
{
    DM163State *s = (DM163State *)opaque;
    DisplaySurface *surface = qemu_console_surface(s->console);
    uint32_t *dest;
    unsigned bits_ppi = surface_bits_per_pixel(surface);

    trace_dm163_bits_ppi(bits_ppi);
    g_assert((bits_ppi == 32));
    dest = surface_data(surface);
    for (unsigned y = 0; y < RGB_MATRIX_NUM_ROWS; y++) {
        for (unsigned _ = 0; _ < LED_SQUARE_SIZE; _++) {
            for (int x = RGB_MATRIX_NUM_COLS * LED_SQUARE_SIZE - 1; x >= 0; x--) {
                *dest++ = s->buffer[s->buffer_idx_of_row[y]][x / LED_SQUARE_SIZE];
            }
        }
        if (s->age_of_row[y]) {
            s->age_of_row[y]--;
            if (!s->age_of_row[y]) {
                /*
                 * If the ROW_PERSISTANCE delay is up,
                 * the row is turned off.
                 * (s->buffer[COLOR_BUFFER] is filled with 0)
                 */
                s->buffer_idx_of_row[y] = COLOR_BUFFER_SIZE;
            }
        }
    }
    /*
     * Ideally set the refresh rate so that the row persistance
     * doesn't need to be changed.
     *
     * Currently `dpy_ui_info_supported(s->console)` returns false
     * which makes it impossible to get or set UIInfo.
     *
     * if (dpy_ui_info_supported(s->console)) {
     *     trace_dm163_refresh_rate(dpy_get_ui_info(s->console)->refresh_rate);
     * } else {
     *     trace_dm163_refresh_rate(0);
     * }
     */
    dpy_gfx_update(s->console, 0, 0, RGB_MATRIX_NUM_COLS * LED_SQUARE_SIZE,
                   RGB_MATRIX_NUM_ROWS * LED_SQUARE_SIZE);
}

static const GraphicHwOps dm163_ops = {
    .invalidate  = dm163_invalidate_display,
    .gfx_update  = dm163_update_display,
};

static void dm163_realize(DeviceState *dev, Error **errp)
{
    DM163State *s = DM163(dev);

    qdev_init_gpio_in(dev, dm163_rows_gpio_handler, 8);
    qdev_init_gpio_in(dev, dm163_sin_gpio_handler, 1);
    qdev_init_gpio_in(dev, dm163_dck_gpio_handler, 1);
    qdev_init_gpio_in(dev, dm163_rst_b_gpio_handler, 1);
    qdev_init_gpio_in(dev, dm163_lat_b_gpio_handler, 1);
    qdev_init_gpio_in(dev, dm163_selbk_gpio_handler, 1);
    qdev_init_gpio_in(dev, dm163_en_b_gpio_handler, 1);
    qdev_init_gpio_out_named(dev, &s->sout, "sout", 1);

    s->console = graphic_console_init(dev, 0, &dm163_ops, s);
    qemu_console_resize(s->console, RGB_MATRIX_NUM_COLS * LED_SQUARE_SIZE,
                        RGB_MATRIX_NUM_ROWS * LED_SQUARE_SIZE);
}

static void dm163_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->desc = "DM163";
    dc->vmsd = &vmstate_dm163;
    dc->realize = dm163_realize;
    rc->phases.hold = dm163_reset_hold;
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
}

static const TypeInfo dm163_types[] = {
    {
        .name = TYPE_DM163,
        .parent = TYPE_DEVICE,
        .instance_size = sizeof(DM163State),
        .class_init = dm163_class_init
    }
};

DEFINE_TYPES(dm163_types)
