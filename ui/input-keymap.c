#include "qemu/osdep.h"
#include "sysemu/sysemu.h"
#include "ui/keymaps.h"
#include "ui/input.h"

#include "standard-headers/linux/input.h"

#include "ui/input-keymap-linux2qcode.c"
#include "ui/input-keymap-qcode2qnum.c"
#include "ui/input-keymap-qnum2qcode.c"

int qemu_input_linux_to_qcode(unsigned int lnx)
{
    if (lnx >= qemu_input_map_linux2qcode_len) {
        return 0;
    }
    return qemu_input_map_linux2qcode[lnx];
}

int qemu_input_qcode_to_number(QKeyCode qcode)
{
    if (qcode >= qemu_input_map_qcode2qnum_len) {
        return 0;
    }
    return qemu_input_map_qcode2qnum[qcode];
}

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
    int keycode = qemu_input_qcode_to_number(qcode);
    int count = 0;

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
