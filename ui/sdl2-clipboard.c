/*
 * SDL UI -- clipboard support (improved async version)
 *
 * Copyright (C) 2023 Kamay Xutax <admin@xutaxkamay.com>
 * Copyright (C) 2025 startergo <startergo@protonmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "ui/console.h"
#include "ui/clipboard.h"
#include "ui/sdl2.h"
#include "qemu/log.h"

#ifdef CONFIG_SDL_CLIPBOARD

/* Track pending clipboard requests to handle async data */
typedef struct {
    struct sdl2_console *scon;
    QemuClipboardInfo *info;
    QemuClipboardType type;
} SDLClipboardRequest;

static SDLClipboardRequest *pending_request = NULL;

static void sdl2_clipboard_clear_pending(void)
{
    if (pending_request) {
        if (pending_request->info) {
            qemu_clipboard_info_unref(pending_request->info);
        }
        g_free(pending_request);
        pending_request = NULL;
    }
}

static void sdl2_clipboard_notify(Notifier *notifier, void *data)
{
    QemuClipboardNotify *notify = data;
    struct sdl2_console *scon =
        container_of(notifier, struct sdl2_console, cbpeer.notifier);
    bool self_update = notify->info->owner == &scon->cbpeer;
    const char *text_data;
    size_t text_size;

    switch (notify->type) {
    case QEMU_CLIPBOARD_UPDATE_INFO:
        {
            /* Skip self-updates to avoid clipboard manager conflicts */
            if (self_update) {
                return;
            }

            if (!notify->info->types[QEMU_CLIPBOARD_TYPE_TEXT].available) {
                return;
            }

            /* Check if this is completion of our pending request */
            if (pending_request && pending_request->info == notify->info && 
                pending_request->type == QEMU_CLIPBOARD_TYPE_TEXT) {
                sdl2_clipboard_clear_pending();
            }

            /* Check if data is available, request asynchronously if not */
            if (!notify->info->types[QEMU_CLIPBOARD_TYPE_TEXT].data) {
                if (!pending_request) {
                    pending_request = g_new0(SDLClipboardRequest, 1);
                    pending_request->scon = scon;
                    pending_request->info = qemu_clipboard_info_ref(notify->info);
                    pending_request->type = QEMU_CLIPBOARD_TYPE_TEXT;
                    qemu_clipboard_request(notify->info, QEMU_CLIPBOARD_TYPE_TEXT);
                }
                return;
            }

            /* Process available data */
            text_size = notify->info->types[QEMU_CLIPBOARD_TYPE_TEXT].size;
            if (text_size == 0) {
                return;
            }

            text_data = (const char *)notify->info->types[QEMU_CLIPBOARD_TYPE_TEXT].data;
            
            /* Ensure null termination for SDL clipboard */
            g_autofree char *text = g_strndup(text_data, text_size);
            if (text && text[0] != '\0') {
                SDL_SetClipboardText(text);
            } else if (!text) {
                qemu_log_mask(LOG_GUEST_ERROR, 
                              "SDL clipboard: Failed to allocate memory for clipboard text\n");
            }
            break;
        }
    case QEMU_CLIPBOARD_RESET_SERIAL:
        sdl2_clipboard_clear_pending();
        break;
    }
}

static void sdl2_clipboard_request(QemuClipboardInfo *info,
                                   QemuClipboardType type)
{
    g_autofree char *text = NULL;

    if (type != QEMU_CLIPBOARD_TYPE_TEXT) {
        return;
    }

    text = SDL_GetClipboardText();
    if (!text) {
        qemu_log_mask(LOG_GUEST_ERROR, 
                      "SDL clipboard: Failed to get clipboard text: %s\n", 
                      SDL_GetError());
        return;
    }

    qemu_clipboard_set_data(info->owner, info, type,
                            strlen(text), text, true);
}

void sdl2_clipboard_init(struct sdl2_console *scon)
{
    scon->cbpeer.name = "sdl2-clipboard";
    scon->cbpeer.notifier.notify = sdl2_clipboard_notify;
    scon->cbpeer.request = sdl2_clipboard_request;

    qemu_clipboard_peer_register(&scon->cbpeer);
}

void sdl2_clipboard_handle_request(struct sdl2_console *scon)
{
    g_autofree char *text = NULL;
    QemuClipboardInfo *info;

    text = SDL_GetClipboardText();
    if (!text) {
        qemu_log_mask(LOG_GUEST_ERROR, 
                      "SDL clipboard: Failed to get clipboard text: %s\n", 
                      SDL_GetError());
        return;
    }

    if (text[0] == '\0') {
        return; /* Ignore empty clipboard */
    }

    info = qemu_clipboard_info_new(&scon->cbpeer, QEMU_CLIPBOARD_SELECTION_CLIPBOARD);
    qemu_clipboard_set_data(&scon->cbpeer, info, QEMU_CLIPBOARD_TYPE_TEXT,
                            strlen(text), text, true);
    qemu_clipboard_info_unref(info);
}

#endif /* CONFIG_SDL_CLIPBOARD */
