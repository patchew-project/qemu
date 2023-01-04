/*
 * BlockBackend RAM Registrar
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "sysemu/block-backend.h"
#include "sysemu/block-ram-registrar.h"
#include "qapi/error.h"

static void ram_block_added(RAMBlockNotifier *n, void *host, size_t size,
                            size_t max_size)
{
    BlockRAMRegistrar *r = container_of(n, BlockRAMRegistrar, notifier);
    Error *err = NULL;

    if (!r->ok) {
        return; /* don't try again if we've already failed */
    }

    if (!blk_register_buf(r->blk, host, max_size, &err)) {
        error_report_err(err);
        ram_block_notifier_remove(&r->notifier);
        r->ok = false;
    }
}

static void ram_block_removed(RAMBlockNotifier *n, void *host, size_t size,
                              size_t max_size)
{
    BlockRAMRegistrar *r = container_of(n, BlockRAMRegistrar, notifier);
    blk_unregister_buf(r->blk, host, max_size);
}

static void remove_ram_block_notifier(BlockRAMRegistrar *r)
{
    if (r->ok) {
        ram_block_notifier_remove(&r->notifier);
    }
}

static void add_ram_block_notifier(BlockRAMRegistrar *r)
{
    r->ok = true; /* reset flag in case it was false */
    ram_block_notifier_add(&r->notifier);
}

static void graph_change_pre_detach(BlockBackend *blk, void *opaque)
{
    BlockRAMRegistrar *r = opaque;

    /* Unregisters all buffers from all BDSes */
    remove_ram_block_notifier(r);
}

static void graph_change_post_attach(BlockBackend *blk, void *opaque)
{
    BlockRAMRegistrar *r = opaque;

    /* Re-registers all buffers with all BDSes */
    add_ram_block_notifier(r);
}

void blk_ram_registrar_init(BlockRAMRegistrar *r, BlockBackend *blk)
{
    r->blk = blk;
    r->notifier = (RAMBlockNotifier){
        .ram_block_added = ram_block_added,
        .ram_block_removed = ram_block_removed,

        /*
         * .ram_block_resized() is not necessary because we use the max_size
         * value that does not change across resize.
         */
    };

    blk_add_graph_change_notifier(blk, graph_change_pre_detach,
                                  graph_change_post_attach, r);
    add_ram_block_notifier(r);
}

void blk_ram_registrar_destroy(BlockRAMRegistrar *r)
{
    remove_ram_block_notifier(r);
    blk_remove_graph_change_notifier(r->blk, graph_change_pre_detach,
                                     graph_change_post_attach, r);
}
