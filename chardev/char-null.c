#include "qemu/osdep.h"
#include "sysemu/char.h"

static void null_chr_open(Chardev *chr,
                          ChardevBackend *backend,
                          bool *be_opened,
                          Error **errp)
{
    *be_opened = false;
}

static void char_null_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->open = null_chr_open;
}

static const TypeInfo char_null_type_info = {
    .name = TYPE_CHARDEV_NULL,
    .parent = TYPE_CHARDEV,
    .instance_size = sizeof(Chardev),
    .class_init = char_null_class_init,
};

static void register_types(void)
{
    type_register_static(&char_null_type_info);
}

type_init(register_types);
