#ifndef QEMU_UI_KBD_STATE_H
#define QEMU_UI_KBD_STATE_H 1

#include "qapi/qapi-types-ui.h"

typedef enum KbdModifier KbdModifier;

enum KbdModifier {
    KBD_MOD_NONE = 0,

    KBD_MOD_SHIFT,
    KBD_MOD_CTRL,
    KBD_MOD_ALT,
    KBD_MOD_ALTGR,

    KBD_MOD_NUMLOCK,
    KBD_MOD_CAPSLOCK,

    KBD_MOD__MAX
};

typedef struct KbdState KbdState;

bool kbd_state_modifier_get(KbdState *kbd, KbdModifier mod);
bool kbd_state_key_get(KbdState *kbd, QKeyCode qcode);
void kbd_state_key_event(KbdState *kbd, QKeyCode qcode, bool down);
void kbd_state_lift_all_keys(KbdState *kbd);
void kbd_state_set_delay(KbdState *kbd, int delay_ms);
void kbd_state_free(KbdState *kbd);
KbdState *kbd_state_init(QemuConsole *con);

#endif /* QEMU_UI_KBD_STATE_H */
