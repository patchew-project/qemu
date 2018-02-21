#include "qemu/osdep.h"
#include "qemu/bitmap.h"
#include "qemu/queue.h"
#include "qapi-types.h"
#include "ui/console.h"
#include "ui/input.h"
#include "ui/kbd-state.h"

typedef struct KbdHotkeyEntry KbdHotkeyEntry;

struct KbdHotkeyEntry {
    uint32_t id;
    QKeyCode qcode;
    DECLARE_BITMAP(mods, KBD_MOD__MAX);
    QTAILQ_ENTRY(KbdHotkeyEntry) next;
};

struct KbdState {
    QemuConsole *con;
    DECLARE_BITMAP(keys, Q_KEY_CODE__MAX);
    DECLARE_BITMAP(mods, KBD_MOD__MAX);
    QTAILQ_HEAD(, KbdHotkeyEntry) hotkeys;
};

static void kbd_state_modifier_update(KbdState *kbd,
                                      QKeyCode qcode1, QKeyCode qcode2,
                                      KbdModifier mod)
{
    if (test_bit(qcode1, kbd->keys) || test_bit(qcode2, kbd->keys)) {
        set_bit(mod, kbd->mods);
    } else {
        clear_bit(mod, kbd->mods);
    }
}

bool kbd_state_modifier_get(KbdState *kbd, KbdModifier mod)
{
    return test_bit(mod, kbd->mods);
}

bool kbd_state_key_get(KbdState *kbd, QKeyCode qcode)
{
    return test_bit(qcode, kbd->keys);
}

void kbd_state_key_event(KbdState *kbd, QKeyCode qcode, bool down)
{
    bool state = test_bit(qcode, kbd->keys);

    if (state == down) {
        /*
         * Filter out events which don't change the keyboard state.
         *
         * Most notably this allows to simply send along all key-up
         * events, and this function will filter out everything where
         * the corresponding key-down event wasn't send to the guest,
         * for example due to being a host hotkey.
         */
        return;
    }

    /* update key and modifier state */
    change_bit(qcode, kbd->keys);
    switch (qcode) {
    case Q_KEY_CODE_SHIFT:
    case Q_KEY_CODE_SHIFT_R:
        kbd_state_modifier_update(kbd, Q_KEY_CODE_SHIFT, Q_KEY_CODE_SHIFT_R,
                                  KBD_MOD_SHIFT);
        break;
    case Q_KEY_CODE_CTRL:
    case Q_KEY_CODE_CTRL_R:
        kbd_state_modifier_update(kbd, Q_KEY_CODE_CTRL, Q_KEY_CODE_CTRL_R,
                                  KBD_MOD_CTRL);
        break;
    case Q_KEY_CODE_ALT:
        kbd_state_modifier_update(kbd, Q_KEY_CODE_ALT, Q_KEY_CODE_ALT,
                                  KBD_MOD_ALT);
        break;
    case Q_KEY_CODE_CAPS_LOCK:
        if (down) {
            change_bit(KBD_MOD_CAPSLOCK, kbd->mods);
        }
        break;
    case Q_KEY_CODE_NUM_LOCK:
        if (down) {
            change_bit(KBD_MOD_NUMLOCK, kbd->mods);
        }
        break;
    default:
        /* keep gcc happy */
        break;
    }

    /* send to guest */
    if (qemu_console_is_graphic(kbd->con)) {
        qemu_input_event_send_key_qcode(kbd->con, qcode, down);
    }
}

void kbd_state_lift_all_keys(KbdState *kbd)
{
    int qcode;

    for (qcode = 0; qcode < Q_KEY_CODE__MAX; qcode++) {
        if (test_bit(qcode, kbd->keys)) {
            kbd_state_key_event(kbd, qcode, false);
        }
    }
}

KbdState *kbd_state_init(QemuConsole *con)
{
    KbdState *kbd = g_new0(KbdState, 1);

    kbd->con = con;
    QTAILQ_INIT(&kbd->hotkeys);

    return kbd;
}

void kbd_state_hotkey_register(KbdState *kbd, KbdHotkey id, QKeyCode qcode,
                               KbdModifier mod1, KbdModifier mod2,
                               KbdModifier mod3)
{
    KbdHotkeyEntry *hotkey = g_new0(KbdHotkeyEntry, 1);

    hotkey->id    = id;
    hotkey->qcode = qcode;
    if (mod1 != KBD_MOD_NONE) {
        set_bit(mod1, hotkey->mods);
    }
    if (mod2 != KBD_MOD_NONE) {
        set_bit(mod2, hotkey->mods);
    }
    if (mod3 != KBD_MOD_NONE) {
        set_bit(mod3, hotkey->mods);
    }
    QTAILQ_INSERT_TAIL(&kbd->hotkeys, hotkey, next);
}

KbdHotkey kbd_state_hotkey_get(KbdState *kbd, QKeyCode qcode)
{
    KbdHotkeyEntry *hotkey;

    QTAILQ_FOREACH(hotkey, &kbd->hotkeys, next) {
        if (qcode == hotkey->qcode &&
            bitmap_equal(kbd->mods, hotkey->mods, KBD_MOD__MAX)) {
            return hotkey->id;
        }
    }
    return KBD_HOTKEY_NONE;
}
