/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * multifd colo implementation
 *
 * Copyright (c) Lukas Straub <lukasstraub2@web.de>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "exec/target_page.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "ram.h"
#include "multifd.h"
#include "options.h"
#include "io/channel-socket.h"
#include "migration/colo.h"
#include "multifd-colo.h"
#include "system/ramblock.h"

void multifd_colo_prepare_recv(MultiFDRecvParams *p)
{
    if (!migrate_colo()) {
        return;
    }

    assert(p->block->colo_cache);

    /*
     * While we're still in precopy state (not yet in colo state), we copy
     * received pages to both guest and cache. No need to set dirty bits,
     * since guest and cache memory are in sync.
     */
    if (migration_incoming_in_colo_state()) {
        colo_record_bitmap(p->block, p->normal, p->normal_num);
    }
    p->host = p->block->colo_cache;
}

void multifd_colo_process_recv(MultiFDRecvParams *p)
{
    if (!migrate_colo()) {
        return;
    }

    if (!migration_incoming_in_colo_state()) {
        for (int i = 0; i < p->normal_num; i++) {
            void *guest = p->block->host + p->normal[i];
            void *cache = p->host + p->normal[i];
            memcpy(guest, cache, multifd_ram_page_size());
        }
    }
    p->host = p->block->host;
}
