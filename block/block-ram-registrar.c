/*
 * BlockBackend RAM Registrar
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "sysemu/block-backend.h"
#include "sysemu/block-ram-registrar.h"

static void ram_block_added(RAMBlockNotifier *n, void *host, size_t size,
                            size_t max_size)
{
    BlockRAMRegistrar *r = container_of(n, BlockRAMRegistrar, notifier);
    blk_register_buf(r->blk, host, max_size);
}

static void ram_block_removed(RAMBlockNotifier *n, void *host, size_t size,
                              size_t max_size)
{
    BlockRAMRegistrar *r = container_of(n, BlockRAMRegistrar, notifier);
    blk_unregister_buf(r->blk, host, max_size);
}

void blk_ram_registrar_init(BlockRAMRegistrar *r, BlockBackend *blk)
{
    r->blk = blk;
    r->notifier = (RAMBlockNotifier){
        .ram_block_added = ram_block_added,
        .ram_block_removed = ram_block_removed,
    };

    ram_block_notifier_add(&r->notifier);
}

void blk_ram_registrar_destroy(BlockRAMRegistrar *r)
{
    ram_block_notifier_remove(&r->notifier);
}
