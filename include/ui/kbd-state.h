typedef enum KbdModifier KbdModifier;

enum KbdModifier {
    KBD_MOD_NONE = 0,

    KBD_MOD_SHIFT,
    KBD_MOD_CTRL,
    KBD_MOD_ALT,

    KBD_MOD_NUMLOCK,
    KBD_MOD_CAPSLOCK,

    KBD_MOD__MAX
};

typedef struct KbdState KbdState;

bool kbd_state_modifier_get(KbdState *kbd, KbdModifier mod);
bool kbd_state_key_get(KbdState *kbd, QKeyCode qcode);
void kbd_state_key_event(KbdState *kbd, QKeyCode qcode, bool down);
void kbd_state_lift_all_keys(KbdState *kbd);
KbdState *kbd_state_init(QemuConsole *con);

/* ------------------------------------------------------------------ */

typedef enum KbdHotkey KbdHotkey;

enum KbdHotkey {
    KBD_HOTKEY_NONE = 0,

    KBD_HOTKEY_GRAB,
    KBD_HOTKEY_FULLSCREEN,
    KBD_HOTKEY_REDRAW,

    KBD_HOTKEY_CONSOLE_1,
    KBD_HOTKEY_CONSOLE_2,
    KBD_HOTKEY_CONSOLE_3,
    KBD_HOTKEY_CONSOLE_4,
    KBD_HOTKEY_CONSOLE_5,
    KBD_HOTKEY_CONSOLE_6,
    KBD_HOTKEY_CONSOLE_7,
    KBD_HOTKEY_CONSOLE_8,
    KBD_HOTKEY_CONSOLE_9,
};

void kbd_state_hotkey_register(KbdState *kbd, KbdHotkey, QKeyCode qcode,
                               KbdModifier mod1, KbdModifier mod2,
                               KbdModifier mod3);
KbdHotkey kbd_state_hotkey_get(KbdState *kbd, QKeyCode qcode);
