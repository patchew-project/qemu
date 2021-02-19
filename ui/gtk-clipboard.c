/*
 * GTK UI -- clipboard support
 *
 * Copyright (C) 2021 Gerd Hoffmann <kraxel@redhat.com>
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
#include "qemu-common.h"

#include "ui/gtk.h"

static void gd_clipboard_notify(Notifier *notifier, void *data)
{
    GtkDisplayState *gd = container_of(notifier, GtkDisplayState, cbpeer.update);
    QemuClipboardInfo *info = data;
    QemuClipboardSelection s = info->selection;
    bool self_update = info->owner == &gd->cbpeer;

    if (info != gd->cbinfo[s]) {
        qemu_clipboard_info_put(gd->cbinfo[s]);
        gd->cbinfo[s] = qemu_clipboard_info_get(info);
        gd->cbpending[s] = 0;
        if (!self_update) {
            if (info->types[QEMU_CLIPBOARD_TYPE_TEXT].available) {
                qemu_clipboard_request(info, QEMU_CLIPBOARD_TYPE_TEXT);
            }
        }
        return;
    }

    if (self_update) {
        return;
    }

    if (info->types[QEMU_CLIPBOARD_TYPE_TEXT].available &&
        info->types[QEMU_CLIPBOARD_TYPE_TEXT].data) {
        gtk_clipboard_set_text(gd->gtkcb[s],
                               info->types[QEMU_CLIPBOARD_TYPE_TEXT].data,
                               info->types[QEMU_CLIPBOARD_TYPE_TEXT].size);
    }
}

static void gd_clipboard_request(QemuClipboardInfo *info,
                                 QemuClipboardType type)
{
    GtkDisplayState *gd = container_of(info->owner, GtkDisplayState, cbpeer);
    char *text;

    switch (type) {
    case QEMU_CLIPBOARD_TYPE_TEXT:
        text = gtk_clipboard_wait_for_text(gd->gtkcb[info->selection]);
        qemu_clipboard_set_data(&gd->cbpeer, info, type,
                                strlen(text), text, true);
        break;
    default:
        break;
    }
}

static QemuClipboardSelection gd_find_selection(GtkDisplayState *gd,
                                                GtkClipboard *clipboard)
{
    QemuClipboardSelection s;

    for (s = 0; s < QEMU_CLIPBOARD_SELECTION__COUNT; s++) {
        if (gd->gtkcb[s] == clipboard) {
            return s;
        }
    }
    return QEMU_CLIPBOARD_SELECTION_CLIPBOARD;
}

static void gd_owner_change(GtkClipboard *clipboard,
                            GdkEvent *event,
                            gpointer data)
{
    GtkDisplayState *gd = data;
    QemuClipboardSelection s = gd_find_selection(gd, clipboard);
    QemuClipboardInfo *info;

    info = qemu_clipboard_info_new(&gd->cbpeer, s);
    if (gtk_clipboard_wait_is_text_available(clipboard)) {
        info->types[QEMU_CLIPBOARD_TYPE_TEXT].available = true;
    }

    qemu_clipboard_update(info);
    qemu_clipboard_info_put(info);
}

void gd_clipboard_init(GtkDisplayState *gd)
{
    gd->cbpeer.name = "gtk";
    gd->cbpeer.update.notify = gd_clipboard_notify;
    gd->cbpeer.request = gd_clipboard_request;
    qemu_clipboard_peer_register(&gd->cbpeer);

    gd->gtkcb[QEMU_CLIPBOARD_SELECTION_CLIPBOARD] =
        gtk_clipboard_get(gdk_atom_intern("CLIPBOARD", FALSE));
    gd->gtkcb[QEMU_CLIPBOARD_SELECTION_PRIMARY] =
        gtk_clipboard_get(gdk_atom_intern("PRIMARY", FALSE));
    gd->gtkcb[QEMU_CLIPBOARD_SELECTION_SECONDARY] =
        gtk_clipboard_get(gdk_atom_intern("SECONDARY", FALSE));

    g_signal_connect(gd->gtkcb[QEMU_CLIPBOARD_SELECTION_CLIPBOARD],
                     "owner-change", G_CALLBACK(gd_owner_change), gd);
    g_signal_connect(gd->gtkcb[QEMU_CLIPBOARD_SELECTION_PRIMARY],
                     "owner-change", G_CALLBACK(gd_owner_change), gd);
    g_signal_connect(gd->gtkcb[QEMU_CLIPBOARD_SELECTION_SECONDARY],
                     "owner-change", G_CALLBACK(gd_owner_change), gd);
}
