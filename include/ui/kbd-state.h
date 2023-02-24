/*
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#ifndef QEMU_UI_KBD_STATE_H
#define QEMU_UI_KBD_STATE_H

#include "qapi/qapi-types-ui.h"

typedef enum QKbdModifier QKbdModifier;

enum QKbdModifier {
    Q_KBD_MODIFIER_NONE = 0,

    Q_KBD_MODIFIER_SHIFT,
    Q_KBD_MODIFIER_CTRL,
    Q_KBD_MODIFIER_ALT,
    Q_KBD_MODIFIER_ALTGR,

    Q_KBD_MODIFIER_NUMLOCK,
    Q_KBD_MODIFIER_CAPSLOCK,

    Q_KBD_MODIFIER__MAX
};

typedef struct QKbdState QKbdState;

/**
 * qkbd_state_init: init keyboard state tracker.
 *
 * Allocates and initializes keyboard state struct.
 *
 * @con: QemuConsole for this state tracker.  Gets passed down to
 * qemu_input_*() functions when sending key events to the guest.
 */
QKbdState *qkbd_state_init(QemuConsole *con);

/**
 * qkbd_state_free: free keyboard tracker state.
 *
 * @kbd: state tracker state.
 */
void qkbd_state_free(QKbdState *kbd);

/**
 * qkbd_state_key_event: process key event.
 *
 * Update keyboard state, send event to the guest.
 *
 * This function takes care to not send suspious events (keyup event
 * for a key not pressed for example).
 *
 * @kbd: state tracker state.
 * @qcode: the key pressed or released.
 * @down: true for key down events, false otherwise.
 */
void qkbd_state_key_event(QKbdState *kbd, QKeyCode qcode, bool down);

/**
 * qkbd_state_set_delay: set key press delay.
 *
 * When set the specified delay will be added after each key event,
 * using qemu_input_event_send_key_delay().
 *
 * @kbd: state tracker state.
 * @delay_ms: the delay in miliseconds.
 */
void qkbd_state_set_delay(QKbdState *kbd, int delay_ms);

/**
 * qkbd_state_key_get: get key state.
 *
 * Returns true when the key is down.
 *
 * @kbd: state tracker state.
 * @qcode: the key to query.
 */
bool qkbd_state_key_get(QKbdState *kbd, QKeyCode qcode);

/**
 * qkbd_state_modifier_get: get modifier state.
 *
 * Returns true when the modifier is active.
 *
 * @kbd: state tracker state.
 * @mod: the modifier to query.
 */
bool qkbd_state_modifier_get(QKbdState *kbd, QKbdModifier mod);

/**
 * qkbd_state_lift_all_keys: lift all pressed keys.
 *
 * This sends key up events to the guest for all keys which are in
 * down state.
 *
 * @kbd: state tracker state.
 */
void qkbd_state_lift_all_keys(QKbdState *kbd);

#endif /* QEMU_UI_KBD_STATE_H */
