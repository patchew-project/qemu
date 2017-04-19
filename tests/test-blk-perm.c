/*
 * Block permission tests
 *
 * Copyright Red Hat, Inc. 2017
 *
 * Authors:
 *  Fam Zheng <famz@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "sysemu/block-backend.h"

static void test_aio_context_success(void)
{
    BlockBackend *blk1 = blk_new(BLK_PERM_AIO_CONTEXT_CHANGE, BLK_PERM_ALL);
    BlockBackend *blk2 = blk_new(BLK_PERM_AIO_CONTEXT_CHANGE, BLK_PERM_ALL);
    BlockDriverState *bs = bdrv_open("null-co://", NULL, NULL, 0, &error_abort);

    blk_insert_bs(blk1, bs, &error_abort);
    blk_insert_bs(blk2, bs, &error_abort);

    blk_unref(blk1);
    blk_unref(blk2);
    bdrv_unref(bs);
}

static void test_aio_context_failure(void)
{
    Error *local_err = NULL;
    BlockBackend *blk1 = blk_new(BLK_PERM_AIO_CONTEXT_CHANGE,
                                 BLK_PERM_ALL & ~BLK_PERM_AIO_CONTEXT_CHANGE);
    BlockBackend *blk2 = blk_new(BLK_PERM_AIO_CONTEXT_CHANGE, BLK_PERM_ALL);
    BlockDriverState *bs = bdrv_open("null-co://", NULL, NULL, 0, &error_abort);

    blk_insert_bs(blk1, bs, &error_abort);
    blk_insert_bs(blk2, bs, &local_err);

    g_assert_nonnull(local_err);

    blk_unref(blk1);
    blk_unref(blk2);
    bdrv_unref(bs);
}

int main(int argc, char **argv)
{
    bdrv_init();
    qemu_init_main_loop(&error_abort);
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/block/perm/aio-context/success",
                    test_aio_context_success);
    g_test_add_func("/block/perm/aio-context/failure",
                    test_aio_context_failure);
    return g_test_run();
}
