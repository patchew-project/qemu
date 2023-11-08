/*
 * SDL UI -- clipboard support
 *
 * Copyright (C) 2023 Kamay Xutax <admin@xutaxkamay.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "ui/sdl2.h"

static void sdl2_clipboard_update(struct sdl2_console *scon,
                                  QemuClipboardInfo *info)
{
    bool self_update = info->owner == &scon->cbpeer;
    char *text;
    size_t text_size;

    /*
     * In case of a self update,
     * set again the text in SDL
     *
     * This is a workaround for hosts that have clipboard history
     * or when they're copying again something,
     * so that SDL can accept a new request from the host
     * and make a new SDL_CLIPBOARDUPDATE event
     */

    if (self_update) {
        text = SDL_GetClipboardText();
        SDL_SetClipboardText(text);
        SDL_free(text);
        return;
    }

    if (!info->types[QEMU_CLIPBOARD_TYPE_TEXT].available) {
        return;
    }

    info = qemu_clipboard_info_ref(info);
    qemu_clipboard_request(info, QEMU_CLIPBOARD_TYPE_TEXT);

    while (info == qemu_clipboard_info(info->selection) &&
           info->types[QEMU_CLIPBOARD_TYPE_TEXT].available &&
           info->types[QEMU_CLIPBOARD_TYPE_TEXT].data == NULL) {
        main_loop_wait(false);
    }

    /* clipboard info changed while waiting for data */
    if (info != qemu_clipboard_info(info->selection)) {
        qemu_clipboard_info_unref(info);
        return;
    }

    /* text is not null terminated in cb info, so we need to copy it */
    text_size = info->types[QEMU_CLIPBOARD_TYPE_TEXT].size;

    if (!text_size) {
        qemu_clipboard_info_unref(info);
        return;
    }

    text = malloc(text_size + 1);

    if (!text) {
        qemu_clipboard_info_unref(info);
        return;
    }

    text[text_size] = 0;
    memcpy(text, info->types[QEMU_CLIPBOARD_TYPE_TEXT].data, text_size);
    /* unref as soon we copied the text */
    qemu_clipboard_info_unref(info);
    SDL_SetClipboardText(text);

    free(text);
}

static void sdl2_clipboard_notify(Notifier *notifier,
                                  void *data)
{
    QemuClipboardNotify *notify = data;
    struct sdl2_console *scon =
        container_of(notifier, struct sdl2_console, cbpeer.notifier);

    switch (notify->type) {
    case QEMU_CLIPBOARD_UPDATE_INFO:
        sdl2_clipboard_update(scon, notify->info);
        break;
    case QEMU_CLIPBOARD_RESET_SERIAL:
        break;
    }
}

static void sdl2_clipboard_request(QemuClipboardInfo *info,
                                   QemuClipboardType type)
{
    struct sdl2_console *scon =
        container_of(info->owner, struct sdl2_console, cbpeer);
    char *text;

    switch (type) {
    case QEMU_CLIPBOARD_TYPE_TEXT:
        if (!SDL_HasClipboardText()) {
            return;
        }

        text = SDL_GetClipboardText();
        qemu_clipboard_set_data(&scon->cbpeer, info, type,
                                strlen(text), text, true);

        SDL_free(text);
        break;
    default:
        return;
    }
}

void sdl2_clipboard_handle_request(struct sdl2_console *scon)
{
    g_autoptr(QemuClipboardInfo) info =
        qemu_clipboard_info_new(&scon->cbpeer,
                                QEMU_CLIPBOARD_SELECTION_CLIPBOARD);

    sdl2_clipboard_request(info, QEMU_CLIPBOARD_TYPE_TEXT);
}

void sdl2_clipboard_init(struct sdl2_console *scon)
{
    scon->cbpeer.name = "sdl2";
    scon->cbpeer.notifier.notify = sdl2_clipboard_notify;
    /* requests will be handled from the SDL event loop */
    qemu_clipboard_peer_register(&scon->cbpeer);
}
