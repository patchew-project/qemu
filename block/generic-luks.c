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
#include "block/block-io.h"
#include "block/crypto.h"
#include "block/qdict.h"
#include "crypto/block.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qemu/option.h"

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

static int gluks_read_func(QCryptoBlock *block,
                           size_t offset,
                           uint8_t *buf,
                           size_t buflen,
                           void *opaque,
                           Error **errp)
{

    BlockDriverState *bs = opaque;
    BDRVGLUKSState *s = bs->opaque;
    ssize_t ret;

    GLOBAL_STATE_CODE();
    GRAPH_RDLOCK_GUARD_MAINLOOP();

    ret = bdrv_pread(s->header, offset, buflen, buf, 0);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not read generic luks header");
        return ret;
    }
    return 0;
}

static int gluks_open(BlockDriverState *bs, QDict *options, int flags,
                      Error **errp)
{
    BDRVGLUKSState *s = bs->opaque;
    QemuOpts *opts = NULL;
    QCryptoBlockOpenOptions *open_opts = NULL;
    QDict *cryptoopts = NULL;
    unsigned int cflags = 0;
    int ret;

    GLOBAL_STATE_CODE();

    if (!bdrv_open_child(NULL, options, "file", bs, &child_of_bds,
                         (BDRV_CHILD_DATA | BDRV_CHILD_PRIMARY), false, errp)) {
        return -EINVAL;
    }
    s->header = bdrv_open_child(NULL, options, "header", bs,
                                &child_of_bds, BDRV_CHILD_METADATA, false,
                                errp);
    if (!s->header) {
        return -EINVAL;
    }

    GRAPH_RDLOCK_GUARD_MAINLOOP();

    opts = qemu_opts_create(&block_crypto_runtime_opts_luks,
        NULL, 0, &error_abort);
    if (!qemu_opts_absorb_qdict(opts, options, errp)) {
        ret = -EINVAL;
        goto cleanup;
    }

    cryptoopts = qemu_opts_to_qdict(opts, NULL);
    qdict_put_str(cryptoopts, "format",
        QCryptoBlockFormat_str(Q_CRYPTO_BLOCK_FORMAT_GLUKS));

    open_opts = block_crypto_open_opts_init(cryptoopts, errp);
    if (!open_opts) {
        goto cleanup;
    }

    s->crypto.block = qcrypto_block_open(open_opts, NULL,
                                         gluks_read_func,
                                         bs,
                                         cflags,
                                         1,
                                         errp);
    if (!s->crypto.block) {
        ret = -EIO;
        goto cleanup;
    }

    s->header_size = qcrypto_block_get_payload_offset(s->crypto.block);
    qcrypto_block_set_payload_offset(s->crypto.block, 0);

    ret = 0;
 cleanup:
    qobject_unref(cryptoopts);
    qapi_free_QCryptoBlockOpenOptions(open_opts);
    return ret;
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
    if (role & BDRV_CHILD_METADATA) {
        /* assign read permission only */
        perm |= BLK_PERM_CONSISTENT_READ;
        /* share all permissions */
        shared |= BLK_PERM_ALL;

        *nperm = perm;
        *nshared = shared;
        return;
    }

    bdrv_default_perms(bs, c, role, reopen_queue, perm, shared, nperm, nshared);
}

static int64_t coroutine_fn GRAPH_RDLOCK
gluks_co_getlength(BlockDriverState *bs)
{
    return bdrv_co_getlength(bs->file->bs);
}

static BlockDriver bdrv_generic_luks = {
    .format_name            = "gluks",
    .instance_size          = sizeof(BDRVGLUKSState),
    .bdrv_open              = gluks_open,
    .bdrv_co_create_opts    = gluks_co_create_opts,
    .bdrv_child_perm        = gluks_child_perms,
    .bdrv_co_getlength      = gluks_co_getlength,

    .bdrv_close             = block_crypto_close,
    .bdrv_co_preadv         = block_crypto_co_preadv,
    .bdrv_co_pwritev        = block_crypto_co_pwritev,
    .create_opts            = &gluks_create_opts_luks,
    .amend_opts             = &block_crypto_amend_opts_luks,
    .is_format              = false,
};

static void block_generic_luks_init(void)
{
    bdrv_register(&bdrv_generic_luks);
}

block_init(block_generic_luks_init);
