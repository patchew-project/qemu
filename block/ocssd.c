/*
 * BlockDriver implementation for "ocssd" format driver
 *
 * Based on the "raw" format driver (raw-format.c).
 *
 * Copyright (c) 2019 CNEX Labs, Inc.
 * Copyright (C) 2010-2016 Red Hat, Inc.
 * Copyright (C) 2010, Blue Swirl <blauwirbel@gmail.com>
 * Copyright (C) 2009, Anthony Liguori <aliguori@us.ibm.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/option.h"
#include "sysemu/block-backend.h"
#include "qapi/error.h"

#include "hw/block/nvme/ocssd.h"

typedef struct BDRVOcssdState {
    OcssdFormatHeader hdr;
    OcssdIdentity *namespaces;
    uint64_t size, real_size;
} BDRVOcssdState;

static QemuOptsList ocssd_create_opts = {
    .name = "ocssd-create-opts",
    .head = QTAILQ_HEAD_INITIALIZER(ocssd_create_opts.head),
    .desc = {
        {
            .name = "num_grp",
            .type = QEMU_OPT_NUMBER,
            .help = "number of groups (default: 2)"
        },
        {
            .name = "num_pu",
            .type = QEMU_OPT_NUMBER,
            .help = "number of parallel units per group (default: 4)"
        },
        {
            .name = "num_chk",
            .type = QEMU_OPT_NUMBER,
            .help = "number of chunks per parallel unit (defaut: 60)"
        },
        {
            .name = "num_sec",
            .type = QEMU_OPT_NUMBER,
            .help = "number of sectors per chunk (default: 4096)"
        },
        {
            .name = "sec_size",
            .type = QEMU_OPT_SIZE,
            .help = "sector size (default: 4096)"
        },
        {
            .name = "md_size",
            .type = QEMU_OPT_SIZE,
            .help = "metadata size (default: 16)"
        },
        {
            .name = "num_ns",
            .type = QEMU_OPT_NUMBER,
            .help = "number of namespaces (default: 1)",
        },
        {
            .name = "mccap",
            .type = QEMU_OPT_NUMBER,
            .help = "media and controller capabilities (default: 0x1)",
        },
        {
            .name = "wit",
            .type = QEMU_OPT_NUMBER,
            .help = "wear-level index delta threshold (default: 10)",
        },
        {
            .name = "ws_min",
            .type = QEMU_OPT_NUMBER,
            .help = "minimum write size (default: 4)",
        },
        {
            .name = "ws_opt",
            .type = QEMU_OPT_NUMBER,
            .help = "optimal write size (default: 8)",
        },
        {
            .name = "mw_cunits",
            .type = QEMU_OPT_NUMBER,
            .help = "cache minimum write size units (default: 24)",
        },
        {
            .name = "pe_cycles",
            .type = QEMU_OPT_NUMBER,
            .help = "program/erase cycles per chunk (default: 1000)",
        },
        { /* end of list */ }
    }
};

static int ocssd_reopen_prepare(BDRVReopenState *reopen_state,
    BlockReopenQueue *queue, Error **errp)
{
    assert(reopen_state != NULL);
    assert(reopen_state->bs != NULL);
    return 0;
}

static int coroutine_fn ocssd_co_preadv(BlockDriverState *bs, uint64_t offset,
    uint64_t bytes, QEMUIOVector *qiov, int flags)
{
    BDRVOcssdState *s = bs->opaque;

    BLKDBG_EVENT(bs->file, BLKDBG_READ_AIO);

    /*
     * Return predefined (deterministic) data for reads that are out of bounds
     * of the real physical size of the device. See ocssd_getlength for why
     * reads might be issued out of bounds.
     */
    if (offset > s->real_size || s->real_size - offset < bytes) {
        return qemu_iovec_memset(qiov, 0, 0x0, bytes);
    }

    return bdrv_co_preadv(bs->file, offset, bytes, qiov, flags);
}

static int coroutine_fn ocssd_co_pwritev(BlockDriverState *bs, uint64_t offset,
    uint64_t bytes, QEMUIOVector *qiov, int flags)
{
    void *buf = NULL;
    BlockDriver *drv;
    QEMUIOVector local_qiov;
    int ret;

    if (bs->probed && offset < BLOCK_PROBE_BUF_SIZE && bytes) {
        /*
         * Handling partial writes would be a pain - so we just
         * require that guests have 512-byte request alignment if
         * probing occurred
         */
        QEMU_BUILD_BUG_ON(BLOCK_PROBE_BUF_SIZE != 512);
        QEMU_BUILD_BUG_ON(BDRV_SECTOR_SIZE != 512);
        assert(offset == 0 && bytes >= BLOCK_PROBE_BUF_SIZE);

        buf = qemu_try_blockalign(bs->file->bs, 512);
        if (!buf) {
            ret = -ENOMEM;
            goto fail;
        }

        ret = qemu_iovec_to_buf(qiov, 0, buf, 512);
        if (ret != 512) {
            ret = -EINVAL;
            goto fail;
        }

        drv = bdrv_probe_all(buf, 512, NULL);
        if (drv != bs->drv) {
            ret = -EPERM;
            goto fail;
        }

        /*
         * Use the checked buffer, a malicious guest might be overwriting its
         * original buffer in the background.
         */
        qemu_iovec_init(&local_qiov, qiov->niov + 1);
        qemu_iovec_add(&local_qiov, buf, 512);
        qemu_iovec_concat(&local_qiov, qiov, 512, qiov->size - 512);
        qiov = &local_qiov;
    }

    BLKDBG_EVENT(bs->file, BLKDBG_WRITE_AIO);
    ret = bdrv_co_pwritev(bs->file, offset, bytes, qiov, flags);

fail:
    if (qiov == &local_qiov) {
        qemu_iovec_destroy(&local_qiov);
    }
    qemu_vfree(buf);
    return ret;
}

static int coroutine_fn ocssd_co_block_status(BlockDriverState *bs,
    bool want_zero, int64_t offset, int64_t bytes, int64_t *pnum, int64_t *map,
    BlockDriverState **file)
{
    *pnum = bytes;
    *file = bs->file->bs;
    *map = offset;
    return BDRV_BLOCK_RAW | BDRV_BLOCK_OFFSET_VALID;
}

static int coroutine_fn ocssd_co_pwrite_zeroes(BlockDriverState *bs,
    int64_t offset, int bytes, BdrvRequestFlags flags)
{
    return bdrv_co_pwrite_zeroes(bs->file, offset, bytes, flags);
}

static int coroutine_fn ocssd_co_pdiscard(BlockDriverState *bs, int64_t offset,
    int bytes)
{
    return bdrv_co_pdiscard(bs->file, offset, bytes);
}

static int64_t ocssd_getlength(BlockDriverState *bs)
{
    /*
     * Return the size of the full physical address space. It may be larger
     * than the real size due to the LBA address format. For reads, predefined
     * (deterministic) data is returned for addresses that are out of bounds of
     * the real size (see ocssd_co_preadv).
     */

    BDRVOcssdState *s = bs->opaque;
    return s->size;
}

static BlockMeasureInfo *ocssd_measure(QemuOpts *opts, BlockDriverState *in_bs,
    Error **errp)
{
    BlockMeasureInfo *info;
    int64_t required;

    if (in_bs) {
        required = bdrv_getlength(in_bs);
        if (required < 0) {
            error_setg_errno(errp, -required, "Unable to get image size");
            return NULL;
        }
    } else {
        required = ROUND_UP(qemu_opt_get_size_del(opts, BLOCK_OPT_SIZE, 0),
            BDRV_SECTOR_SIZE);
    }

    info = g_new(BlockMeasureInfo, 1);
    info->required = required;

    /* unallocated sectors count towards the file size in ocssd images */
    info->fully_allocated = info->required;
    return info;
}

static int ocssd_get_info(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    return bdrv_get_info(bs->file->bs, bdi);
}

static ImageInfoSpecificOcssdNS *ocssd_get_namespace_info(
    OcssdIdentity *ns)
{
    ImageInfoSpecificOcssdNS *info = g_new0(ImageInfoSpecificOcssdNS, 1);

    *info = (ImageInfoSpecificOcssdNS) {
        .num_grp = ns->geo.num_grp,
        .num_pu = ns->geo.num_pu,
        .num_chk = ns->geo.num_chk,
        .num_sec = ns->geo.clba,
    };

    return info;
}

static ImageInfoSpecific *ocssd_get_specific_info(BlockDriverState *bs,
    Error **errp)
{
    BDRVOcssdState *s = bs->opaque;
    ImageInfoSpecific *spec_info;
    ImageInfoSpecificOcssdNSList **next;

    spec_info = g_new0(ImageInfoSpecific, 1);
    *spec_info = (ImageInfoSpecific){
        .type  = IMAGE_INFO_SPECIFIC_KIND_OCSSD,
        .u.ocssd.data = g_new0(ImageInfoSpecificOcssd, 1),
    };

    *spec_info->u.ocssd.data = (ImageInfoSpecificOcssd){
        .num_ns = s->hdr.num_ns,
        .sector_size = s->hdr.sector_size,
        .metadata_size = s->hdr.md_size,
    };

    next = &spec_info->u.ocssd.data->namespaces;
    for (int i = 0; i < s->hdr.num_ns; i++) {
        *next = g_new0(ImageInfoSpecificOcssdNSList, 1);
        (*next)->value = ocssd_get_namespace_info(&s->namespaces[i]);
        (*next)->next = NULL;
        next = &(*next)->next;
    }

    return spec_info;
}

static void ocssd_refresh_limits(BlockDriverState *bs, Error **errp)
{
    if (bs->probed) {
        /*
         * To make it easier to protect the first sector, any probed
         * image is restricted to read-modify-write on sub-sector
         * operations.
         */
        bs->bl.request_alignment = BDRV_SECTOR_SIZE;
    }
}

static int coroutine_fn ocssd_co_truncate(BlockDriverState *bs, int64_t offset,
    PreallocMode prealloc, Error **errp)
{
    return bdrv_co_truncate(bs->file, offset, prealloc, errp);
}

static void ocssd_eject(BlockDriverState *bs, bool eject_flag)
{
    bdrv_eject(bs->file->bs, eject_flag);
}

static void ocssd_lock_medium(BlockDriverState *bs, bool locked)
{
    bdrv_lock_medium(bs->file->bs, locked);
}

static int ocssd_co_ioctl(BlockDriverState *bs, unsigned long int req,
    void *buf)
{
    return bdrv_co_ioctl(bs->file->bs, req, buf);
}

static int ocssd_has_zero_init(BlockDriverState *bs)
{
    return bdrv_has_zero_init(bs->file->bs);
}

static int coroutine_fn ocssd_co_create_opts(const char *filename,
    QemuOpts *opts, Error **errp)
{
    BlockBackend *blk = NULL;
    BlockDriverState *bs = NULL;
    Error *local_err = NULL;
    OcssdFormatHeader hdr;
    OcssdIdentity id;
    OcssdIdLBAF lbaf;
    OcssdAddrF addrf;
    OcssdChunkDescriptor *chk;
    OcssdChunkAcctDescriptor *acct;
    uint8_t wit;
    uint16_t num_grp, num_pu;
    uint32_t num_chk, num_sec, mccap, ws_min, ws_opt, mw_cunits, pe_cycles;
    uint64_t sec_size, md_size, num_ns, logpage_size, acct_size;
    uint64_t chks_total, secs_total, usable_size, ns_size, size, offset;
    int ret;

    num_grp = qemu_opt_get_number(opts, "num_grp", 2);
    num_pu = qemu_opt_get_number(opts, "num_pu", 8);
    num_chk = qemu_opt_get_number(opts, "num_chk", 60);
    num_sec = qemu_opt_get_number(opts, "num_sec", 4096);
    num_ns = qemu_opt_get_number(opts, "num_ns", 1);
    mccap = qemu_opt_get_number(opts, "mccap", 0x1);
    wit = qemu_opt_get_number(opts, "wit", 10);
    ws_min = qemu_opt_get_number(opts, "ws_min", 4);
    ws_opt = qemu_opt_get_number(opts, "ws_opt", 8);
    mw_cunits = qemu_opt_get_number(opts, "mw_cunits", 24);
    pe_cycles = qemu_opt_get_number(opts, "pe_cycles", 1000);

    sec_size = qemu_opt_get_size(opts, "sec_size", 4096);
    md_size = qemu_opt_get_size(opts, "md_size", 16);

    chks_total = num_grp * num_pu * num_chk;
    logpage_size = QEMU_ALIGN_UP(chks_total * sizeof(OcssdChunkDescriptor),
        sec_size);
    acct_size = QEMU_ALIGN_UP(chks_total * sizeof(OcssdChunkAcctDescriptor),
        sec_size);

    secs_total = chks_total * num_sec;

    /*
     * The ocssd format begins with a 4k format header. Namespaces are laid out
     * contiguously in sections after the header.
     *
     * A namespace section consists of a 4k OcssdIdentify block, an accounting
     * region and a chunk info region. The accounting and chunk info regions
     * are of variable size (in multiples of the sector size). Then comes a
     * data section that contains a region dedicated to data dn a region for
     * metadata afterwards.
     *
     *     [Format header          ] 4096 bytes
     *     [OCSSD identity/geometry] 4096 bytes
     *     [Accounting             ] sector_size * n
     *     [Chunk info             ] sector_size * m
     *     [Namespace data         ] sector_size * k
     *     [Namespace meta data    ] md_size * k
     *
     * , where 'n' is the number of sectors required to hold accounting
     * information on all chunks, 'm' is the number of sectors required to hold
     * chunk information and 'k' is the number of available LBAs.
     *
     */

    usable_size = secs_total * (sec_size + md_size);
    ns_size = usable_size + sizeof(OcssdIdentity) + acct_size + logpage_size;

    size = sizeof(OcssdFormatHeader) + ns_size * num_ns;

    qemu_opt_set_number(opts, "size", size, errp);

    ret = bdrv_create_file(filename, opts, errp);
    if (ret < 0) {
        error_propagate(errp, local_err);
        goto fail;
    }

    bs = bdrv_open(filename, NULL, NULL,
        BDRV_O_RDWR | BDRV_O_RESIZE | BDRV_O_PROTOCOL, errp);
    if (bs == NULL) {
        ret = -EIO;
        goto fail;
    }

    blk = blk_new(BLK_PERM_WRITE | BLK_PERM_RESIZE, BLK_PERM_ALL);
    ret = blk_insert_bs(blk, bs, errp);
    if (ret < 0) {
        goto fail;
    }

    blk_set_allow_write_beyond_eof(blk, true);

    ret = blk_truncate(blk, 0, PREALLOC_MODE_OFF, errp);
    if (ret < 0) {
        goto fail;
    }

    /*
     * Calculate an "optimal" LBA address format that uses as few bits as
     * possible.
     */
    lbaf = (OcssdIdLBAF) {
        .sec_len = 32 - clz32(num_sec - 1),
        .chk_len = 32 - clz32(num_chk - 1),
        .pu_len  = 32 - clz32(num_pu - 1),
        .grp_len = 32 - clz32(num_grp - 1),
    };

    ocssd_ns_optimal_addrf(&addrf, &lbaf);

    hdr = (OcssdFormatHeader) {
        .magic = OCSSD_MAGIC,
        .version = 0x1,
        .num_ns = num_ns,
        .md_size = md_size,
        .sector_size = sec_size,
        .ns_size = ns_size,
        .pe_cycles = pe_cycles,
        .lbaf = lbaf,
    };

    ret = blk_pwrite(blk, 0, &hdr, sizeof(OcssdFormatHeader), 0);
    if (ret < 0) {
        goto fail;
    }

    offset = sizeof(OcssdFormatHeader);
    for (int i = 0; i < num_ns; i++) {
        id = (OcssdIdentity) {
            .ver.major = 2,
            .ver.minor = 0,
            .lbaf = lbaf,
            .mccap = mccap,
            .wit = wit,
            .geo = (OcssdIdGeo) {
                .num_grp = num_grp,
                .num_pu  = num_pu,
                .num_chk = num_chk,
                .clba    = num_sec,
            },
            .wrt = (OcssdIdWrt) {
                .ws_min    = ws_min,
                .ws_opt    = ws_opt,
                .mw_cunits = mw_cunits,
            },
            .perf = (OcssdIdPerf) {
                .trdt = cpu_to_le32(70000),
                .trdm = cpu_to_le32(100000),
                .tprt = cpu_to_le32(1900000),
                .tprm = cpu_to_le32(3500000),
                .tbet = cpu_to_le32(3000000),
                .tbem = cpu_to_le32(3000000),
            },
        };

        ret = blk_pwrite(blk, offset, &id, sizeof(OcssdIdentity), 0);
        if (ret < 0) {
            goto fail;
        }

        offset += sizeof(OcssdIdentity);

        acct = g_malloc0(acct_size);
        ret = blk_pwrite(blk, offset, acct, acct_size, 0);
        g_free(acct);
        if (ret < 0) {
            goto fail;
        }

        offset += acct_size;

        chk = g_malloc0(logpage_size);
        for (int i = 0; i < chks_total; i++) {
            chk[i].state = OCSSD_CHUNK_FREE;
            chk[i].type = OCSSD_CHUNK_TYPE_SEQUENTIAL;
            chk[i].wear_index = 0;
            chk[i].slba = (i / (num_chk * num_pu)) << addrf.grp_offset
                | (i % (num_chk * num_pu) / num_chk) << addrf.pu_offset
                | (i % num_chk) << addrf.chk_offset;
            chk[i].cnlb = num_sec;
            chk[i].wp = 0;
        }

        ret = blk_pwrite(blk, offset, chk, logpage_size, 0);
        g_free(chk);
        if (ret < 0) {
            goto fail;
        }

        offset += logpage_size + usable_size;
    }

    ret = blk_truncate(blk, size, PREALLOC_MODE_OFF, errp);
    if (ret < 0) {
        goto fail;
    }

fail:
    bdrv_unref(bs);
    return ret;
}

static int ocssd_open(BlockDriverState *bs, QDict *options, int flags,
    Error **errp)
{
    BDRVOcssdState *s = bs->opaque;
    OcssdIdLBAF *lbaf;
    int ret;
    int64_t len;
    uint64_t nblks;

    bs->file = bdrv_open_child(NULL, options, "file", bs, &child_file,
                               false, errp);
    if (!bs->file) {
        return -EINVAL;
    }

    bs->sg = bs->file->bs->sg;
    bs->supported_write_flags = BDRV_REQ_WRITE_UNCHANGED |
        (BDRV_REQ_FUA & bs->file->bs->supported_write_flags);
    bs->supported_zero_flags = BDRV_REQ_WRITE_UNCHANGED |
        ((BDRV_REQ_FUA | BDRV_REQ_MAY_UNMAP) &
            bs->file->bs->supported_zero_flags);

    len = bdrv_getlength(bs->file->bs);
    if (len < 0) {
        error_setg_errno(errp, -len, "could not get image size");
        return len;
    }
    s->real_size = len;

    ret = bdrv_pread(bs->file, 0, &s->hdr, sizeof(OcssdFormatHeader));
    if (ret < 0) {
        return ret;
    }

    /*
     * Calculate the size according to the address space. This is returned from
     * ocssd_getlength. See ocssd_getlength and ocssd_co_preadv.
     */
    lbaf = &s->hdr.lbaf;
    nblks = 1 << (lbaf->grp_len + lbaf->pu_len + lbaf->chk_len +
        lbaf->sec_len);
    s->size = s->hdr.num_ns * nblks * (s->hdr.sector_size + s->hdr.md_size);

    s->namespaces = g_new0(OcssdIdentity, s->hdr.num_ns);

    for (int i = 0; i < s->hdr.num_ns; i++) {
        ret = bdrv_pread(bs->file, s->hdr.sector_size + i * s->hdr.ns_size,
            &s->namespaces[i], sizeof(OcssdIdentity));
        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}

static int ocssd_probe(const uint8_t *buf, int buf_size, const char *filename)
{
    const OcssdFormatHeader *hdr = (const void *) buf;

    if (buf_size < sizeof(*hdr)) {
        return 0;
    }

    if (hdr->magic == OCSSD_MAGIC && hdr->version == 1) {
        return 100;
    }

    return 0;
}

static int ocssd_probe_blocksizes(BlockDriverState *bs, BlockSizes *bsz)
{
    return bdrv_probe_blocksizes(bs->file->bs, bsz);
}

static int ocssd_probe_geometry(BlockDriverState *bs, HDGeometry *geo)
{
    return bdrv_probe_geometry(bs->file->bs, geo);
}

static int coroutine_fn ocssd_co_copy_range_from(BlockDriverState *bs,
    BdrvChild *src, uint64_t src_offset, BdrvChild *dst, uint64_t dst_offset,
    uint64_t bytes, BdrvRequestFlags read_flags, BdrvRequestFlags write_flags)
{
    return bdrv_co_copy_range_from(bs->file, src_offset, dst, dst_offset,
        bytes, read_flags, write_flags);
}

static int coroutine_fn ocssd_co_copy_range_to(BlockDriverState *bs,
    BdrvChild *src, uint64_t src_offset, BdrvChild *dst, uint64_t dst_offset,
    uint64_t bytes, BdrvRequestFlags read_flags, BdrvRequestFlags write_flags)
{
    return bdrv_co_copy_range_to(src, src_offset, bs->file, dst_offset, bytes,
        read_flags, write_flags);
}

BlockDriver bdrv_ocssd = {
    .format_name   = "ocssd",
    .instance_size = sizeof(BDRVOcssdState),

    .bdrv_probe          = &ocssd_probe,
    .bdrv_open           = &ocssd_open,
    .bdrv_reopen_prepare = &ocssd_reopen_prepare,
    .bdrv_child_perm     = bdrv_filter_default_perms,

    .bdrv_co_create_opts     = &ocssd_co_create_opts,
    .bdrv_co_preadv          = &ocssd_co_preadv,
    .bdrv_co_pwritev         = &ocssd_co_pwritev,
    .bdrv_co_pwrite_zeroes   = &ocssd_co_pwrite_zeroes,
    .bdrv_co_pdiscard        = &ocssd_co_pdiscard,
    .bdrv_co_block_status    = &ocssd_co_block_status,
    .bdrv_co_copy_range_from = &ocssd_co_copy_range_from,
    .bdrv_co_copy_range_to   = &ocssd_co_copy_range_to,
    .bdrv_co_truncate        = &ocssd_co_truncate,
    .bdrv_co_ioctl           = &ocssd_co_ioctl,

    .bdrv_getlength         = &ocssd_getlength,
    .bdrv_measure           = &ocssd_measure,
    .bdrv_get_info          = &ocssd_get_info,
    .bdrv_get_specific_info = &ocssd_get_specific_info,
    .bdrv_refresh_limits    = &ocssd_refresh_limits,
    .bdrv_probe_blocksizes  = &ocssd_probe_blocksizes,
    .bdrv_probe_geometry    = &ocssd_probe_geometry,
    .bdrv_eject             = &ocssd_eject,
    .bdrv_lock_medium       = &ocssd_lock_medium,
    .bdrv_has_zero_init     = &ocssd_has_zero_init,

    .create_opts = &ocssd_create_opts,

    .no_size_required = true,
};

static void bdrv_ocssd_init(void)
{
    bdrv_register(&bdrv_ocssd);
}

block_init(bdrv_ocssd_init);
