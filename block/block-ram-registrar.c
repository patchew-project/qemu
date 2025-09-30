/*
 * BlockBackend RAM Registrar
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "system/block-backend.h"
#include "system/block-ram-registrar.h"
#include "system/ramblock.h"
#include "qapi/error.h"
#include "trace.h"

static void ram_block_added(RAMBlockNotifier *n, void *host, size_t size,
                            size_t max_size)
{
    BlockRAMRegistrar *r =
        container_of(n, BlockRAMRegistrar, ram_block_notifier);
    Error *err = NULL;

    if (!r->ok) {
        return; /* don't try again if we've already failed */
    }

    if (!blk_register_buf(r->blk, host, max_size, &err)) {
        error_report_err(err);
        blk_ram_registrar_destroy(r);
        return;
    }
}

static void ram_block_removed(RAMBlockNotifier *n, void *host, size_t size,
                              size_t max_size)
{
    BlockRAMRegistrar *r =
        container_of(n, BlockRAMRegistrar, ram_block_notifier);
    blk_unregister_buf(r->blk, host, max_size);
}

static void blk_attached(Notifier *n, void *data)
{
    BlockRAMRegistrar *r =
        container_of(n, BlockRAMRegistrar, blk_attach_notifier);
    BlockBackendAttachDetachArgs *args = data;
    BlockDriverState *bs = args->bs;
    Error *err = NULL;

    WITH_RCU_READ_LOCK_GUARD() {
        RAMBlock *rb;

        RAMBLOCK_FOREACH(rb) {
            ram_addr_t max_size = qemu_ram_get_max_length(rb);
            void *host = qemu_ram_get_host_addr(rb);

            if (!bdrv_register_buf(bs, host, max_size, &err)) {
                goto err;
            }
        }
    }

    return;

err:
    error_report_err(err);
    blk_ram_registrar_destroy(r);
}

static void blk_detached(Notifier *n, void *data)
{
    BlockRAMRegistrar *r =
        container_of(n, BlockRAMRegistrar, blk_attach_notifier);
    BlockBackendAttachDetachArgs *args = data;
    BlockDriverState *bs = args->bs;
    RAMBlock *rb;

    RCU_READ_LOCK_GUARD();

    RAMBLOCK_FOREACH(rb) {
        ram_addr_t max_size = qemu_ram_get_max_length(rb);
        void *host = qemu_ram_get_host_addr(rb);

        bdrv_unregister_buf(bs, host, max_size);
    }
}

void blk_ram_registrar_init(BlockRAMRegistrar *r, BlockBackend *blk)
{
    r->blk = blk;
    r->ram_block_notifier = (RAMBlockNotifier){
        .ram_block_added = ram_block_added,
        .ram_block_removed = ram_block_removed,

        /*
         * .ram_block_resized() is not necessary because we use the max_size
         * value that does not change across resize.
         */
    };
    r->blk_attach_notifier = (Notifier){
        .notify = blk_attached,
    };
    r->blk_detach_notifier = (Notifier){
        .notify = blk_detached,
    };
    r->ok = true;

    ram_block_notifier_add(&r->ram_block_notifier);
    blk_add_attach_notifier(blk, &r->blk_attach_notifier);
    blk_add_detach_notifier(blk, &r->blk_detach_notifier);
}

void blk_ram_registrar_destroy(BlockRAMRegistrar *r)
{
    if (r->ok) {
        notifier_remove(&r->blk_detach_notifier);
        notifier_remove(&r->blk_attach_notifier);
        ram_block_notifier_remove(&r->ram_block_notifier);
        r->ok = false;
    }
}
