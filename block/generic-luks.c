/*
 * QEMU block driver for the generic luks encryption
 *
 * Copyright (c) 2024 SmartX Inc
 *
 * Author: Hyman Huang <yong.huang@smartx.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"

#include "block/block_int.h"
#include "block/crypto.h"
#include "crypto/block.h"

#include "generic-luks.h"

/* BDRVGLUKSState holds the state of one generic LUKS instance */
typedef struct BDRVGLUKSState {
    BlockCrypto crypto;
    BdrvChild *header;      /* LUKS header node */
    uint64_t header_size;   /* In bytes */
} BDRVGLUKSState;

static QemuOptsList gluks_create_opts_luks = {
    .name = "crypto",
    .head = QTAILQ_HEAD_INITIALIZER(gluks_create_opts_luks.head),
    .desc = {
        BLOCK_CRYPTO_OPT_DEF_LUKS_KEY_SECRET(""),
        BLOCK_CRYPTO_OPT_DEF_LUKS_CIPHER_ALG(""),
        BLOCK_CRYPTO_OPT_DEF_LUKS_CIPHER_MODE(""),
        BLOCK_CRYPTO_OPT_DEF_LUKS_IVGEN_ALG(""),
        BLOCK_CRYPTO_OPT_DEF_LUKS_IVGEN_HASH_ALG(""),
        BLOCK_CRYPTO_OPT_DEF_LUKS_HASH_ALG(""),
        BLOCK_CRYPTO_OPT_DEF_LUKS_ITER_TIME(""),
        { /* end of list */ }
    },
};

static int gluks_open(BlockDriverState *bs, QDict *options, int flags,
                      Error **errp)
{
    return 0;
}

static int coroutine_fn GRAPH_UNLOCKED
gluks_co_create_opts(BlockDriver *drv, const char *filename,
                     QemuOpts *opts, Error **errp)
{
    return 0;
}

static void
gluks_child_perms(BlockDriverState *bs, BdrvChild *c,
                  const BdrvChildRole role,
                  BlockReopenQueue *reopen_queue,
                  uint64_t perm, uint64_t shared,
                  uint64_t *nperm, uint64_t *nshared)
{

}

static int64_t coroutine_fn GRAPH_RDLOCK
gluks_co_getlength(BlockDriverState *bs)
{
    return 0;
}

static BlockDriver bdrv_generic_luks = {
    .format_name            = "gluks",
    .instance_size          = sizeof(BDRVGLUKSState),
    .bdrv_open              = gluks_open,
    .bdrv_co_create_opts    = gluks_co_create_opts,
    .bdrv_child_perm        = gluks_child_perms,
    .bdrv_co_getlength      = gluks_co_getlength,

    .create_opts            = &gluks_create_opts_luks,
    .amend_opts             = &block_crypto_amend_opts_luks,
};

static void block_generic_luks_init(void)
{
    bdrv_register(&bdrv_generic_luks);
}

block_init(block_generic_luks_init);
