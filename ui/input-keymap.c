#include "qemu/osdep.h"
#include "sysemu/sysemu.h"
#include "ui/keymaps.h"
#include "ui/input.h"

#include "standard-headers/linux/input.h"

#include "ui/input-keymap-atset12qcode.c"
#include "ui/input-keymap-linux2qcode.c"
#include "ui/input-keymap-osx2qcode.c"
#include "ui/input-keymap-qcode2adb.c"
#include "ui/input-keymap-qcode2atset1.c"
#include "ui/input-keymap-qcode2atset2.c"
#include "ui/input-keymap-qcode2atset3.c"
#include "ui/input-keymap-qcode2linux.c"
#include "ui/input-keymap-qcode2qnum.c"
#include "ui/input-keymap-qcode2sun.c"
#include "ui/input-keymap-qnum2qcode.c"
#include "ui/input-keymap-usb2qcode.c"
#include "ui/input-keymap-win322qcode.c"
#include "ui/input-keymap-x112qcode.c"
#include "ui/input-keymap-xorgevdev2qcode.c"
#include "ui/input-keymap-xorgkbd2qcode.c"
#include "ui/input-keymap-xorgxquartz2qcode.c"
#include "ui/input-keymap-xorgxwin2qcode.c"

int qemu_input_key_number_to_qcode(unsigned int nr)
{
    if (nr >= qemu_input_map_qnum2qcode_len) {
        return 0;
    }
    return qemu_input_map_qnum2qcode[nr];
}

int qemu_input_qcode_to_scancode(QKeyCode qcode, bool down,
                                 int *codes)
{
    int keycode;
    int count = 0;

    if (qcode >= qemu_input_map_qcode2qnum_len) {
        keycode = 0;
    } else {
        keycode = qemu_input_map_qcode2qnum[qcode];
    }

    if (qcode == Q_KEY_CODE_PAUSE) {
        /* specific case */
        int v = down ? 0 : 0x80;
        codes[count++] = 0xe1;
        codes[count++] = 0x1d | v;
        codes[count++] = 0x45 | v;
        return count;
    }
    if (keycode & SCANCODE_GREY) {
        codes[count++] = SCANCODE_EMUL0;
        keycode &= ~SCANCODE_GREY;
    }
    if (!down) {
        keycode |= SCANCODE_UP;
    }
    codes[count++] = keycode;

    return count;
}
