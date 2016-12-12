#include "qemu/osdep.h"
#include "char-win.h"

static void qemu_chr_open_win_con(Chardev *chr,
                                  ChardevBackend *backend,
                                  bool *be_opened,
                                  Error **errp)
{
    qemu_chr_open_win_file(chr, GetStdHandle(STD_OUTPUT_HANDLE));
}

static void char_console_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->open = qemu_chr_open_win_con;
}

static const TypeInfo char_console_type_info = {
    .name = TYPE_CHARDEV_CONSOLE,
    .parent = TYPE_CHARDEV_WIN,
    .class_init = char_console_class_init,
};

static void register_types(void)
{
    type_register_static(&char_console_type_info);
}

type_init(register_types);
