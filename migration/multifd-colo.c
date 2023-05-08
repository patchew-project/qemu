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
#include "migration/colo.h"

#define MULTIFD_INTERNAL
#include "multifd-internal.h"

static int multifd_colo_recv_pages(MultiFDRecvParams *p, Error **errp)
{
    int ret = 0;

    /*
     * While we're still in precopy mode, we copy received pages to both guest
     * and cache. No need to set dirty bits, since guest and cache memory are
     * in sync.
     */
    if (migration_incoming_in_colo_state()) {
        colo_record_bitmap(p->block, p->normal, p->normal_num);
    }

    p->host = p->block->colo_cache;
    ret = multifd_recv_state->ops->recv_pages(p, errp);
    if (ret != 0) {
        p->host = p->block->host;
        return ret;
    }

    if (!migration_incoming_in_colo_state()) {
        for (int i = 0; i < p->normal_num; i++) {
            void *guest = p->block->host + p->normal[i];
            void *cache = p->host + p->normal[i];
            memcpy(guest, cache, p->page_size);
        }
    }

    p->host = p->block->host;
    return ret;
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
