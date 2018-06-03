/*
 * stimulate.c
 *
 * Copyright (C) 2018 Steffen Görtz <contrib@steffen-goertz.de>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/log.h"
#include "qom/object.h"
#include "hw/qdev-core.h"
#include "hw/boards.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-stimulate.h"

#ifndef DEBUG_STIMULATE
#define DEBUG_STIMULATE 0
#endif

#define DPRINTF(fmt, ...)                                            \

void qmp_buttons_set_state(ButtonPressList *buttons, Error **errp)
{
    for (; buttons; buttons = buttons->next) {
        if (buttons->value) {
            DPRINTF("Set button %s to %s", buttons->value->identifier,
                    buttons->value->pushed_down ? "true" : "false");

            gchar *name = g_strdup_printf("button-%s",
                    buttons->value->identifier);
            Object *child = object_resolve_path_component(
                    OBJECT(current_machine), name);

            if (!child) {
                error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                        "GPIO '%s' doesn't exists", name);
                g_free(name);
                return;

            } else {
                g_free(name);
                qemu_set_irq(OBJECT_CHECK(struct IRQState, (child), TYPE_IRQ),
                        buttons->value->pushed_down);

            }
        }
    }
}
