/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * QEMU VC stubs
 */
#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/option.h"
#include "chardev/char.h"
#include "ui/console-priv.h"

void qemu_text_console_select(QemuTextConsole *c)
{
}

const char * qemu_text_console_get_label(QemuTextConsole *c)
{
    return NULL;
}

void qemu_text_console_update_cursor(void)
{
}

void qemu_text_console_handle_keysym(QemuTextConsole *s, int keysym)
{
}

#define TYPE_CHARDEV_VC "chardev-vc"

static void vc_chr_parse(QemuOpts *opts, ChardevBackend *backend, Error **errp)
{
    const char *id = qemu_opts_id(opts);

    warn_report("%s: this is a dummy VC driver. "
                "Use '-nographic' or a different chardev.", id);
}

static void char_vc_class_init(ObjectClass *oc, void *data)
{
    ChardevClass *cc = CHARDEV_CLASS(oc);

    cc->parse = vc_chr_parse;
}

static const TypeInfo char_vc_type_info = {
    .name = TYPE_CHARDEV_VC,
    .parent = TYPE_CHARDEV,
    .class_init = char_vc_class_init,
};

void qemu_console_early_init(void)
{
    /* set the default vc driver */
    if (!object_class_by_name(TYPE_CHARDEV_VC)) {
        type_register(&char_vc_type_info);
    }
}
