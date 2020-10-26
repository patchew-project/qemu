/*
 * Convert between legacy and modern interfaces
 *
 * Copyright (C) 2020 Red Hat, Inc.
 *
 * Authors:
 *  Markus Armbruster <armbru@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/clone-visitor.h"
#include "qapi/qapi-visit-char.h"
#include "qemu/sockets.h"
#include "chardev/char.h"

/*
 * TODO Convert internal interfaces to ChardevOptions, replace this
 * function by one that flattens (const char *str, ChardevBackend
 * *backend) -> ChardevOptions.
 */
q_obj_chardev_add_arg *chardev_options_crumple(ChardevOptions *chr)
{
    q_obj_chardev_add_arg *arg;
    ChardevBackend *be;

    if (!chr) {
        return NULL;
    }

    arg = g_malloc(sizeof(*arg));
    arg->id = g_strdup(chr->id);
    arg->backend = be = g_malloc(sizeof(*be));

    switch (chr->backend) {
    case CHARDEV_BACKEND_TYPE_FILE:
        be->type = CHARDEV_BACKEND_KIND_FILE;
        be->u.file.data = QAPI_CLONE(ChardevFile, &chr->u.file);
        break;
    case CHARDEV_BACKEND_TYPE_SERIAL:
        be->type = CHARDEV_BACKEND_KIND_SERIAL;
        be->u.serial.data = QAPI_CLONE(ChardevHostdev, &chr->u.serial);
        break;
    case CHARDEV_BACKEND_TYPE_PARALLEL:
        be->type = CHARDEV_BACKEND_KIND_PARALLEL;
        be->u.parallel.data = QAPI_CLONE(ChardevHostdev, &chr->u.parallel);
        break;
    case CHARDEV_BACKEND_TYPE_PIPE:
        be->type = CHARDEV_BACKEND_KIND_PIPE;
        be->u.pipe.data = QAPI_CLONE(ChardevHostdev, &chr->u.pipe);
        break;
    case CHARDEV_BACKEND_TYPE_SOCKET:
        be->type = CHARDEV_BACKEND_KIND_SOCKET;
        /*
         * Clone with SocketAddress crumpled to SocketAddressLegacy.
         * All other members are in the base type.
         */
        be->u.socket.data = g_memdup(&chr->u.socket, sizeof(chr->u.socket));
        QAPI_CLONE_MEMBERS(ChardevSocketBase,
                           qapi_ChardevSocket_base(be->u.socket.data),
                           qapi_ChardevSocketFlat_base(&chr->u.socket));
        be->u.socket.data->addr = socket_address_crumple(chr->u.socket.addr);
        break;
    case CHARDEV_BACKEND_TYPE_UDP:
        be->type = CHARDEV_BACKEND_KIND_UDP;
        /*
         * Clone with SocketAddress crumpled to SocketAddressLegacy.
         * All other members in are the base type.
         */
        be->u.udp.data = g_memdup(&chr->u.udp, sizeof(chr->u.udp));
        QAPI_CLONE_MEMBERS(ChardevCommon,
                           qapi_ChardevUdp_base(be->u.udp.data),
                           qapi_ChardevUdpFlat_base(&chr->u.udp));
        be->u.udp.data->remote = socket_address_crumple(chr->u.udp.remote);
        be->u.udp.data->local = socket_address_crumple(chr->u.udp.local);
        break;
    case CHARDEV_BACKEND_TYPE_PTY:
        be->type = CHARDEV_BACKEND_KIND_PTY;
        be->u.pty.data = QAPI_CLONE(ChardevCommon, &chr->u.pty);
        break;
    case CHARDEV_BACKEND_TYPE_NULL:
        be->type = CHARDEV_BACKEND_KIND_NULL;
        be->u.null.data = QAPI_CLONE(ChardevCommon, &chr->u.null);
        break;
    case CHARDEV_BACKEND_TYPE_MUX:
        be->type = CHARDEV_BACKEND_KIND_MUX;
         be->u.mux.data = QAPI_CLONE(ChardevMux, &chr->u.mux);
        break;
    case CHARDEV_BACKEND_TYPE_MSMOUSE:
        be->type = CHARDEV_BACKEND_KIND_MSMOUSE;
        be->u.msmouse.data = QAPI_CLONE(ChardevCommon, &chr->u.msmouse);
        break;
    case CHARDEV_BACKEND_TYPE_WCTABLET:
        be->type = CHARDEV_BACKEND_KIND_WCTABLET;
        be->u.wctablet.data = QAPI_CLONE(ChardevCommon, &chr->u.wctablet);
        break;
    case CHARDEV_BACKEND_TYPE_BRAILLE:
        be->type = CHARDEV_BACKEND_KIND_BRAILLE;
        be->u.braille.data = QAPI_CLONE(ChardevCommon, &chr->u.braille);
        break;
    case CHARDEV_BACKEND_TYPE_TESTDEV:
        be->type = CHARDEV_BACKEND_KIND_TESTDEV;
        be->u.testdev.data = QAPI_CLONE(ChardevCommon, &chr->u.testdev);
        break;
    case CHARDEV_BACKEND_TYPE_STDIO:
        be->type = CHARDEV_BACKEND_KIND_STDIO;
        be->u.stdio.data = QAPI_CLONE(ChardevStdio, &chr->u.stdio);
        break;
    case CHARDEV_BACKEND_TYPE_CONSOLE:
        be->type = CHARDEV_BACKEND_KIND_CONSOLE;
        be->u.console.data = QAPI_CLONE(ChardevCommon, &chr->u.console);
        break;
#ifdef CONFIG_SPICE
    case CHARDEV_BACKEND_TYPE_SPICEVMC:
        be->type = CHARDEV_BACKEND_KIND_SPICEVMC;
        be->u.spicevmc.data = QAPI_CLONE(ChardevSpiceChannel,
                                         &chr->u.spicevmc);
        break;
    case CHARDEV_BACKEND_TYPE_SPICEPORT:
        be->type = CHARDEV_BACKEND_KIND_SPICEPORT;
        be->u.spiceport.data = QAPI_CLONE(ChardevSpicePort,
                                          &chr->u.spiceport);
        break;
#endif
    case CHARDEV_BACKEND_TYPE_VC:
        be->type = CHARDEV_BACKEND_KIND_VC;
        be->u.vc.data = QAPI_CLONE(ChardevVC, &chr->u.vc);
        break;
    case CHARDEV_BACKEND_TYPE_RINGBUF:
        be->type = CHARDEV_BACKEND_KIND_RINGBUF;
        be->u.ringbuf.data = QAPI_CLONE(ChardevRingbuf, &chr->u.ringbuf);
        break;
    default:
        abort();
    }

    return arg;
}
