/*
 * Support of Parallels Format Extension. It's a part of Parallels format
 * driver.
 *
 * Copyright (c) 2021 Virtuozzo International GmbH
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "block/block-io.h"
#include "block/block_int.h"
#include "block/dirty-bitmap.h"
#include "parallels.h"
#include "crypto/hash.h"
#include "qemu/uuid.h"
#include "qemu/memalign.h"

#define PARALLELS_FORMAT_EXTENSION_MAGIC 0xAB234CEF23DCEA87ULL

#define PARALLELS_END_OF_FEATURES_MAGIC 0x0ULL
#define PARALLELS_DIRTY_BITMAP_FEATURE_MAGIC 0x20385FAE252CB34AULL

typedef struct ParallelsFormatExtensionHeader {
    uint64_t magic; /* PARALLELS_FORMAT_EXTENSION_MAGIC */
    uint8_t check_sum[16];
} QEMU_PACKED ParallelsFormatExtensionHeader;

typedef struct ParallelsFeatureHeader {
    uint64_t magic;
    uint64_t flags;
    uint32_t data_size;
    uint32_t _unused;
} QEMU_PACKED ParallelsFeatureHeader;

typedef struct ParallelsDirtyBitmapFeature {
    uint64_t size;
    uint8_t id[16];
    uint32_t granularity;
    uint32_t l1_size;
    /* L1 table follows */
} QEMU_PACKED ParallelsDirtyBitmapFeature;

/* Given L1 table read bitmap data from the image and populate @bitmap */
static int parallels_load_bitmap_data(BlockDriverState *bs,
                                      const uint64_t *l1_table,
                                      uint32_t l1_size,
                                      BdrvDirtyBitmap *bitmap,
                                      Error **errp)
{
    BDRVParallelsState *s = bs->opaque;
    int ret = 0;
    uint64_t offset, limit;
    uint64_t bm_size = bdrv_dirty_bitmap_size(bitmap);
    uint8_t *buf = NULL;
    uint64_t i, tab_size =
        DIV_ROUND_UP(bdrv_dirty_bitmap_serialization_size(bitmap, 0, bm_size),
                     s->cluster_size);

    if (tab_size != l1_size) {
        error_setg(errp, "Bitmap table size %" PRIu32 " does not correspond "
                   "to bitmap size and cluster size. Expected %" PRIu64,
                   l1_size, tab_size);
        return -EINVAL;
    }

    buf = qemu_blockalign(bs, s->cluster_size);
    limit = bdrv_dirty_bitmap_serialization_coverage(s->cluster_size, bitmap);
    for (i = 0, offset = 0; i < tab_size; ++i, offset += limit) {
        uint64_t count = MIN(bm_size - offset, limit);
        uint64_t entry = l1_table[i];

        if (entry == 0) {
            /* No need to deserialize zeros because @bitmap is cleared. */
            continue;
        }

        if (entry == 1) {
            bdrv_dirty_bitmap_deserialize_ones(bitmap, offset, count, false);
        } else {
            ret = bdrv_pread(bs->file, entry << BDRV_SECTOR_BITS,
                             s->cluster_size, buf, 0);
            if (ret < 0) {
                error_setg_errno(errp, -ret,
                                 "Failed to read bitmap data cluster");
                goto finish;
            }
            bdrv_dirty_bitmap_deserialize_part(bitmap, buf, offset, count,
                                               false);
        }
    }
    ret = 0;

    bdrv_dirty_bitmap_deserialize_finish(bitmap);

finish:
    qemu_vfree(buf);

    return ret;
}

/*
 * @data buffer (of @data_size size) is the Dirty bitmaps feature which
 * consists of ParallelsDirtyBitmapFeature followed by L1 table.
 */
static BdrvDirtyBitmap *parallels_load_bitmap(BlockDriverState *bs,
                                              uint8_t *data,
                                              size_t data_size,
                                              Error **errp)
{
    int ret;
    ParallelsDirtyBitmapFeature bf;
    g_autofree uint64_t *l1_table = NULL;
    BdrvDirtyBitmap *bitmap;
    QemuUUID uuid;
    char uuidstr[UUID_FMT_LEN + 1];
    int i;

    if (data_size < sizeof(bf)) {
        error_setg(errp, "Too small Bitmap Feature area in Parallels Format "
                   "Extension: %zu bytes, expected at least %zu bytes",
                   data_size, sizeof(bf));
        return NULL;
    }
    memcpy(&bf, data, sizeof(bf));
    bf.size = le64_to_cpu(bf.size);
    bf.granularity = le32_to_cpu(bf.granularity) << BDRV_SECTOR_BITS;
    bf.l1_size = le32_to_cpu(bf.l1_size);
    data += sizeof(bf);
    data_size -= sizeof(bf);

    if (bf.size != bs->total_sectors) {
        error_setg(errp, "Bitmap size (in sectors) %" PRId64 " differs from "
                   "disk size in sectors %" PRId64, bf.size, bs->total_sectors);
        return NULL;
    }

    if (bf.l1_size * sizeof(uint64_t) > data_size) {
        error_setg(errp, "Bitmaps feature corrupted: l1 table exceeds "
                   "extension data_size");
        return NULL;
    }

    memcpy(&uuid, bf.id, sizeof(uuid));
    qemu_uuid_unparse(&uuid, uuidstr);
    bitmap = bdrv_create_dirty_bitmap(bs, bf.granularity, uuidstr, errp);
    if (!bitmap) {
        return NULL;
    }

    l1_table = g_new(uint64_t, bf.l1_size);
    for (i = 0; i < bf.l1_size; i++, data += sizeof(uint64_t)) {
        l1_table[i] = ldq_le_p(data);
    }

    ret = parallels_load_bitmap_data(bs, l1_table, bf.l1_size, bitmap, errp);
    if (ret < 0) {
        bdrv_release_dirty_bitmap(bitmap);
        return NULL;
    }

    return bitmap;
}

static int parallels_parse_format_extension(BlockDriverState *bs,
                                            uint8_t *ext_cluster, Error **errp)
{
    BDRVParallelsState *s = bs->opaque;
    int ret;
    int remaining = s->cluster_size;
    uint8_t *pos = ext_cluster;
    ParallelsFormatExtensionHeader eh;
    g_autofree uint8_t *hash = NULL;
    size_t hash_len = 0;
    GSList *bitmaps = NULL, *el;

    memcpy(&eh, pos, sizeof(eh));
    eh.magic = le64_to_cpu(eh.magic);
    pos += sizeof(eh);
    remaining -= sizeof(eh);

    if (eh.magic != PARALLELS_FORMAT_EXTENSION_MAGIC) {
        error_setg(errp, "Wrong parallels Format Extension magic: 0x%" PRIx64
                   ", expected: 0x%llx", eh.magic,
                   PARALLELS_FORMAT_EXTENSION_MAGIC);
        goto fail;
    }

    ret = qcrypto_hash_bytes(QCRYPTO_HASH_ALG_MD5, (char *)pos, remaining,
                             &hash, &hash_len, errp);
    if (ret < 0) {
        goto fail;
    }

    if (hash_len != sizeof(eh.check_sum) ||
        memcmp(hash, eh.check_sum, sizeof(eh.check_sum)) != 0) {
        error_setg(errp, "Wrong checksum in Format Extension header. Format "
                   "extension is corrupted.");
        goto fail;
    }

    while (true) {
        ParallelsFeatureHeader fh;
        BdrvDirtyBitmap *bitmap;

        if (remaining < sizeof(fh)) {
            error_setg(errp, "Can not read feature header, as remaining bytes "
                       "(%d) in Format Extension is less than Feature header "
                       "size (%zu)", remaining, sizeof(fh));
            goto fail;
        }

        memcpy(&fh, pos, sizeof(fh));
        pos += sizeof(fh);
        remaining -= sizeof(fh);

        fh.magic = le64_to_cpu(fh.magic);
        fh.flags = le64_to_cpu(fh.flags);
        fh.data_size = le32_to_cpu(fh.data_size);

        if (fh.flags) {
            error_setg(errp, "Flags for extension feature are unsupported");
            goto fail;
        }

        if (fh.data_size > remaining) {
            error_setg(errp, "Feature data_size exceedes Format Extension "
                       "cluster");
            goto fail;
        }

        switch (fh.magic) {
        case PARALLELS_END_OF_FEATURES_MAGIC:
            return 0;

        case PARALLELS_DIRTY_BITMAP_FEATURE_MAGIC:
            bitmap = parallels_load_bitmap(bs, pos, fh.data_size, errp);
            if (!bitmap) {
                goto fail;
            }
            bitmaps = g_slist_append(bitmaps, bitmap);
            break;

        default:
            error_setg(errp, "Unknown feature: 0x%" PRIx64, fh.magic);
            goto fail;
        }

        pos = ext_cluster + QEMU_ALIGN_UP(pos + fh.data_size - ext_cluster, 8);
    }

fail:
    for (el = bitmaps; el; el = el->next) {
        bdrv_release_dirty_bitmap(el->data);
    }
    g_slist_free(bitmaps);

    return -EINVAL;
}

int parallels_read_format_extension(BlockDriverState *bs,
                                    int64_t ext_off, Error **errp)
{
    BDRVParallelsState *s = bs->opaque;
    int ret;
    uint8_t *ext_cluster = qemu_blockalign(bs, s->cluster_size);

    assert(ext_off > 0);

    ret = bdrv_pread(bs->file, ext_off, s->cluster_size, ext_cluster, 0);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Failed to read Format Extension cluster");
        goto out;
    }

    ret = parallels_parse_format_extension(bs, ext_cluster, errp);

out:
    qemu_vfree(ext_cluster);

    return ret;
}

static void parallels_save_bitmap(BlockDriverState *bs, BdrvDirtyBitmap *bitmap,
                                  uint8_t **buf, int *buf_size)
{
    BDRVParallelsState *s = bs->opaque;
    ParallelsFeatureHeader *fh;
    ParallelsDirtyBitmapFeature *bh;
    uint64_t *l1_table, l1_size, granularity, limit;
    int64_t bm_size, ser_size, offset, buf_used;
    int64_t alloc_size = 1;
    const char *name;
    uint8_t *bm_buf;
    QemuUUID uuid;
    int ret = 0;

    if (!bdrv_dirty_bitmap_get_persistence(bitmap) ||
        bdrv_dirty_bitmap_inconsistent(bitmap)) {
        return;
    }

    bm_size = bdrv_dirty_bitmap_size(bitmap);
    granularity = bdrv_dirty_bitmap_granularity(bitmap);
    limit = bdrv_dirty_bitmap_serialization_coverage(s->cluster_size, bitmap);
    ser_size = bdrv_dirty_bitmap_serialization_size(bitmap, 0, bm_size);
    l1_size = DIV_ROUND_UP(ser_size, s->cluster_size);

    buf_used = l1_size * 8 + sizeof(*fh) + sizeof(*bh);
    /* Check if there is enough space for the final section */
    if (*buf_size - buf_used < sizeof(*fh)) {
        return;
    }

    name = bdrv_dirty_bitmap_name(bitmap);
    ret = qemu_uuid_parse(name, &uuid);
    if (ret < 0) {
        error_report("Can't save dirty bitmap: ID parsing error: '%s'", name);
        return;
    }

    fh = (ParallelsFeatureHeader *)*buf;
    bh = (ParallelsDirtyBitmapFeature *)(*buf + sizeof(*fh));
    l1_table = (uint64_t *)((uint8_t *)bh + sizeof(*bh));

    fh->magic = cpu_to_le64(PARALLELS_DIRTY_BITMAP_FEATURE_MAGIC);
    fh->data_size = cpu_to_le32(l1_size * 8 + sizeof(*bh));

    bh->l1_size = cpu_to_le32(l1_size);
    bh->size = cpu_to_le64(bm_size >> BDRV_SECTOR_BITS);
    bh->granularity = cpu_to_le32(granularity >> BDRV_SECTOR_BITS);
    memcpy(bh->id, &uuid, sizeof(uuid));

    bm_buf = qemu_blockalign(bs, s->cluster_size);

    offset = 0;
    while ((offset = bdrv_dirty_bitmap_next_dirty(bitmap, offset, bm_size)) >= 0) {
        uint64_t idx = offset / limit;
        int64_t cluster_off, end, write_size, first_zero;

        offset = QEMU_ALIGN_DOWN(offset, limit);
        end = MIN(bm_size, offset + limit);
        write_size = bdrv_dirty_bitmap_serialization_size(bitmap, offset,
                                                          end - offset);
        assert(write_size <= s->cluster_size);

        bdrv_dirty_bitmap_serialize_part(bitmap, bm_buf, offset, end - offset);
        if (write_size < s->cluster_size) {
            memset(bm_buf + write_size, 0, s->cluster_size - write_size);
        }

        first_zero = bdrv_dirty_bitmap_next_zero(bitmap, offset, bm_size);
        if (first_zero < 0) {
            goto end;
        }
        if (first_zero - offset >= s->cluster_size) {
            l1_table[idx] = 1;
            offset = end;
            continue;
        }

        cluster_off = parallels_allocate_host_clusters(bs, &alloc_size);
        if (cluster_off <= 0) {
            goto end;
        }

        ret = bdrv_pwrite(bs->file, cluster_off, s->cluster_size, bm_buf, 0);
        if (ret < 0) {
            memset(&fh->magic, 0, sizeof(fh->magic));
            parallels_mark_unused(bs, s->used_bmap, s->used_bmap_size,
                                  cluster_off, 1);
            goto end;
        }

        l1_table[idx] = cpu_to_le64(cluster_off >> BDRV_SECTOR_BITS);
        offset = end;
    }

    *buf_size -= buf_used;
    *buf += buf_used;

end:
    qemu_vfree(bm_buf);
}

void parallels_store_persistent_dirty_bitmaps(BlockDriverState *bs,
                                              Error **errp)
{
    BDRVParallelsState *s = bs->opaque;
    BdrvDirtyBitmap *bitmap;
    ParallelsFormatExtensionHeader *eh;
    int remaining = s->cluster_size;
    uint8_t *buf, *pos;
    int64_t header_off, alloc_size = 1;
    g_autofree uint8_t *hash = NULL;
    size_t hash_len = 0;
    int ret;

    s->header->ext_off = 0;

    if (!bdrv_has_named_bitmaps(bs)) {
        return;
    }

    buf = qemu_blockalign0(bs, s->cluster_size);

    eh = (ParallelsFormatExtensionHeader *)buf;
    pos = buf + sizeof(*eh);

    eh->magic = cpu_to_le64(PARALLELS_FORMAT_EXTENSION_MAGIC);

    FOR_EACH_DIRTY_BITMAP(bs, bitmap) {
        parallels_save_bitmap(bs, bitmap, &pos, &remaining);
    }

    header_off = parallels_allocate_host_clusters(bs, &alloc_size);
    if (header_off < 0) {
        error_report("Can't save dirty bitmap: cluster allocation error");
        ret = header_off;
        goto end;
    }

    ret = qcrypto_hash_bytes(QCRYPTO_HASH_ALG_MD5,
                             (const char *)(buf + sizeof(*eh)),
                             s->cluster_size - sizeof(*eh),
                             &hash, &hash_len, errp);
    if (ret < 0 || hash_len != sizeof(eh->check_sum)) {
        error_report("Can't save dirty bitmap: hash error");
        ret = -EINVAL;
        goto end;
    }
    memcpy(eh->check_sum, hash, hash_len);

    ret = bdrv_pwrite(bs->file, header_off, s->cluster_size, buf, 0);
    if (ret < 0) {
        error_report("Can't save dirty bitmap: IO error");
        parallels_mark_unused(bs, s->used_bmap, s->used_bmap_size,
                              header_off, 1);
        goto end;
    }

    s->header->ext_off = cpu_to_le64(header_off / BDRV_SECTOR_SIZE);
end:
    qemu_vfree(buf);
}

bool coroutine_fn parallels_co_can_store_new_dirty_bitmap(BlockDriverState *bs,
                                                          const char *name,
                                                          uint32_t granularity,
                                                          Error **errp)
{
    if (bdrv_find_dirty_bitmap(bs, name)) {
        error_setg(errp, "Bitmap already exists: %s", name);
        return false;
    }

    return true;
}
