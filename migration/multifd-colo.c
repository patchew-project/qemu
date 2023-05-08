/*
 * multifd colo implementation
 *
 * Copyright (c) Lukas Straub <lukasstraub2@web.de>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "exec/target_page.h"
#include "exec/ramblock.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "ram.h"
#include "multifd.h"
#include "io/channel-socket.h"

#define MULTIFD_INTERNAL
#include "multifd-internal.h"

static int multifd_colo_recv_pages(MultiFDRecvParams *p, Error **errp)
{
    return multifd_recv_state->ops->recv_pages(p, errp);
}

int multifd_colo_load_setup(Error **errp)
{
    int ret;

    ret = _multifd_load_setup(errp);
    if (ret) {
        return ret;
    }

    multifd_recv_state->recv_pages = multifd_colo_recv_pages;

    return 0;
}
