/*
 * QEMU HID devices
 *
 * Copyright (c) 2005 Fabrice Bellard
 * Copyright (c) 2007 OpenMoko, Inc.  (andrew@openedhand.com)
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
#include "hw/hw.h"
#include "ui/console.h"
#include "qemu/timer.h"
#include "hw/input/hid.h"
#include "include/hw/input/usb-keys.h"
#include <math.h>

#define HID_USAGE_ERROR_ROLLOVER        0x01
#define HID_USAGE_POSTFAIL              0x02
#define HID_USAGE_ERROR_UNDEFINED       0x03

#define RELEASED -1
#define PUSHED -2

/* #define DEBUG_HID_CODE */
#ifdef DEBUG_HID_CODE
    #define DEBUG_HID(fmt, ...) printf(fmt, __VA_ARGS__)
#else
    #define DEBUG_HID(fmt, ...) (void)0
#endif

/* Translates a QKeyCode to USB HID value */
static const uint8_t qcode_to_usb_hid[] = {
    [Q_KEY_CODE_SHIFT] = USB_HID_LEFT_SHIFT,
    [Q_KEY_CODE_SHIFT_R] = USB_HID_RIGHT_SHIFT,
    [Q_KEY_CODE_ALT] = USB_HID_LEFT_OPTION,
    [Q_KEY_CODE_ALT_R] = USB_HID_RIGHT_OPTION,
    [Q_KEY_CODE_ALTGR] = USB_HID_LEFT_OPTION,
    [Q_KEY_CODE_ALTGR_R] = USB_HID_RIGHT_OPTION,
    [Q_KEY_CODE_CTRL] = USB_HID_LEFT_CONTROL,
    [Q_KEY_CODE_CTRL_R] = USB_HID_RIGHT_CONTROL,
    [Q_KEY_CODE_MENU] = USB_HID_MENU,
    [Q_KEY_CODE_ESC] = USB_HID_ESC,
    [Q_KEY_CODE_1] = USB_HID_1,
    [Q_KEY_CODE_2] = USB_HID_2,
    [Q_KEY_CODE_3] = USB_HID_3,
    [Q_KEY_CODE_4] = USB_HID_4,
    [Q_KEY_CODE_5] = USB_HID_5,
    [Q_KEY_CODE_6] = USB_HID_6,
    [Q_KEY_CODE_7] = USB_HID_7,
    [Q_KEY_CODE_8] = USB_HID_8,
    [Q_KEY_CODE_9] = USB_HID_9,
    [Q_KEY_CODE_0] = USB_HID_0,
    [Q_KEY_CODE_MINUS] = USB_HID_MINUS,
    [Q_KEY_CODE_EQUAL] = USB_HID_EQUALS,
    [Q_KEY_CODE_BACKSPACE] = USB_HID_DELETE,
    [Q_KEY_CODE_TAB] = USB_HID_TAB,
    [Q_KEY_CODE_Q] = USB_HID_Q,
    [Q_KEY_CODE_W] = USB_HID_W,
    [Q_KEY_CODE_E] = USB_HID_E,
    [Q_KEY_CODE_R] = USB_HID_R,
    [Q_KEY_CODE_T] = USB_HID_T,
    [Q_KEY_CODE_Y] = USB_HID_Y,
    [Q_KEY_CODE_U] = USB_HID_U,
    [Q_KEY_CODE_I] = USB_HID_I,
    [Q_KEY_CODE_O] = USB_HID_O,
    [Q_KEY_CODE_P] = USB_HID_P,
    [Q_KEY_CODE_BRACKET_LEFT] = USB_HID_LEFT_BRACKET,
    [Q_KEY_CODE_BRACKET_RIGHT] = USB_HID_RIGHT_BRACKET,
    [Q_KEY_CODE_RET] = USB_HID_RETURN,
    [Q_KEY_CODE_A] = USB_HID_A,
    [Q_KEY_CODE_S] = USB_HID_S,
    [Q_KEY_CODE_D] = USB_HID_D,
    [Q_KEY_CODE_F] = USB_HID_F,
    [Q_KEY_CODE_G] = USB_HID_G,
    [Q_KEY_CODE_H] = USB_HID_H,
    [Q_KEY_CODE_J] = USB_HID_J,
    [Q_KEY_CODE_K] = USB_HID_K,
    [Q_KEY_CODE_L] = USB_HID_L,
    [Q_KEY_CODE_SEMICOLON] = USB_HID_SEMICOLON,
    [Q_KEY_CODE_APOSTROPHE] = USB_HID_QUOTE,
    [Q_KEY_CODE_GRAVE_ACCENT] = USB_HID_GRAVE_ACCENT,
    [Q_KEY_CODE_BACKSLASH] = USB_HID_BACKSLASH,
    [Q_KEY_CODE_Z] = USB_HID_Z,
    [Q_KEY_CODE_X] = USB_HID_X,
    [Q_KEY_CODE_C] = USB_HID_C,
    [Q_KEY_CODE_V] = USB_HID_V,
    [Q_KEY_CODE_B] = USB_HID_B,
    [Q_KEY_CODE_N] = USB_HID_N,
    [Q_KEY_CODE_M] = USB_HID_M,
    [Q_KEY_CODE_COMMA] = USB_HID_COMMA,
    [Q_KEY_CODE_DOT] = USB_HID_PERIOD,
    [Q_KEY_CODE_SLASH] = USB_HID_FORWARD_SLASH,
    [Q_KEY_CODE_ASTERISK] = USB_HID_KP_MULTIPLY,
    [Q_KEY_CODE_SPC] = USB_HID_SPACE,
    [Q_KEY_CODE_CAPS_LOCK] = USB_HID_CAPS_LOCK,
    [Q_KEY_CODE_F1] = USB_HID_F1,
    [Q_KEY_CODE_F2] = USB_HID_F2,
    [Q_KEY_CODE_F3] = USB_HID_F3,
    [Q_KEY_CODE_F4] = USB_HID_F4,
    [Q_KEY_CODE_F5] = USB_HID_F5,
    [Q_KEY_CODE_F6] = USB_HID_F6,
    [Q_KEY_CODE_F7] = USB_HID_F7,
    [Q_KEY_CODE_F8] = USB_HID_F8,
    [Q_KEY_CODE_F9] = USB_HID_F9,
    [Q_KEY_CODE_F10] = USB_HID_F10,
    [Q_KEY_CODE_NUM_LOCK] = USB_HID_CLEAR,
    [Q_KEY_CODE_SCROLL_LOCK] = USB_HID_SCROLL_LOCK,
    [Q_KEY_CODE_KP_DIVIDE] = USB_HID_KP_DIVIDE,
    [Q_KEY_CODE_KP_MULTIPLY] = USB_HID_KP_MULTIPLY,
    [Q_KEY_CODE_KP_SUBTRACT] = USB_HID_KP_MINUS,
    [Q_KEY_CODE_KP_ADD] = USB_HID_KP_ADD,
    [Q_KEY_CODE_KP_ENTER] = USB_HID_KP_ENTER,
    [Q_KEY_CODE_KP_DECIMAL] = USB_HID_KP_PERIOD,
    [Q_KEY_CODE_SYSRQ] = USB_HID_PRINT,
    [Q_KEY_CODE_KP_0] = USB_HID_KP_0,
    [Q_KEY_CODE_KP_1] = USB_HID_KP_1,
    [Q_KEY_CODE_KP_2] = USB_HID_KP_2,
    [Q_KEY_CODE_KP_3] = USB_HID_KP_3,
    [Q_KEY_CODE_KP_4] = USB_HID_KP_4,
    [Q_KEY_CODE_KP_5] = USB_HID_KP_5,
    [Q_KEY_CODE_KP_6] = USB_HID_KP_6,
    [Q_KEY_CODE_KP_7] = USB_HID_KP_7,
    [Q_KEY_CODE_KP_8] = USB_HID_KP_8,
    [Q_KEY_CODE_KP_9] = USB_HID_KP_9,
    [Q_KEY_CODE_LESS] = 0,
    [Q_KEY_CODE_F11] = USB_HID_F11,
    [Q_KEY_CODE_F12] = USB_HID_F12,
    [Q_KEY_CODE_PRINT] = USB_HID_PRINT,
    [Q_KEY_CODE_HOME] = USB_HID_HOME,
    [Q_KEY_CODE_PGUP] = USB_HID_PAGE_UP,
    [Q_KEY_CODE_PGDN] = USB_HID_PAGE_DOWN,
    [Q_KEY_CODE_END] = USB_HID_END,
    [Q_KEY_CODE_LEFT] = USB_HID_LEFT_ARROW,
    [Q_KEY_CODE_UP] = USB_HID_UP_ARROW,
    [Q_KEY_CODE_DOWN] = USB_HID_DOWN_ARROW,
    [Q_KEY_CODE_RIGHT] = USB_HID_RIGHT_ARROW,
    [Q_KEY_CODE_INSERT] = USB_HID_INSERT,
    [Q_KEY_CODE_DELETE] = USB_HID_FORWARD_DELETE,
    [Q_KEY_CODE_STOP] = USB_HID_STOP,
    [Q_KEY_CODE_AGAIN] = USB_HID_AGAIN,
    [Q_KEY_CODE_PROPS] = 0,
    [Q_KEY_CODE_UNDO] = USB_HID_UNDO,
    [Q_KEY_CODE_FRONT] = 0,
    [Q_KEY_CODE_COPY] = USB_HID_COPY,
    [Q_KEY_CODE_OPEN] = 0,
    [Q_KEY_CODE_PASTE] = USB_HID_PASTE,
    [Q_KEY_CODE_FIND] = USB_HID_FIND,
    [Q_KEY_CODE_CUT] = USB_HID_CUT,
    [Q_KEY_CODE_LF] = 0,
    [Q_KEY_CODE_HELP] = USB_HID_HELP,
    [Q_KEY_CODE_META_L] = USB_HID_LEFT_GUI,
    [Q_KEY_CODE_META_R] = USB_HID_RIGHT_GUI,
    [Q_KEY_CODE_COMPOSE] = 0,
    [Q_KEY_CODE_PAUSE] = USB_HID_PAUSE,
    [Q_KEY_CODE_RO] = 0,
    [Q_KEY_CODE_KP_COMMA] = USB_HID_KP_COMMA,
    [Q_KEY_CODE_KP_EQUALS] = USB_HID_KP_EQUALS,
    [Q_KEY_CODE_POWER] = USB_HID_POWER,
};

bool hid_has_events(HIDState *hs)
{
    return hs->n > 0 || hs->idle_pending;
}

static void hid_idle_timer(void *opaque)
{
    HIDState *hs = opaque;

    hs->idle_pending = true;
    hs->event(hs);
}

static void hid_del_idle_timer(HIDState *hs)
{
    if (hs->idle_timer) {
        timer_del(hs->idle_timer);
        timer_free(hs->idle_timer);
        hs->idle_timer = NULL;
    }
}

void hid_set_next_idle(HIDState *hs)
{
    if (hs->idle) {
        uint64_t expire_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                               NANOSECONDS_PER_SECOND * hs->idle * 4 / 1000;
        if (!hs->idle_timer) {
            hs->idle_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, hid_idle_timer, hs);
        }
        timer_mod_ns(hs->idle_timer, expire_time);
    } else {
        hid_del_idle_timer(hs);
    }
}

static void hid_pointer_event(DeviceState *dev, QemuConsole *src,
                              InputEvent *evt)
{
    static const int bmap[INPUT_BUTTON__MAX] = {
        [INPUT_BUTTON_LEFT]   = 0x01,
        [INPUT_BUTTON_RIGHT]  = 0x02,
        [INPUT_BUTTON_MIDDLE] = 0x04,
    };
    HIDState *hs = (HIDState *)dev;
    HIDPointerEvent *e;
    InputMoveEvent *move;
    InputBtnEvent *btn;

    assert(hs->n < QUEUE_LENGTH);
    e = &hs->ptr.queue[(hs->head + hs->n) & QUEUE_MASK];

    switch (evt->type) {
    case INPUT_EVENT_KIND_REL:
        move = evt->u.rel.data;
        if (move->axis == INPUT_AXIS_X) {
            e->xdx += move->value;
        } else if (move->axis == INPUT_AXIS_Y) {
            e->ydy += move->value;
        }
        break;

    case INPUT_EVENT_KIND_ABS:
        move = evt->u.abs.data;
        if (move->axis == INPUT_AXIS_X) {
            e->xdx = move->value;
        } else if (move->axis == INPUT_AXIS_Y) {
            e->ydy = move->value;
        }
        break;

    case INPUT_EVENT_KIND_BTN:
        btn = evt->u.btn.data;
        if (btn->down) {
            e->buttons_state |= bmap[btn->button];
            if (btn->button == INPUT_BUTTON_WHEEL_UP) {
                e->dz--;
            } else if (btn->button == INPUT_BUTTON_WHEEL_DOWN) {
                e->dz++;
            }
        } else {
            e->buttons_state &= ~bmap[btn->button];
        }
        break;

    default:
        /* keep gcc happy */
        break;
    }

}

static void hid_pointer_sync(DeviceState *dev)
{
    HIDState *hs = (HIDState *)dev;
    HIDPointerEvent *prev, *curr, *next;
    bool event_compression = false;

    if (hs->n == QUEUE_LENGTH-1) {
        /*
         * Queue full.  We are losing information, but we at least
         * keep track of most recent button state.
         */
        return;
    }

    prev = &hs->ptr.queue[(hs->head + hs->n - 1) & QUEUE_MASK];
    curr = &hs->ptr.queue[(hs->head + hs->n) & QUEUE_MASK];
    next = &hs->ptr.queue[(hs->head + hs->n + 1) & QUEUE_MASK];

    if (hs->n > 0) {
        /*
         * No button state change between previous and current event
         * (and previous wasn't seen by the guest yet), so there is
         * motion information only and we can combine the two event
         * into one.
         */
        if (curr->buttons_state == prev->buttons_state) {
            event_compression = true;
        }
    }

    if (event_compression) {
        /* add current motion to previous, clear current */
        if (hs->kind == HID_MOUSE) {
            prev->xdx += curr->xdx;
            curr->xdx = 0;
            prev->ydy += curr->ydy;
            curr->ydy = 0;
        } else {
            prev->xdx = curr->xdx;
            prev->ydy = curr->ydy;
        }
        prev->dz += curr->dz;
        curr->dz = 0;
    } else {
        /* prepate next (clear rel, copy abs + btns) */
        if (hs->kind == HID_MOUSE) {
            next->xdx = 0;
            next->ydy = 0;
        } else {
            next->xdx = curr->xdx;
            next->ydy = curr->ydy;
        }
        next->dz = 0;
        next->buttons_state = curr->buttons_state;
        /* make current guest visible, notify guest */
        hs->n++;
        hs->event(hs);
    }
}

static void hid_keyboard_event(DeviceState *dev, QemuConsole *src,
                               InputEvent *evt)
{
    HIDState *hs = (HIDState *)dev;
    int scancodes[3], i, count;
    int slot, qcode, keycode;

    qcode = qemu_input_key_value_to_qcode(evt->u.key.data->key);
    if (qcode >= ARRAY_SIZE(qcode_to_usb_hid)) {
        return;
    }
    keycode = qcode_to_usb_hid[qcode];
    DEBUG_HID("keycode = 0x%x qcode:%d\n", keycode, qcode);

    count = 2;
    if (evt->u.key.data->down == false) { /* if key up event */
        scancodes[0] = RELEASED;
    } else {
        scancodes[0] = PUSHED;
    }
    scancodes[1] = keycode;

    if (hs->n + count > QUEUE_LENGTH) {
        fprintf(stderr, "usb-kbd: warning: key event queue full\n");
        return;
    }
    for (i = 0; i < count; i++) {
        slot = (hs->head + hs->n) & QUEUE_MASK; hs->n++;
        hs->kbd.keycodes[slot] = scancodes[i];
    }
    hs->event(hs);
}

/* Sets the modifiers variable */
static void set_modifiers(int status, int bit_position, uint16_t *modifiers)
{
    int value = pow(2, bit_position);
    if (status == PUSHED) {
        *modifiers |= value;
    }  else {
        *modifiers &= ~value;
    }
}

/* Handles the modifier keys - they are handled differently from other keys. */
static void process_modifier_key(int status, int keycode, uint16_t *modifiers)
{
    /* subtracting 0xe0 from the keycode gives us the bit position */
    set_modifiers(status, keycode - 0xe0, modifiers);
}

static void hid_keyboard_process_keycode(HIDState *hs)
{
    int i, keycode, slot, status;

    if (hs->n == 0) {
        return;
    }
    slot = hs->head & QUEUE_MASK; QUEUE_INCR(hs->head); hs->n--;
    status = hs->kbd.keycodes[slot];
    slot = hs->head & QUEUE_MASK; QUEUE_INCR(hs->head); hs->n--;
    keycode = hs->kbd.keycodes[slot];

    DEBUG_HID("keycode:0x%x status:%s\n", keycode, (status == PUSHED ? "Pushed"
              : "Released"));

    /* handle Control, Option, GUI/Windows/Command, and Shift keys */
    if (keycode >= 0xe0) {
        process_modifier_key(status, keycode, &(hs->kbd.modifiers));
        return;
    }

    /* if key released */
    if (status == RELEASED) {
        /* find the key then remove it from the buffer */
        for (i = hs->kbd.keys - 1; i >= 0; i--) {
            if (hs->kbd.key[i] == keycode) {
                hs->kbd.key[i] = hs->kbd.key[-- hs->kbd.keys];
                hs->kbd.key[hs->kbd.keys] = 0x00;
                break;
            }
        }
        if (i < 0) {
            return;
        }
    } else {
        /* search for the key's location in the buffer */
        for (i = hs->kbd.keys - 1; i >= 0; i--) {
            if (hs->kbd.key[i] == keycode) {
                break;
            }
        }
        if (i < 0) {
            if (hs->kbd.keys < sizeof(hs->kbd.key)) {
                hs->kbd.key[hs->kbd.keys++] = keycode;
            }
        } else {
            return;
        }
    }
}

static inline int int_clamp(int val, int vmin, int vmax)
{
    if (val < vmin) {
        return vmin;
    } else if (val > vmax) {
        return vmax;
    } else {
        return val;
    }
}

void hid_pointer_activate(HIDState *hs)
{
    if (!hs->ptr.mouse_grabbed) {
        qemu_input_handler_activate(hs->s);
        hs->ptr.mouse_grabbed = 1;
    }
}

int hid_pointer_poll(HIDState *hs, uint8_t *buf, int len)
{
    int dx, dy, dz, l;
    int index;
    HIDPointerEvent *e;

    hs->idle_pending = false;

    hid_pointer_activate(hs);

    /* When the buffer is empty, return the last event.  Relative
       movements will all be zero.  */
    index = (hs->n ? hs->head : hs->head - 1);
    e = &hs->ptr.queue[index & QUEUE_MASK];

    if (hs->kind == HID_MOUSE) {
        dx = int_clamp(e->xdx, -127, 127);
        dy = int_clamp(e->ydy, -127, 127);
        e->xdx -= dx;
        e->ydy -= dy;
    } else {
        dx = e->xdx;
        dy = e->ydy;
    }
    dz = int_clamp(e->dz, -127, 127);
    e->dz -= dz;

    if (hs->n &&
        !e->dz &&
        (hs->kind == HID_TABLET || (!e->xdx && !e->ydy))) {
        /* that deals with this event */
        QUEUE_INCR(hs->head);
        hs->n--;
    }

    /* Appears we have to invert the wheel direction */
    dz = 0 - dz;
    l = 0;
    switch (hs->kind) {
    case HID_MOUSE:
        if (len > l) {
            buf[l++] = e->buttons_state;
        }
        if (len > l) {
            buf[l++] = dx;
        }
        if (len > l) {
            buf[l++] = dy;
        }
        if (len > l) {
            buf[l++] = dz;
        }
        break;

    case HID_TABLET:
        if (len > l) {
            buf[l++] = e->buttons_state;
        }
        if (len > l) {
            buf[l++] = dx & 0xff;
        }
        if (len > l) {
            buf[l++] = dx >> 8;
        }
        if (len > l) {
            buf[l++] = dy & 0xff;
        }
        if (len > l) {
            buf[l++] = dy >> 8;
        }
        if (len > l) {
            buf[l++] = dz;
        }
        break;

    default:
        abort();
    }

    return l;
}

int hid_keyboard_poll(HIDState *hs, uint8_t *buf, int len)
{
    hs->idle_pending = false;

    if (len < 2) {
        return 0;
    }

    hid_keyboard_process_keycode(hs);

    buf[0] = hs->kbd.modifiers & 0xff;
    buf[1] = 0;
    if (hs->kbd.keys > 6) {
        memset(buf + 2, HID_USAGE_ERROR_ROLLOVER, MIN(8, len) - 2);
    } else {
        memcpy(buf + 2, hs->kbd.key, MIN(8, len) - 2);
    }

    return MIN(8, len);
}

int hid_keyboard_write(HIDState *hs, uint8_t *buf, int len)
{
    if (len > 0) {
        int ledstate = 0;
        /* 0x01: Num Lock LED
         * 0x02: Caps Lock LED
         * 0x04: Scroll Lock LED
         * 0x08: Compose LED
         * 0x10: Kana LED */
        hs->kbd.leds = buf[0];
        if (hs->kbd.leds & 0x04) {
            ledstate |= QEMU_SCROLL_LOCK_LED;
        }
        if (hs->kbd.leds & 0x01) {
            ledstate |= QEMU_NUM_LOCK_LED;
        }
        if (hs->kbd.leds & 0x02) {
            ledstate |= QEMU_CAPS_LOCK_LED;
        }
        kbd_put_ledstate(ledstate);
    }
    return 0;
}

void hid_reset(HIDState *hs)
{
    switch (hs->kind) {
    case HID_KEYBOARD:
        memset(hs->kbd.keycodes, 0, sizeof(hs->kbd.keycodes));
        memset(hs->kbd.key, 0, sizeof(hs->kbd.key));
        hs->kbd.keys = 0;
        break;
    case HID_MOUSE:
    case HID_TABLET:
        memset(hs->ptr.queue, 0, sizeof(hs->ptr.queue));
        break;
    }
    hs->head = 0;
    hs->n = 0;
    hs->protocol = 1;
    hs->idle = 0;
    hs->idle_pending = false;
    hid_del_idle_timer(hs);
}

void hid_free(HIDState *hs)
{
    qemu_input_handler_unregister(hs->s);
    hid_del_idle_timer(hs);
}

static QemuInputHandler hid_keyboard_handler = {
    .name  = "QEMU HID Keyboard",
    .mask  = INPUT_EVENT_MASK_KEY,
    .event = hid_keyboard_event,
};

static QemuInputHandler hid_mouse_handler = {
    .name  = "QEMU HID Mouse",
    .mask  = INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_REL,
    .event = hid_pointer_event,
    .sync  = hid_pointer_sync,
};

static QemuInputHandler hid_tablet_handler = {
    .name  = "QEMU HID Tablet",
    .mask  = INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_ABS,
    .event = hid_pointer_event,
    .sync  = hid_pointer_sync,
};

void hid_init(HIDState *hs, int kind, HIDEventFunc event)
{
    hs->kind = kind;
    hs->event = event;

    if (hs->kind == HID_KEYBOARD) {
        hs->s = qemu_input_handler_register((DeviceState *)hs,
                                            &hid_keyboard_handler);
        qemu_input_handler_activate(hs->s);
    } else if (hs->kind == HID_MOUSE) {
        hs->s = qemu_input_handler_register((DeviceState *)hs,
                                            &hid_mouse_handler);
    } else if (hs->kind == HID_TABLET) {
        hs->s = qemu_input_handler_register((DeviceState *)hs,
                                            &hid_tablet_handler);
    }
}

static int hid_post_load(void *opaque, int version_id)
{
    HIDState *s = opaque;

    hid_set_next_idle(s);

    if (s->n == QUEUE_LENGTH && (s->kind == HID_TABLET ||
                                 s->kind == HID_MOUSE)) {
        /*
         * Handle ptr device migration from old qemu with full queue.
         *
         * Throw away everything but the last event, so we propagate
         * at least the current button state to the guest.  Also keep
         * current position for the tablet, signal "no motion" for the
         * mouse.
         */
        HIDPointerEvent evt;
        evt = s->ptr.queue[(s->head+s->n) & QUEUE_MASK];
        if (s->kind == HID_MOUSE) {
            evt.xdx = 0;
            evt.ydy = 0;
        }
        s->ptr.queue[0] = evt;
        s->head = 0;
        s->n = 1;
    }
    return 0;
}

static const VMStateDescription vmstate_hid_ptr_queue = {
    .name = "HIDPointerEventQueue",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(xdx, HIDPointerEvent),
        VMSTATE_INT32(ydy, HIDPointerEvent),
        VMSTATE_INT32(dz, HIDPointerEvent),
        VMSTATE_INT32(buttons_state, HIDPointerEvent),
        VMSTATE_END_OF_LIST()
    }
};

const VMStateDescription vmstate_hid_ptr_device = {
    .name = "HIDPointerDevice",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = hid_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(ptr.queue, HIDState, QUEUE_LENGTH, 0,
                             vmstate_hid_ptr_queue, HIDPointerEvent),
        VMSTATE_UINT32(head, HIDState),
        VMSTATE_UINT32(n, HIDState),
        VMSTATE_INT32(protocol, HIDState),
        VMSTATE_UINT8(idle, HIDState),
        VMSTATE_END_OF_LIST(),
    }
};

const VMStateDescription vmstate_hid_keyboard_device = {
    .name = "HIDKeyboardDevice",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = hid_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(kbd.keycodes, HIDState, QUEUE_LENGTH),
        VMSTATE_UINT32(head, HIDState),
        VMSTATE_UINT32(n, HIDState),
        VMSTATE_UINT16(kbd.modifiers, HIDState),
        VMSTATE_UINT8(kbd.leds, HIDState),
        VMSTATE_UINT8_ARRAY(kbd.key, HIDState, 16),
        VMSTATE_INT32(kbd.keys, HIDState),
        VMSTATE_INT32(protocol, HIDState),
        VMSTATE_UINT8(idle, HIDState),
        VMSTATE_END_OF_LIST(),
    }
};
