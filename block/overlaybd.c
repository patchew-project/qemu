/*
 *
 * Read-only block driver for OverlayBD images
 *
 * An OverlayBD image is a stack of layer files. Each layer is an LSMT
 * blob: a 4096-byte header, a data region,
 * a sorted index of 16-byte LBA mappings, and a 4096-byte trailer. The
 * driver merges the per-layer indexes top-down (a top layer's mapping
 * wins its whole extent; lower layers only fill holes) and serves reads
 * at 512-byte sector granularity. Holes and zeroed mappings read as
 * zeros.
 *
 * Layers may optionally be wrapped in a single-entry tar and/or
 * compressed with ZFile (block-wise LZ4 or ZSTD with a jump table).
 *
 * The layer stack is given either by an OCI image manifest, whose layer
 * digests name blobs in a local directory (manifest option, or the file
 * child for -drive file=... usage), or as an explicit bottom-first
 * "layers" array.
 *
 * See also the reference implementation:
 * https://github.com/containerd/overlaybd
 *
 * Specifications for the on-disk formats:
 *   LSMT layer blob format (header, data, index, trailer)
 *     https://github.com/containerd/overlaybd/blob/main/src/overlaybd/lsmt/format_spec.md
 *   ZFile block compression format (jump table, LZ4/ZSTD)
 *     https://github.com/containerd/overlaybd/blob/main/src/overlaybd/zfile/format_spec.md
 *
 * The layer blobs are named by an OCI image manifest; the remaining OCI
 * specifications are listed where the manifest is parsed:
 *   https://github.com/opencontainers/image-spec/blob/main/manifest.md
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "block/block-io.h"
#include "block/block_int.h"
#include "block/qdict.h"
#include "qemu/module.h"
#include "qemu/bswap.h"
#include "qemu/crc32c.h"
#include "qemu/cutils.h"
#include "qemu/memalign.h"
#include "qobject/qjson.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"

#ifdef CONFIG_LZ4
#include <lz4.h>
#endif
#ifdef CONFIG_ZSTD
#include <zstd.h>
#endif

/*
 *
 * On-disk format definitions below mirror the reference implementation
 * (containerd/overlaybd: lsmt/file.cpp HeaderTrailer, lsmt/index.h,
 * zfile/zfile.cpp HeaderTrailer, zfile/compressor.h CompressOptions)
 * field for field; per-struct citations mark the source of each layout.
 * Metadata is little-endian on disk and is read through explicit
 * little-endian accessors, which also sidesteps the signedness of
 * ldl_le_p() for the 32-bit fields.
 */

static const char lsmt_magic0[8] = "LSMT\0\1\2";
static const uint8_t lsmt_magic1[16] = {
    0x65, 0x7e, 0x63, 0xd2, 0x94, 0x44, 0x08, 0x4c,
    0xa2, 0xd2, 0xc8, 0xec, 0x4f, 0xcf, 0xae, 0x8a
};
static const char zfile_magic0[8] = "ZFile\0\1";
static const uint8_t zfile_magic1[16] = "tuji.yyf@Alibaba";

/* LSMT::HeaderTrailer::SPACE, lsmt/file.cpp */
#define LSMT_HT_SPACE             4096
/* LSMT::MAX_LSMT_RO_INDEX_SIZE, lsmt/index.h */
#define LSMT_MAX_RO_INDEX_ENTRIES 1000000LL
/* LSMT::MAX_LSMT_INDEX_SIZE, lsmt/index.h: cap on the merged index */
#define LSMT_MAX_MERGED_ENTRIES   128000000LL
/* LSMT::MAX_STACK_LAYERS, lsmt/file.h */
#define LSMT_MAX_LAYERS           255
/* LSMT::Segment::INVALID_OFFSET, lsmt/index.h */
#define LSMT_INVALID_OFFSET       (((uint64_t)1 << 50) - 1)
#define LSMT_MOFFSET_BEGIN        (LSMT_HT_SPACE / BDRV_SECTOR_SIZE)

/* LSMT::HeaderTrailer::FLAG_SHIFT_*, lsmt/file.cpp */
#define LSMT_FLAG_HEADER    (1u << 0)
#define LSMT_FLAG_DATA_FILE (1u << 1)
#define LSMT_FLAG_SEALED    (1u << 2)

/*
 * LSMT::HeaderTrailer, lsmt/file.cpp: a packed struct with byte-aligned
 * fields only (no bitfields), identical to the upstream definition less
 * the C++ methods, and covering the whole LSMT_HT_SPACE block that is
 * read and written.  Upstream stops at user_tag (sizeof == 390) and
 * keeps the tail outside the type, reading the block into a raw buffer
 * and casting it; format_spec.md:32 documents that tail as a header
 * field, so it is declared here instead.
 */
typedef struct QEMU_PACKED OverlaybdLSMTHeader {
    uint64_t magic0;
    uint8_t magic1[16];
    uint32_t size;           /* 390: the fields above, tail excluded */
    uint32_t flags;
    uint64_t index_offset;
    uint64_t index_size;     /* # of 16-byte index entries */
    uint64_t virtual_size;   /* bytes */
    uint8_t uuid[37];        /* UUID::String */
    uint8_t parent_uuid[37]; /* UUID::String */
    uint16_t reserved;
    uint8_t version;
    uint8_t sub_version;
    uint8_t user_tag[256];   /* TAG_SIZE */
    uint8_t reserved_space[3706];
} OverlaybdLSMTHeader;

QEMU_BUILD_BUG_ON(sizeof(OverlaybdLSMTHeader) != LSMT_HT_SPACE);

/* ZFile::HeaderTrailer::SPACE, zfile/zfile.cpp */
#define ZFILE_HT_SPACE       512
/*
 * ZFile::MAX_READ_SIZE, zfile/zfile.h: upstream uses this one constant
 * both as the block size cap and as the size of BlockReader's
 * compressed-data read window.  Only the block size cap is needed here --
 * the read window is sized to the request.
 */
#define ZFILE_MAX_BLOCK_SIZE 65536
/*
 * qemu-side hardening: a jump table entry is image-controlled, so cap the
 * compressed size of one block to bound the read window.  Upstream has no
 * span cap; it ERANGEs when a block overflows its 64 KiB window instead.
 */
#define ZFILE_MAX_SPAN       (1u << 20)
/* ZFile::CompressOptions::LZ4 / ZSTD, zfile/compressor.h */
#define ZFILE_ALGO_LZ4       1
#define ZFILE_ALGO_ZSTD      2

/* ZFile::HeaderTrailer::FLAG_SHIFT_*, zfile/zfile.cpp */
#define ZFILE_FLAG_HEADER           (1ULL << 0)
#define ZFILE_FLAG_DATA_FILE        (1ULL << 1)
#define ZFILE_FLAG_SEALED           (1ULL << 2)
#define ZFILE_FLAG_HEADER_OVERWRITE (1ULL << 3)
#define ZFILE_FLAG_DIGEST           (1ULL << 4)
#define ZFILE_FLAG_INDEX_COMPRESSED (1ULL << 5)

/* ZFile::CompressOptions, zfile/compressor.h: sizeof == 24 */
typedef struct QEMU_PACKED OverlaybdCompressOptions {
    uint32_t block_size;
    uint8_t algo;
    uint8_t level;
    uint8_t use_dict;
    uint8_t __padding_0;
    uint32_t reserved;
    uint32_t dict_size;
    uint8_t verify;
    uint8_t __padding_1[7];
} OverlaybdCompressOptions;

/*
 * ZFile::HeaderTrailer, zfile/zfile.cpp, embedding ZFile::CompressOptions
 * at offset 72: upstream is not packed, but every member is naturally
 * aligned at these offsets, so a packed C struct has the identical
 * layout.  As with the LSMT header, the ZFILE_HT_SPACE tail that
 * upstream keeps outside the type is declared here, per
 * format_spec.md:39 (offset 89 ~ 511, of which opt.__padding_1 already
 * covers 89 ~ 95).
 */
typedef struct QEMU_PACKED OverlaybdZFileHeader {
    uint64_t magic0;
    uint8_t magic1[16];
    uint32_t size;           /* 96: the fields above, tail excluded */
    uint32_t digest;         /* CRC32C of bytes 28..511, digest zeroed */
    uint64_t flags;
    uint64_t index_offset;
    uint64_t index_size;     /* # of u32 jump-table entries (blocks) */
    uint64_t original_file_size;
    uint32_t index_crc;
    uint32_t reserved_0;
    OverlaybdCompressOptions opt;
    uint8_t reserved_space[416];
} OverlaybdZFileHeader;

QEMU_BUILD_BUG_ON(sizeof(OverlaybdCompressOptions) != 24);
QEMU_BUILD_BUG_ON(sizeof(OverlaybdZFileHeader) != ZFILE_HT_SPACE);

/* ZFile::MAX_ZFILE_INDEX_SIZE, zfile/zfile.cpp */
#define ZFILE_MAX_INDEX_ENTRIES 1000000000LL

/* crc32c_salt()'s seed, zfile/zfile.cpp: per-block CRCs are salted */
#define ZFILE_CRC_SALT          100007

/*
 * ZFile::JumpTable, zfile/zfile.cpp: within-group offsets are uint16_t,
 * so one group spans at most UINT16_MAX + 1 bytes of compressed data.
 */
#define ZFILE_JUMP_GROUP_BYTES  (UINT16_MAX + 1u)

#define ZFILE_INDEX_CHUNK_BYTES (1 << 20) /* ZFile::load_jump_table delta */
#define ZFILE_INDEX_MAX_COROUTINES 32

/*
 * In-memory 16-byte index entry. The field assignment mirrors the
 * on-disk SegmentMapping (lo = lba:50 | length:14, hi = moffset:55 |
 * zeroed:1 | tag:8, the tag slot holding the serving layer at runtime,
 * like the reference implementation), but this struct deliberately does
 * NOT map the disk image: bitfields are forbidden in packed or
 * exact-layout structures (docs/devel/style.rst), so on-disk bytes are
 * decoded by overlaybd_decode_entry() with explicit little-endian loads
 * and the bit order below is compiler-managed, never exposed.
 */
typedef struct OverlaybdSegment {
    uint64_t lba:50;
    uint64_t length:14;
    uint64_t moffset:55;
    uint64_t zeroed:1;
    uint64_t layer:8;
} OverlaybdSegment;

QEMU_BUILD_BUG_ON(sizeof(OverlaybdSegment) != 16);

typedef struct OverlaybdZfile {
    uint64_t index_offset;       /* jump table offset, ZFile-relative bytes */
    uint64_t index_size;
    uint64_t original_file_size;
    uint32_t block_size;
    uint8_t algo;
    bool verify;
    /*
     * ZFile::JumpTable, zfile/zfile.cpp: block @i starts at
     * partial_offset[i >> group_shift] + deltas[i], except at a group
     * boundary where the delta is zero.
     */
    uint64_t *partial_offset;
    uint16_t *deltas;
    unsigned group_shift;
} OverlaybdZfile;

typedef struct OverlaybdLayer {
    BdrvChild *child;
    bool is_zfile;
    uint64_t base_offset; /* tar prefix length in the child, bytes */
    uint64_t view_size;
    OverlaybdZfile zf;
    /* LSMT metadata, in ZFile-virtual space for compressed layers */
    uint64_t index_offset;
    OverlaybdSegment *index;
    size_t index_count;
    uint64_t virtual_size;
} OverlaybdLayer;

typedef struct BDRVOverlaybdState {
    OverlaybdLayer *layers; /* top-first: layers[0] is the topmost layer */
    int nb_layers;
    OverlaybdSegment *merged;
    size_t merged_cap;
    size_t merged_count;
    uint64_t virtual_size;
} BDRVOverlaybdState;

/*
 * Load one on-disk 16-byte index entry (two little-endian u64:
 * lo = lba:50 | length:14, hi = moffset:55 | zeroed:1 | tag:8) into the
 * host-side entry. The on-disk tag is discarded; the merge assigns
 * layers.
 */
static void overlaybd_decode_entry(const uint8_t *p, OverlaybdSegment *out)
{
    uint64_t lo = ldq_le_p(p);
    uint64_t hi = ldq_le_p(p + 8);

    out->lba = lo & (((uint64_t)1 << 50) - 1);
    out->length = (lo >> 50) & ((1u << 14) - 1);
    out->moffset = hi & (((uint64_t)1 << 55) - 1);
    out->zeroed = (hi >> 55) & 1;
    out->layer = 0;
}

static bool overlaybd_has_magic(const void *ht, const void *magic0,
                                const void *magic1)
{
    return memcmp(ht, magic0, 8) == 0 &&
           memcmp((const uint8_t *)ht + 8, magic1, 16) == 0;
}

static bool overlaybd_is_lsmt_header(const uint8_t *buf)
{
    return overlaybd_has_magic(buf, lsmt_magic0, lsmt_magic1);
}

static bool overlaybd_is_zfile_header(const uint8_t *buf)
{
    return overlaybd_has_magic(buf, zfile_magic0, zfile_magic1);
}

static bool overlaybd_is_tar_header(const uint8_t *hdr)
{
    return memcmp(hdr + 257, "ustar", 5) == 0 &&
           (hdr[156] == '0' || hdr[156] == '\0' || hdr[156] == 'x');
}

static bool overlaybd_verify_lsmt_block(const OverlaybdLSMTHeader *ht,
                                        bool is_trailer)
{
    uint32_t flags;

    if (!overlaybd_has_magic(ht, lsmt_magic0, lsmt_magic1)) {
        return false;
    }
    flags = le32_to_cpu(ht->flags);
    if (is_trailer) {
        return !(flags & LSMT_FLAG_HEADER) &&
                (flags & LSMT_FLAG_DATA_FILE) &&
                (flags & LSMT_FLAG_SEALED);
    }
    return flags & LSMT_FLAG_HEADER;
}

/*
 * Upstream's crc32::crc32c_extend() is the raw CRC-32C recurrence with no
 * post-conditioning, while qemu's crc32c() inverts once on return, so XOR
 * the result to recover upstream's value for any seed. A wrong convention
 * here fails silently against real images: nothing but the digest mismatches.
 */
static uint32_t overlaybd_crc32c(uint32_t seed, const void *buf, size_t len)
{
    return crc32c(seed, buf, len) ^ 0xffffffff;
}

static bool overlaybd_zfile_verify_digest(OverlaybdZFileHeader *blk)
{
    uint32_t crc, saved = le32_to_cpu(blk->digest);

    blk->digest = 0;
    crc = overlaybd_crc32c(0, blk, sizeof(*blk));
    blk->digest = cpu_to_le32(saved);
    return crc == saved;
}

static bool overlaybd_zfile_verify_block(OverlaybdZFileHeader *blk,
                                         bool is_trailer)
{
    uint64_t flags;

    if (!overlaybd_has_magic(blk, zfile_magic0, zfile_magic1)) {
        return false;
    }
    flags = le64_to_cpu(blk->flags);
    if (is_trailer) {
        if ((flags & ZFILE_FLAG_HEADER) || !(flags & ZFILE_FLAG_DATA_FILE) ||
            !(flags & ZFILE_FLAG_SEALED)) {
            return false;
        }
    } else {
        if (!(flags & ZFILE_FLAG_HEADER)) {
            return false;
        }
    }
    if (flags & ZFILE_FLAG_DIGEST && !overlaybd_zfile_verify_digest(blk)) {
        return false;
    }
    return true;
}

/*
 * Locate a header/trailer block of @ht_space bytes near the end of the
 * layer and return its offset, relative to the child, or -errno.  The
 * block found is also copied to @ht_out, which must hold @ht_space bytes:
 * it has already been read and validated here, so re-reading it at the
 * returned offset would be a second I/O for the same bytes.
 *
 * The end of the layer is not the end of the child: a tar wrapper pads
 * its entry to a blocking factor, so the scan must stop at
 * base_offset + view_size. That end is still not exact for a bare layer,
 * whose view_size is just bdrv_getlength() rounded up to 512 bytes, so
 * scan the last @ht_space + 511 bytes for the magic (the same approach
 * as dmg_find_koly_offset()), taking the match closest to the end.
 */
static int64_t GRAPH_RDLOCK
overlaybd_scan_trailer(OverlaybdLayer *l, size_t ht_space, bool is_zfile,
                       void *ht_out)
{
    int64_t end = l->base_offset + l->view_size;
    int64_t window, off, found = -ENOENT;
    uint8_t *buf;
    int ret;

    window = (int64_t)ht_space + 511;
    off = end > window ? end - window : 0;
    window = end - off;

    buf = g_malloc(window);
    ret = bdrv_pread(l->child, off, window, buf, 0);
    if (ret < 0) {
        g_free(buf);
        return ret;
    }
    for (int64_t i = (int64_t)window - ht_space; i >= 0; i--) {
        bool ok;

        /* copy the whole block: the ZFile digest covers all 512 bytes */
        if (is_zfile) {
            OverlaybdZFileHeader blk;

            memcpy(&blk, buf + i, sizeof(blk));
            ok = overlaybd_zfile_verify_block(&blk, true);
        } else {
            OverlaybdLSMTHeader blk;

            memcpy(&blk, buf + i, sizeof(blk));
            ok = overlaybd_verify_lsmt_block(&blk, true);
        }
        if (ok) {
            memcpy(ht_out, buf + i, ht_space);
            found = off + i;
            break;
        }
    }
    g_free(buf);
    return found;
}

/*
 * ZFile::JumpTable::build(), zfile/zfile.cpp: turn the on-disk u32
 * block lengths into one absolute offset per group plus uint16_t
 * within-group prefix sums. @offset_begin is where block 0 lives,
 * relative to the ZFile.
 */
static int overlaybd_zfile_build_table(OverlaybdZfile *zf,
                                       const uint32_t *table,
                                       uint64_t offset_begin, Error **errp)
{
    uint64_t group_size = ZFILE_JUMP_GROUP_BYTES / zf->block_size;
    uint64_t min_span = zf->verify ? 4 : 0;
    uint64_t raw_offset = offset_begin;
    uint32_t span;

    zf->group_shift = ctz64(group_size);
    /*
     * g_try_new, not g_new: both sizes come from the image, and a hostile
     * header can ask for gigabytes (at block_size 65536, group_size is 1
     * and partial_offset costs 8 bytes per entry).
     */
    zf->partial_offset = g_try_new(uint64_t, zf->index_size / group_size + 1);
    zf->deltas = g_try_new(uint16_t, zf->index_size + 1);
    if (!zf->partial_offset || !zf->deltas) {
        return -ENOMEM;
    }
    zf->partial_offset[0] = raw_offset;
    zf->deltas[0] = 0;

    for (uint64_t i = 1; i <= zf->index_size; i++) {
        span = ldl_le_p((const uint8_t *)&table[i - 1]);

        if (span <= min_span || span > ZFILE_MAX_SPAN) {
            error_setg(errp, "invalid ZFile block size in jump table");
            return -EINVAL;
        }
        raw_offset += span;
        if (i % group_size == 0) {
            zf->partial_offset[i / group_size] = raw_offset;
            zf->deltas[i] = 0;
            continue;
        }
        if ((uint64_t)zf->deltas[i - 1] + span >= UINT16_MAX) {
            error_setg(errp, "ZFile compressed blocks in one jump table "
                             "group exceed %d bytes", UINT16_MAX);
            return -ERANGE;
        }
        zf->deltas[i] = zf->deltas[i - 1] + span;
    }
    return 0;
}

/* ZFile::JumpTable::operator[], zfile/zfile.cpp */
static uint64_t overlaybd_zfile_block_off(const OverlaybdZfile *zf,
                                          uint64_t idx)
{
    uint64_t part = zf->partial_offset[idx >> zf->group_shift];

    if (idx & ((1ULL << zf->group_shift) - 1)) {
        return part + zf->deltas[idx];
    }
    return part;
}

typedef struct OverlaybdZfileLoad {
    BdrvChild *child;
    int64_t offset;   /* table start, relative to the child */
    uint8_t *table;
    uint64_t bytes;   /* table size */
    uint64_t next;    /* first byte no coroutine has claimed yet */
    int nb_done;
    int ret;
} OverlaybdZfileLoad;

/*
 * The caller holds the graph rdlock across the whole open, so these
 * coroutines take no further lock. They share the one cursor in @load:
 * claiming a chunk reads load->next then advances it with no yield between,
 * and coroutines in one AioContext never run concurrently, so the chunks are
 * partitioned with no locking. Inputs are copied to locals here; only next,
 * ret and nb_done stay shared (a private next would spin on chunk 0 forever).
 */
static void coroutine_fn GRAPH_RDLOCK
overlaybd_zfile_load_co(void *opaque)
{
    OverlaybdZfileLoad *load = opaque;
    BdrvChild *child = load->child;
    int64_t offset = load->offset;
    uint8_t *table = load->table;
    uint64_t bytes = load->bytes;

    while (load->next < bytes) {
        uint64_t off = load->next;
        uint64_t len = MIN(bytes - off, (uint64_t)ZFILE_INDEX_CHUNK_BYTES);
        int ret;

        load->next = off + len;

        ret = bdrv_co_pread(child, offset + off, len, table + off, 0);
        if (ret < 0) {
            if (load->ret == 0) {
                load->ret = ret;
            }
            break;
        }
    }
    load->nb_done++;
}

static int GRAPH_RDLOCK
overlaybd_zfile_load_table(OverlaybdLayer *l, uint32_t *table, Error **errp)
{
    OverlaybdZfile *zf = &l->zf;
    uint64_t bytes = zf->index_size * sizeof(uint32_t);
    int ret;

    if (bytes <= ZFILE_INDEX_CHUNK_BYTES) {
        ret = bdrv_pread(l->child, l->base_offset + zf->index_offset,
                         bytes, table, 0);
    } else {
        OverlaybdZfileLoad load = {
            .child = l->child,
            .offset = l->base_offset + zf->index_offset,
            .table = (uint8_t *)table,
            .bytes = bytes,
        };
        int nb_chunks = DIV_ROUND_UP(bytes, ZFILE_INDEX_CHUNK_BYTES);
        int nb_cos = MIN(nb_chunks, ZFILE_INDEX_MAX_COROUTINES);

        for (int i = 0; i < nb_cos; i++) {
            qemu_coroutine_enter(qemu_coroutine_create(overlaybd_zfile_load_co,
                                                       &load));
        }
        BDRV_POLL_WHILE(l->child->bs, load.nb_done < nb_cos);
        ret = load.ret;
    }
    if (ret < 0) {
        error_setg_errno(errp, -ret, "could not read ZFile jump table");
    }
    return ret;
}

static int GRAPH_RDLOCK
overlaybd_zfile_open(OverlaybdLayer *l, Error **errp)
{
    BdrvChild *child = l->child;
    OverlaybdZfile *zf = &l->zf;
    OverlaybdZFileHeader ht;
    uint64_t flags, index_offset, index_size, original_file_size;
    uint32_t *table = NULL;
    uint32_t block_size;
    uint8_t algo, verify;
    int64_t child_size, zfile_end;
    uint64_t end_reserve;
    int ret;

    child_size = bdrv_getlength(child->bs);
    if (child_size < 0) {
        error_setg_errno(errp, -child_size, "could not get layer file size");
        return child_size;
    }
    child_size -= l->base_offset;
    if (child_size < 2 * ZFILE_HT_SPACE) {
        error_setg(errp, "layer file too small to be a ZFile");
        return -EINVAL;
    }

    ret = bdrv_pread(child, l->base_offset, sizeof(ht), &ht, 0);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "could not read ZFile header");
        return ret;
    }
    if (!overlaybd_zfile_verify_block(&ht, false)) {
        error_setg(errp, "invalid ZFile header");
        return -EINVAL;
    }

    flags = le64_to_cpu(ht.flags);
    /*
     * The layer's own end, not the child's: a tar wrapper pads its entry.
     * bdrv_getlength() also rounds up, so when there is a trailer the
     * scanned trailer position gives the exact end instead.
     */
    zfile_end = l->view_size;
    end_reserve = 0;
    if (!(flags & ZFILE_FLAG_HEADER_OVERWRITE)) {
        /*
         * Without HEADER_OVERWRITE the trailer carries the final metadata,
         * so the header is dead once its flags have been read and the
         * trailer is read into the same block -- as upstream does.
         */
        int64_t trl_off = overlaybd_scan_trailer(l, ZFILE_HT_SPACE, true,
                                                 &ht);

        if (trl_off < 0) {
            error_setg(errp, "invalid ZFile trailer");
            return -EINVAL;
        }
        flags = le64_to_cpu(ht.flags);
        zfile_end = trl_off + ZFILE_HT_SPACE - (int64_t)l->base_offset;
        end_reserve = ZFILE_HT_SPACE;
    }

    if (flags & ZFILE_FLAG_INDEX_COMPRESSED) {
        error_setg(errp, "compressed ZFile jump table is not supported");
        return -ENOTSUP;
    }

    index_offset = le64_to_cpu(ht.index_offset);
    index_size = le64_to_cpu(ht.index_size);
    original_file_size = le64_to_cpu(ht.original_file_size);
    block_size = le32_to_cpu(ht.opt.block_size);
    algo = ht.opt.algo;
    verify = ht.opt.verify;

    if (ht.opt.use_dict != 0) {
        error_setg(errp, "ZFile with dictionary is not supported");
        return -ENOTSUP;
    }
    if (block_size == 0 || block_size > ZFILE_MAX_BLOCK_SIZE ||
        !is_power_of_2(block_size)) {
        error_setg(errp, "invalid ZFile block size %" PRIu32, block_size);
        return -EINVAL;
    }
    if (algo != ZFILE_ALGO_LZ4 && algo != ZFILE_ALGO_ZSTD) {
        error_setg(errp, "unknown ZFile compression algorithm %" PRIu8, algo);
        return -EINVAL;
    }
#ifndef CONFIG_LZ4
    if (algo == ZFILE_ALGO_LZ4) {
        error_setg(errp, "LZ4-compressed layer, but qemu was compiled "
                         "without lz4 support");
        return -ENOTSUP;
    }
#endif
#ifndef CONFIG_ZSTD
    if (algo == ZFILE_ALGO_ZSTD) {
        error_setg(errp, "ZSTD-compressed layer, but qemu was compiled "
                         "without zstd support");
        return -ENOTSUP;
    }
#endif

    if (index_size > ZFILE_MAX_INDEX_ENTRIES) {
        error_setg(errp, "ZFile index size %" PRIu64 " exceeds maximum "
                         "%lld", index_size, ZFILE_MAX_INDEX_ENTRIES);
        return -EINVAL;
    }
    /*
     * order the checks so each subtraction is guarded by the previous
     * condition (no unsigned wraparound on adversarial values)
     */
    if (index_offset < ZFILE_HT_SPACE ||
        index_offset > (uint64_t)zfile_end - end_reserve ||
        index_size * 4 > (uint64_t)zfile_end - end_reserve - index_offset) {
        error_setg(errp, "invalid ZFile jump table location");
        return -EINVAL;
    }
    if ((index_size - 1) * block_size >= original_file_size ||
        original_file_size > index_size * block_size) {
        error_setg(errp, "ZFile block count does not match content size");
        return -EINVAL;
    }

    zf->index_offset = index_offset;
    zf->index_size = index_size;
    zf->original_file_size = original_file_size;
    zf->block_size = block_size;
    zf->algo = algo;
    zf->verify = verify;

    table = qemu_try_blockalign(l->child->bs,
                                index_size * sizeof(uint32_t));
    if (!table) {
        return -ENOMEM;
    }
    ret = overlaybd_zfile_load_table(l, table, errp);
    if (ret < 0) {
        goto out;
    }
    /*
     * like the reference implementation, the jump table CRC is only
     * checked when the digest flag is set (it is garbage otherwise)
     */
    if ((flags & ZFILE_FLAG_DIGEST) && le32_to_cpu(ht.index_crc) !=
        overlaybd_crc32c(0, table, index_size * sizeof(uint32_t))) {
        error_setg(errp, "ZFile jump table CRC mismatch");
        ret = -EIO;
        goto out;
    }

    ret = overlaybd_zfile_build_table(zf, table,
                                      ZFILE_HT_SPACE +
                                      le32_to_cpu(ht.opt.dict_size), errp);
    if (ret < 0) {
        goto out;
    }
    if (overlaybd_zfile_block_off(zf, zf->index_size) > zf->index_offset) {
        error_setg(errp, "ZFile data region overlaps the jump table");
        ret = -EINVAL;
        goto out;
    }

    l->view_size = zf->original_file_size;
    ret = 0;
out:
    qemu_vfree(table);
    return ret;
}

/*
 * Forward-only cursor into a QEMUIOVector. Seeding it and committing each
 * block's bytes are the same advance, so they share one. The blocks of a
 * request are written contiguously in increasing order, so a full pass costs
 * O(niov) rather than the O(niov * nblocks) of qemu_iovec_subvec_niov(),
 * which rescans from iov[0] -- expensive for the one-iovec-per-guest-page
 * qiov that virtio-blk normally hands us.
 */
typedef struct OverlaybdQiovCursor {
    struct iovec *iov;
    struct iovec *end;
    uint8_t *cur;
    size_t left;
} OverlaybdQiovCursor;

static void overlaybd_qiov_cursor_advance(OverlaybdQiovCursor *c, size_t n)
{
    while (c->cur && n >= c->left) {
        n -= c->left;
        if (++c->iov >= c->end) {
            c->cur = NULL;
            c->left = 0;
            return;
        }
        c->cur = (uint8_t *)c->iov->iov_base;
        c->left = c->iov->iov_len;
    }
    if (c->cur) {
        c->cur += n;
        c->left -= n;
    }
}

/*
 * Point @c at byte 0 of @qiov.  @qiov is NULL on the buf path, which
 * leaves the cursor exhausted, so every test of it fails closed.
 */
static void overlaybd_qiov_cursor_init(OverlaybdQiovCursor *c,
                                       QEMUIOVector *qiov)
{
    c->iov = qiov ? qiov->iov : NULL;
    c->end = qiov ? qiov->iov + qiov->niov : NULL;
    c->cur = c->iov < c->end ? (uint8_t *)c->iov->iov_base : NULL;
    c->left = c->cur ? c->iov->iov_len : 0;
}

/*
 * All I/O goes through bdrv_pread() (a mixed wrapper), so this is
 * callable from both GS and coroutine contexts.
 */
static int coroutine_mixed_fn GRAPH_RDLOCK
overlaybd_zfile_read(OverlaybdLayer *l, uint64_t off, uint64_t bytes,
                     void *buf, QEMUIOVector *qiov, size_t qiov_off)
{
    OverlaybdZfile *zf = &l->zf;
    uint32_t bsz = zf->block_size;
    uint64_t first, last, base, need;
    uint8_t *cbuf = NULL;
    uint8_t *scratch = NULL;
    OverlaybdQiovCursor c;
    int ret;

    assert(off + bytes <= zf->original_file_size);

    /*
     * also keeps "off + bytes - 1" below from wrapping when both are 0,
     * which a caller reading an empty index can legitimately ask for
     */
    if (bytes == 0) {
        return 0;
    }

    first = off / bsz;
    last = (off + bytes - 1) / bsz;
    /*
     * One read covers the compressed bytes of every block the request spans,
     * so the loop below never refills. A hostile jump table can still make
     * @need many times larger than @bytes, hence qemu_try_blockalign() rather
     * than the aborting qemu_blockalign(); aligning to the child's
     * opt_mem_alignment lets an already-aligned request reach the protocol
     * driver without being bounced.
     */
    base = overlaybd_zfile_block_off(zf, first);
    need = overlaybd_zfile_block_off(zf, last + 1) - base;

    /*
     * One allocation for both: the compressed window, then the one-block
     * decompression scratch behind it. bdrv_pread() writes only the first
     * @need bytes so the two never overlap, and only the window goes to the
     * block layer, so only its start needs the alignment.
     */
    cbuf = qemu_try_blockalign(l->child->bs, need + bsz);
    if (!cbuf) {
        return -ENOMEM;
    }
    scratch = cbuf + need;

    ret = bdrv_pread(l->child, l->base_offset + base, need, cbuf, 0);
    if (ret < 0) {
        goto out;
    }

    overlaybd_qiov_cursor_init(&c, qiov);
    overlaybd_qiov_cursor_advance(&c, qiov_off);

    for (uint64_t bi = first; bi <= last; bi++) {
        uint64_t bstart = bi * (uint64_t)bsz;
        uint64_t expected = MIN((uint64_t)bsz,
                                zf->original_file_size - bstart);
        uint64_t cp_begin = bstart > off ? 0 : off - bstart;
        uint64_t cp_len = MIN(expected, off + bytes - bstart) - cp_begin;
        uint64_t dst_off = bstart + cp_begin - off;
        uint64_t off_blk = overlaybd_zfile_block_off(zf, bi);
        uint64_t span = overlaybd_zfile_block_off(zf, bi + 1) - off_blk;
        uint64_t clen = span - (zf->verify ? 4 : 0);
        const uint8_t *cdata;
        uint8_t *dst = NULL, *dec;

        cdata = cbuf + (off_blk - base);

        /*
         * BlockReader::crc32_code(), zfile/zfile.cpp: the salted CRC trails
         * the compressed data and covers exactly those bytes.  Upstream
         * retries three times before failing, which cannot help against a
         * read-only child.
         */
        if (zf->verify) {
            uint32_t want = ldl_le_p(cdata + clen);

            if (want != overlaybd_crc32c(ZFILE_CRC_SALT, cdata, clen)) {
                ret = -EIO;
                goto out;
            }
        }

        if (cp_begin == 0 && cp_len == expected) {
            if (buf) {
                dst = (uint8_t *)buf + dst_off;
            } else if (c.cur && cp_len <= c.left) {
                dst = c.cur;
            }
        }
        dec = dst ? dst : scratch;

        ret = -EIO;
#ifdef CONFIG_LZ4
        if (zf->algo == ZFILE_ALGO_LZ4) {
            int r = LZ4_decompress_safe((const char *)cdata, (char *)dec,
                                        clen, expected);

            if (r >= 0 && (uint64_t)r == expected) {
                ret = 0;
            }
        }
#endif
#ifdef CONFIG_ZSTD
        if (zf->algo == ZFILE_ALGO_ZSTD) {
            size_t r = ZSTD_decompress(dec, expected, cdata, clen);

            if (!ZSTD_isError(r) && r == expected) {
                ret = 0;
            }
        }
#endif
        if (ret < 0) {
            goto out;
        }
        if (!dst) {
            if (buf) {
                memcpy((uint8_t *)buf + dst_off, scratch + cp_begin, cp_len);
            } else {
                qemu_iovec_from_buf(qiov, qiov_off + dst_off,
                                    scratch + cp_begin, cp_len);
            }
        }

        overlaybd_qiov_cursor_advance(&c, cp_len);
    }

    ret = 0;
out:
    qemu_vfree(cbuf);
    return ret;
}

static int GRAPH_RDLOCK
overlaybd_pread_view(OverlaybdLayer *l, uint64_t off, int64_t bytes, void *buf)
{
    if (l->is_zfile) {
        return overlaybd_zfile_read(l, off, bytes, buf, NULL, 0);
    }
    return bdrv_pread(l->child, l->base_offset + off, bytes, buf, 0);
}

/*
 * Load and validate the LSMT header/trailer/index of a layer. All
 * metadata comes from the trailer; the header is only a format sniff
 * (real images may have garbage header fields).
 */
static int GRAPH_RDLOCK
overlaybd_lsmt_open(OverlaybdLayer *l, Error **errp)
{
    OverlaybdLSMTHeader ht;
    OverlaybdSegment *index = NULL;
    uint8_t *raw = NULL;
    uint64_t index_offset, index_size, virtual_size, moffset_end;
    uint64_t index_bound;
    uint64_t prev_end = 0;
    size_t count = 0;
    int ret;

    if (l->view_size < 2 * LSMT_HT_SPACE) {
        error_setg(errp, "layer too small to be an LSMT image");
        return -EINVAL;
    }

    ret = overlaybd_pread_view(l, 0, LSMT_HT_SPACE, &ht);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "could not read LSMT header");
        return ret;
    }
    if (!overlaybd_verify_lsmt_block(&ht, false)) {
        error_setg(errp, "invalid LSMT header");
        return -EINVAL;
    }

    /*
     * The header is only a format sniff, so reading the trailer into the
     * same block is safe.  index_bound is where the index must end: the
     * trailer start, which for a bare layer is not view_size - SPACE
     * because bdrv_getlength() over-reports (tar padding, rounding).
     */
    if (l->is_zfile) {
        /* in the ZFile virtual space the content size is exact */
        ret = overlaybd_pread_view(l, l->view_size - LSMT_HT_SPACE,
                                   LSMT_HT_SPACE, &ht);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "could not read LSMT trailer");
            return ret;
        }
        index_bound = l->view_size - LSMT_HT_SPACE;
    } else {
        int64_t trl_off = overlaybd_scan_trailer(l, LSMT_HT_SPACE, false,
                                                 &ht);

        if (trl_off < 0) {
            error_setg(errp, "invalid LSMT trailer (not a sealed data "
                             "layer?)");
            return trl_off < -ENOENT ? trl_off : -EINVAL;
        }
        index_bound = (uint64_t)(trl_off - (int64_t)l->base_offset);
    }
    if (!overlaybd_verify_lsmt_block(&ht, true)) {
        error_setg(errp, "invalid LSMT trailer (not a sealed data layer?)");
        return -EINVAL;
    }

    index_offset = le64_to_cpu(ht.index_offset);
    index_size = le64_to_cpu(ht.index_size);
    virtual_size = le64_to_cpu(ht.virtual_size);

    if (index_size > LSMT_MAX_RO_INDEX_ENTRIES) {
        error_setg(errp, "LSMT index of %" PRIu64 " entries exceeds maximum "
                         "%" PRId64, index_size, LSMT_MAX_RO_INDEX_ENTRIES);
        return -EINVAL;
    }
    if (index_offset < LSMT_HT_SPACE ||
        index_offset > index_bound ||
        index_size * 16 > index_bound - index_offset) {
        error_setg(errp, "invalid LSMT index location");
        return -EINVAL;
    }

    /*
     * both sizes come from the image, so the allocating variants that
     * abort on OOM are not acceptable here; out: frees either pointer
     */
    index = g_try_new(OverlaybdSegment, index_size);
    raw = qemu_try_blockalign(l->child->bs, index_size * 16);
    if (!index || !raw) {
        ret = -ENOMEM;
        goto out;
    }
    ret = overlaybd_pread_view(l, index_offset, index_size * 16, raw);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "could not read LSMT index");
        goto out;
    }

    moffset_end = index_offset / BDRV_SECTOR_SIZE;
    for (uint64_t i = 0; i < index_size; i++) {
        OverlaybdSegment seg, *out;

        overlaybd_decode_entry(raw + i * 16, &seg);
        if (seg.lba == LSMT_INVALID_OFFSET || seg.length == 0) {
            continue;
        }
        if (count > 0 && seg.lba < prev_end) {
            error_setg(errp, "LSMT index is disordered or overlapping");
            ret = -EINVAL;
            goto out;
        }
        if (seg.zeroed) {
            if (seg.moffset < LSMT_MOFFSET_BEGIN ||
                seg.moffset > moffset_end) {
                error_setg(errp, "LSMT zeroed entry moffset out of range");
                ret = -EINVAL;
                goto out;
            }
        } else {
            if (seg.moffset < LSMT_MOFFSET_BEGIN ||
                seg.moffset >= moffset_end ||
                seg.moffset + seg.length > moffset_end) {
                error_setg(errp, "LSMT entry moffset out of range");
                ret = -EINVAL;
                goto out;
            }
        }
        out = &index[count++];
        *out = seg;
        prev_end = seg.lba + seg.length;
    }

    l->index_offset = index_offset;
    l->index = index;
    index = NULL;
    l->index_count = count;
    l->virtual_size = virtual_size;
    ret = 0;
out:
    qemu_vfree(raw);
    g_free(index);
    return ret;
}

static int GRAPH_RDLOCK
overlaybd_detect_tar(OverlaybdLayer *l, int64_t child_size, Error **errp)
{
    uint8_t hdr[512];
    uint64_t hoff = 0;
    int ret;

    l->base_offset = 0;
    l->view_size = child_size;

    if (child_size < 1024) {
        return 0;
    }
    ret = bdrv_pread(l->child, 0, sizeof(hdr), hdr, 0);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "could not read layer file header");
        return ret;
    }
    if (memcmp(hdr + 257, "ustar", 5) != 0) {
        return 0;
    }
    if (!overlaybd_is_tar_header(hdr)) {
        error_setg(errp, "unsupported tar layer layout");
        return -EINVAL;
    }
    if (hdr[156] == 'x') {
        /*
         * pax extended header: its size field gives the length of the
         * attribute block that precedes the real ustar header
         */
        char size[13];
        unsigned long long pax_size;

        memcpy(size, hdr + 124, 12);
        size[12] = 0;
        if (qemu_strtou64(size, NULL, 8, &pax_size) < 0) {
            pax_size = 0;
        }
        hoff = ZFILE_HT_SPACE +
               DIV_ROUND_UP(pax_size, ZFILE_HT_SPACE) * ZFILE_HT_SPACE;
        if (hoff + 2 * ZFILE_HT_SPACE > (uint64_t)child_size) {
            error_setg(errp, "tar layer too small");
            return -EINVAL;
        }
        ret = bdrv_pread(l->child, hoff, sizeof(hdr), hdr, 0);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "could not read tar file header");
            return ret;
        }
        if (memcmp(hdr + 257, "ustar", 5) != 0 ||
            (hdr[156] != '0' && hdr[156] != '\0')) {
            error_setg(errp, "unsupported tar layer layout");
            return -EINVAL;
        }
    }

    l->base_offset = hoff + ZFILE_HT_SPACE;
    l->view_size = child_size - l->base_offset;
    if (hdr[124] < 0x80) {
        /*
         * prefer the tar entry size: it is authoritative when the tar
         * entry is padded
         */
        char size[13];
        unsigned long long tar_size;

        memcpy(size, hdr + 124, 12);
        size[12] = 0;
        if (!qemu_strtou64(size, NULL, 8, &tar_size) &&
            tar_size > 0 && tar_size <= l->view_size) {
            l->view_size = tar_size;
        }
    }
    return 0;
}

static int GRAPH_RDLOCK
overlaybd_open_layer(OverlaybdLayer *l, Error **errp)
{
    uint8_t buf[512];
    int64_t child_size;
    int ret;

    child_size = bdrv_getlength(l->child->bs);
    if (child_size < 0) {
        error_setg_errno(errp, -child_size, "could not get layer file size");
        return child_size;
    }

    ret = overlaybd_detect_tar(l, child_size, errp);
    if (ret < 0) {
        return ret;
    }
    if (l->view_size < 2 * ZFILE_HT_SPACE) {
        error_setg(errp, "layer file too small");
        return -EINVAL;
    }

    ret = bdrv_pread(l->child, l->base_offset, sizeof(buf), buf, 0);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "could not read layer file header");
        return ret;
    }
    if (overlaybd_is_zfile_header(buf) &&
        (ldq_le_p(buf + 32) & ZFILE_FLAG_HEADER)) {
        l->is_zfile = true;
        ret = overlaybd_zfile_open(l, errp);
        if (ret < 0) {
            return ret;
        }
    }

    return overlaybd_lsmt_open(l, errp);
}

/*
 * First index whose entry covers or follows @sector: entries are sorted
 * and non-overlapping, so entry ends are monotonically increasing.
 * This matches the reference implementation's lower_bound(), which
 * searches by entry end so that an entry covering @sector is found.
 */
static size_t overlaybd_find_segment(const OverlaybdSegment *a, size_t n,
                                     uint64_t sector)
{
    size_t lo = 0, hi = n;

    while (lo < hi) {
        size_t mid = (lo + hi) / 2;

        if (a[mid].lba + a[mid].length <= sector) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

/*
 * Merge the per-layer indexes into s->merged: layers[0] is the topmost
 * layer; a layer's mapping wins its whole extent, lower layers fill the
 * holes. Entries pushed for [begin, end) are edge-trimmed to that range.
 */
static bool overlaybd_merge_level(BDRVOverlaybdState *s, int level,
                                  uint64_t begin, uint64_t end, size_t *count)
{
    OverlaybdLayer *l;
    size_t i, size0 = *count;
    uint64_t begin0 = begin;

    if (level >= s->nb_layers || begin >= end) {
        return true;
    }
    l = &s->layers[level];
    for (i = overlaybd_find_segment(l->index, l->index_count, begin);
         i < l->index_count && l->index[i].lba < end; i++) {
        OverlaybdSegment *e = &l->index[i];

        if (e->lba > begin &&
            !overlaybd_merge_level(s, level + 1, begin, e->lba, count)) {
            return false;
        }
        if (*count == s->merged_cap) {
            /*
             * One input entry can be emitted several times when it is
             * split by ranges covered (or zeroed) at upper layers, so the
             * output can exceed the sum of the input indexes -- hence the
             * cap, which upstream enforces at the same point.  Grow into a
             * temporary so that a failed realloc leaves s->merged valid
             * for overlaybd_close() to free.
             */
            size_t cap = s->merged_cap ? s->merged_cap * 2 : 64;
            OverlaybdSegment *grown;

            if (cap > LSMT_MAX_MERGED_ENTRIES) {
                return false;
            }
            grown = g_try_renew(OverlaybdSegment, s->merged, cap);
            if (!grown) {
                return false;
            }
            s->merged = grown;
            s->merged_cap = cap;
        }
        s->merged[*count] = *e;
        s->merged[*count].layer = level;
        (*count)++;
        begin = e->lba + e->length;
    }
    if (begin < end &&
        !overlaybd_merge_level(s, level + 1, begin, end, count)) {
        return false;
    }
    if (*count > size0) {
        OverlaybdSegment *first = &s->merged[size0];
        OverlaybdSegment *last = &s->merged[*count - 1];

        if (first->lba < begin0) {
            uint64_t delta = begin0 - first->lba;

            first->length -= delta;
            first->lba = begin0;
            if (!first->zeroed) {
                first->moffset += delta;
            }
        }
        if (last->lba + last->length > end) {
            last->length = end - last->lba;
        }
    }
    return true;
}

static int coroutine_fn GRAPH_RDLOCK
overlaybd_co_preadv(BlockDriverState *bs, int64_t offset, int64_t bytes,
                    QEMUIOVector *qiov, BdrvRequestFlags flags)
{
    BDRVOverlaybdState *s = bs->opaque;
    uint64_t cur = offset >> BDRV_SECTOR_BITS;
    uint64_t end = cur + (bytes >> BDRV_SECTOR_BITS);
    size_t done = 0;
    int ret = 0;

    assert(QEMU_IS_ALIGNED(offset, BDRV_SECTOR_SIZE));
    assert(QEMU_IS_ALIGNED(bytes, BDRV_SECTOR_SIZE));

    while (cur < end) {
        OverlaybdSegment *e;
        OverlaybdLayer *l;
        uint64_t run, run_end;
        size_t idx = overlaybd_find_segment(s->merged, s->merged_count, cur);

        if (idx == s->merged_count || s->merged[idx].lba >= end) {
            qemu_iovec_memset(qiov, done, 0, (end - cur) * BDRV_SECTOR_SIZE);
            break;
        }
        e = &s->merged[idx];
        if (e->lba > cur) {
            run = (e->lba - cur) * BDRV_SECTOR_SIZE;
            qemu_iovec_memset(qiov, done, 0, run);
            done += run;
            cur = e->lba;
        }
        run_end = MIN(end, e->lba + e->length);
        run = (run_end - cur) * BDRV_SECTOR_SIZE;
        if (e->zeroed) {
            qemu_iovec_memset(qiov, done, 0, run);
        } else {
            int64_t off = (e->moffset + (cur - e->lba)) * BDRV_SECTOR_SIZE;

            l = &s->layers[e->layer];
            if (l->is_zfile) {
                ret = overlaybd_zfile_read(l, off, run, NULL, qiov, done);
            } else {
                ret = bdrv_co_preadv_part(l->child, l->base_offset + off, run,
                                          qiov, done, 0);
            }
            if (ret < 0) {
                return ret;
            }
        }
        done += run;
        cur = run_end;
    }
    return ret;
}

static void overlaybd_refresh_limits(BlockDriverState *bs, Error **errp)
{
    bs->bl.request_alignment = BDRV_SECTOR_SIZE;
}

static int overlaybd_probe(const uint8_t *buf, int buf_size,
                           const char *filename)
{
    if (buf_size < 24) {
        return 0;
    }
    /*
     * Only a bare LSMT or ZFile magic is distinctive enough to claim a file.
     * A tar-wrapped layer shows just its entry header here, the payload
     * starting past the 512-byte probe window, and tar is far too generic a
     * container to claim on that evidence -- every unrelated archive would
     * then fail to open instead of being read as raw. So a tar-wrapped layer
     * must be named explicitly (-f overlaybd, file.driver=,
     * layers.<n>.driver= or a manifest), all of which bypass probing.
     */
    if (overlaybd_is_lsmt_header(buf) || overlaybd_is_zfile_header(buf)) {
        return 100;
    }
    return 0;
}

static void overlaybd_close(BlockDriverState *bs)
{
    BDRVOverlaybdState *s = bs->opaque;
    int i;

    bdrv_graph_wrlock_drained();
    for (i = 0; i < s->nb_layers; i++) {
        if (s->layers[i].child) {
            bdrv_unref_child(bs, s->layers[i].child);
            s->layers[i].child = NULL;
        }
    }
    bdrv_graph_wrunlock();

    for (i = 0; i < s->nb_layers; i++) {
        g_free(s->layers[i].zf.partial_offset);
        g_free(s->layers[i].zf.deltas);
        g_free(s->layers[i].index);
    }
    g_free(s->layers);
    s->layers = NULL;
    s->nb_layers = 0;
    g_free(s->merged);
    s->merged = NULL;
    s->merged_count = 0;
}

/*
 * Manifest handling: the layer stack comes either from an OCI image manifest
 * (manifest option, or the file child content) or from an explicit
 * bottom-first "layers" array. Of a manifest only "schemaVersion" (must be 2)
 * and "layers" (non-empty, bottom-first) are read, and of each layer
 * descriptor only "digest"; everything else -- the top-level "mediaType",
 * "annotations", "subject" and the whole "config" descriptor -- is ignored,
 * so the image config blob is never fetched.
 *
 * Blobs are named after the root they live under: ROOT/<alg>/<hex> for an OCI
 * image layout's blobs/ or an explicit blob-path, or ROOT/<hex> when ROOT is
 * itself an <alg> directory, i.e. the manifest is a sibling of the layers it
 * names (an OCI layout read from inside blobs/sha256/, or a containerd
 * content store).
 *
 * Specifications:
 *   image manifest (schemaVersion, layers)
 *     https://github.com/opencontainers/image-spec/blob/main/manifest.md
 *   descriptor "digest" grammar, narrowed by overlaybd_parse_digest()
 *     https://github.com/opencontainers/image-spec/blob/main/descriptor.md
 *   the blobs/<alg>/<hex> directory layout
 *     https://github.com/opencontainers/image-spec/blob/main/image-layout.md
 */

typedef enum {
    OBD_BLOB_DIR,
    OBD_BLOB_SIBLING,
} OverlaybdBlobKind;

typedef struct OverlaybdBlobRoot {
    char *path;
    OverlaybdBlobKind kind;
    GPtrArray *blobs;
} OverlaybdBlobRoot;

#define OCI_DIGEST_ALG_MAX 32
#define OCI_DIGEST_HEX_MAX 128
/* an OCI image manifest is a few kilobytes; anything larger is not one */
#define OCI_MANIFEST_MAX_SIZE (1u << 20)

static char *overlaybd_blob_path(const OverlaybdBlobRoot *root,
                                 const char *alg, const char *hex)
{
    switch (root->kind) {
    case OBD_BLOB_SIBLING:
        return g_strdup_printf("%s/%s", root->path, hex);
    case OBD_BLOB_DIR:
        return g_strdup_printf("%s/%s/%s", root->path, alg, hex);
    }
    g_assert_not_reached();
}

/*
 * Validate a digest and split it into algorithm and hex parts. Both are used
 * verbatim as path components, so anything outside the OCI grammar -- in
 * particular "." and "/" -- is rejected. The grammar also allows separators
 * in the algorithm and uppercase hex; both are excluded on purpose, since
 * every tool in practice emits a lowercase "sha256" and narrowing the set
 * costs nothing.
 */
static int overlaybd_parse_digest(const char *digest, char **alg, char **hex,
                                  Error **errp)
{
    static const char alg_chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    static const char hex_chars[] = "0123456789abcdef";
    const char *colon = strchr(digest, ':');
    size_t alen, hlen;

    if (!colon || strchr(colon + 1, ':')) {
        goto invalid;
    }
    alen = colon - digest;
    hlen = strlen(colon + 1);
    if (alen < 1 || alen > OCI_DIGEST_ALG_MAX ||
        hlen < 1 || hlen > OCI_DIGEST_HEX_MAX ||
        strspn(digest, alg_chars) != alen ||
        strspn(colon + 1, hex_chars) != hlen) {
        goto invalid;
    }

    *alg = g_strndup(digest, alen);
    *hex = g_strdup(colon + 1);
    return 0;

invalid:
    error_setg(errp, "invalid layer digest '%s'", digest);
    return -EINVAL;
}

static int overlaybd_reject_url(const char *what, const char *ref,
                                Error **errp)
{
    if (path_has_protocol(ref)) {
        error_setg(errp, "%s '%s' is a URL; only local files are supported",
                   what, ref);
        return -ENOTSUP;
    }
    return 0;
}

/*
 * Put an explicit file child reference under @key, so that the blob is
 * opened without format probing. A bare string reference would instead be
 * looked up as a node name.
 */
static void overlaybd_put_child(QDict *options, const char *key,
                                const char *value)
{
    /* two variables: reassigning a g_autofree one leaks the old value */
    g_autofree char *drv_key = g_strdup_printf("%s.driver", key);
    g_autofree char *ref = g_strdup_printf("%s.filename", key);

    qdict_put_str(options, drv_key, "file");
    qdict_put_str(options, ref, value);
}

static int overlaybd_blob_root_from_option(const char *blob_path,
                                           OverlaybdBlobRoot *root,
                                           Error **errp)
{
    char *end;
    int ret;

    ret = overlaybd_reject_url("blob-path", blob_path, errp);
    if (ret < 0) {
        return ret;
    }

    root->path = g_strdup(blob_path);
    end = root->path + strlen(root->path);
    while (end > root->path && end[-1] == '/') {
        *--end = '\0';
    }
    if (!*root->path) {
        error_setg(errp, "blob-path is empty");
        g_free(root->path);
        root->path = NULL;
        return -EINVAL;
    }
    root->kind = OBD_BLOB_DIR;
    return 0;
}

static int overlaybd_derive_blob_root(const char *manifest, const char *alg,
                                      OverlaybdBlobRoot *root, Error **errp)
{
    g_autofree char *dir = NULL;
    g_autofree char *base = NULL;
    g_autofree char *alg_dir = NULL;

    root->path = NULL;

    dir = g_path_get_dirname(manifest);
    base = g_path_get_basename(dir);
    if (strcmp(base, alg) == 0) {
        root->path = g_strdup(dir);
        root->kind = OBD_BLOB_SIBLING;
        return 0;
    }

    /*
     * Otherwise the blobs are grouped by algorithm in a directory beside
     * the manifest, either directly or under the blobs/ of an OCI image
     * layout. The root is that directory's parent, since a directory root
     * composes ROOT/<alg>/<hex>.
     */
    alg_dir = g_build_filename(dir, alg, NULL);
    if (g_file_test(alg_dir, G_FILE_TEST_IS_DIR)) {
        root->path = g_strdup(dir);
    } else {
        root->path = g_build_filename(dir, "blobs", NULL);
        if (!g_file_test(root->path, G_FILE_TEST_IS_DIR)) {
            error_setg(errp, "could not find the blobs of manifest '%s': no "
                       "%s/ or blobs/ directory beside it; give blob-path",
                       manifest, alg);
            g_free(root->path);
            root->path = NULL;
            return -ENOENT;
        }
    }
    root->kind = OBD_BLOB_DIR;
    return 0;
}

static int overlaybd_parse_manifest(const char *content, QDict **manifest_out,
                                    Error **errp)
{
    QDict *manifest;
    QObject *obj;
    int64_t version;

    obj = qobject_from_json(content, errp);
    if (!obj) {
        return -EINVAL;
    }
    manifest = qobject_to(QDict, obj);
    if (!manifest) {
        error_setg(errp, "overlaybd manifest root is not a JSON object");
        qobject_unref(obj);
        return -EINVAL;
    }

    version = qdict_get_try_int(manifest, "schemaVersion", 0);
    if (version != 2) {
        error_setg(errp, "unsupported OCI schemaVersion %" PRId64 ", "
                   "expected 2", version);
        qobject_unref(obj);
        return -ENOTSUP;
    }
    if (!qobject_to(QList, qdict_get(manifest, "layers"))) {
        error_setg(errp, "overlaybd manifest has no \"layers\" array");
        qobject_unref(obj);
        return -EINVAL;
    }
    *manifest_out = manifest;
    return 0;
}

static int overlaybd_paths_from_manifest(QDict *manifest, const char *location,
                                         OverlaybdBlobRoot *root, Error **errp)
{
    QList *layers = qobject_to(QList, qdict_get(manifest, "layers"));
    const QListEntry *entry;
    int i = 0, ret;

    for (entry = qlist_first(layers); entry; entry = qlist_next(entry)) {
        QDict *desc = qobject_to(QDict, entry->value);
        const char *digest = desc ? qdict_get_try_str(desc, "digest") : NULL;
        g_autofree char *alg = NULL;
        g_autofree char *hex = NULL;

        if (!digest) {
            error_setg(errp, "layers[%d] has no digest", i);
            return -EINVAL;
        }
        ret = overlaybd_parse_digest(digest, &alg, &hex, errp);
        if (ret < 0) {
            return ret;
        }
        if (i == 0 && !root->path) {
            ret = overlaybd_derive_blob_root(location, alg, root, errp);
            if (ret < 0) {
                return ret;
            }
        }
        g_ptr_array_add(root->blobs, overlaybd_blob_path(root, alg, hex));
        i++;
    }
    if (i == 0) {
        error_setg(errp, "overlaybd manifest has no layers");
        return -EINVAL;
    }
    return 0;
}

static int overlaybd_layers_from_options(QDict *options, int nb_layers,
                                         Error **errp)
{
    int i;

    for (i = 0; i < nb_layers; i++) {
        char key[32];
        const char *val;
        int n, ret;

        n = snprintf(key, sizeof(key), "layers.%d", i);
        assert(n < (int)sizeof(key));

        val = qdict_get_try_str(options, key);
        if (val) {
            g_autofree char *path = g_strdup(val);

            ret = overlaybd_reject_url("layer", path, errp);
            if (ret < 0) {
                return ret;
            }
            qdict_del(options, key);
            overlaybd_put_child(options, key, path);
        }
    }
    return 0;
}

static int overlaybd_open_layers(BlockDriverState *bs, QDict *options,
                                 const OverlaybdBlobRoot *root, int nb_layers,
                                 BdrvChild *file_layer, Error **errp)
{
    BDRVOverlaybdState *s = bs->opaque;
    int ret;
    int i;

    if (nb_layers > LSMT_MAX_LAYERS) {
        error_setg(errp, "too many overlaybd layers (%d, maximum %d)",
                   nb_layers, LSMT_MAX_LAYERS);
        return -EINVAL;
    }

    s->layers = g_new0(OverlaybdLayer, nb_layers);
    s->nb_layers = nb_layers;

    if (file_layer) {
        s->layers[0].child = file_layer;
    } else {
        for (i = 0; i < nb_layers; i++) {
            BdrvChild *child;
            char key[32];
            int n;

            if (root) {
                n = snprintf(key, sizeof(key), "layer.%d", i);
                assert(n < (int)sizeof(key));
                overlaybd_put_child(options, key, root->blobs->pdata[i]);
            } else {
                n = snprintf(key, sizeof(key), "layers.%d", i);
                assert(n < (int)sizeof(key));
            }

            child = bdrv_open_child(NULL, options, key, bs, &child_of_bds,
                                    BDRV_CHILD_DATA | BDRV_CHILD_METADATA,
                                    false, errp);
            if (!child) {
                ret = -EINVAL;
                goto fail;
            }
            /*
             * fill top-first: the last path (bottom-first) is the top
             * layer
             */
            s->layers[nb_layers - 1 - i].child = child;
        }
    }

    bdrv_graph_rdlock_main_loop();
    for (i = 0; i < nb_layers; i++) {
        ret = overlaybd_open_layer(&s->layers[i], errp);
        if (ret < 0) {
            goto unlock_fail;
        }
    }

    s->merged_cap = 64;
    s->merged = g_new(OverlaybdSegment, s->merged_cap);
    if (!overlaybd_merge_level(s, 0, 0, UINT64_MAX, &s->merged_count)) {
        error_setg(errp, "failed to merge overlaybd layer indexes");
        ret = -EINVAL;
        goto unlock_fail;
    }

    s->virtual_size = 0;
    for (i = 0; i < nb_layers; i++) {
        if (s->layers[i].virtual_size != 0) {
            s->virtual_size = s->layers[i].virtual_size;
            break;
        }
    }
    if (s->virtual_size == 0) {
        error_setg(errp, "overlaybd image has zero virtual size");
        ret = -EINVAL;
        goto unlock_fail;
    }
    /* DIV_ROUND_UP() below would wrap around to a zero-length image */
    if (s->virtual_size > UINT64_MAX - (BDRV_SECTOR_SIZE - 1)) {
        error_setg(errp, "overlaybd image virtual size %" PRIu64 " is too "
                   "large", s->virtual_size);
        ret = -EINVAL;
        goto unlock_fail;
    }

    bs->total_sectors = DIV_ROUND_UP(s->virtual_size, BDRV_SECTOR_SIZE);
    bdrv_graph_rdunlock_main_loop();
    return 0;

unlock_fail:
    bdrv_graph_rdunlock_main_loop();
fail:
    return ret;
}

static int overlaybd_open(BlockDriverState *bs, QDict *options, int flags,
                          Error **errp)
{
    g_autofree char *manifest_opt = NULL;
    g_autofree char *blob_path = NULL;
    OverlaybdBlobRoot root = { 0 };
    const OverlaybdBlobRoot *blobs = NULL;
    QDict *manifest = NULL;
    const char *location = NULL;
    char *content = NULL;
    BdrvChild *file_layer = NULL;
    bool use_file_child;
    int nb_layers = 0;
    int ret;

    GLOBAL_STATE_CODE();

    bdrv_graph_rdlock_main_loop();
    ret = bdrv_apply_auto_read_only(bs, NULL, errp);
    bdrv_graph_rdunlock_main_loop();
    if (ret < 0) {
        return ret;
    }

    qdict_flatten(options);

    /* copy before deleting: qdict_get_try_str() borrows from the entry */
    manifest_opt = g_strdup(qdict_get_try_str(options, "manifest"));
    blob_path = g_strdup(qdict_get_try_str(options, "blob-path"));
    qdict_del(options, "manifest");
    qdict_del(options, "blob-path");
    use_file_child = qdict_haskey(options, "file");
    nb_layers = qdict_array_entries(options, "layers.");
    if (nb_layers < 0) {
        error_setg(errp, "option layers is not a valid array");
        return -EINVAL;
    }
    if ((manifest_opt != NULL) + use_file_child + (nb_layers > 0) > 1) {
        error_setg(errp, "only one of manifest, file or layers may be given");
        return -EINVAL;
    }
    if (!manifest_opt && !use_file_child && nb_layers == 0) {
        error_setg(errp, "either manifest, file or layers is required");
        return -EINVAL;
    }
    if (blob_path && !manifest_opt && !use_file_child) {
        error_setg(errp, "blob-path requires a manifest");
        return -EINVAL;
    }

    if (manifest_opt) {
        gsize len;

        ret = overlaybd_reject_url("manifest", manifest_opt, errp);
        if (ret < 0) {
            return ret;
        }
        if (!g_file_get_contents(manifest_opt, &content, &len, NULL)) {
            error_setg(errp, "could not read overlaybd manifest '%s'",
                       manifest_opt);
            return -ENOENT;
        }
        location = manifest_opt;
    } else if (use_file_child) {
        uint8_t sniff[512];
        const uint8_t *p, *end;
        int64_t len;

        ret = bdrv_open_file_child(NULL, options, "file", bs, errp);
        if (ret < 0) {
            return ret;
        }
        /*
         * The file child is either an OCI image manifest or a single layer,
         * possibly inside a tar and/or ZFile shell. No layer magic starts
         * with '{', so a JSON document is a manifest and anything else is a
         * layer; overlaybd_open_layer() peels the shells and requires an
         * LSMT core.
         */
        bdrv_graph_rdlock_main_loop();
        len = bdrv_getlength(bs->file->bs);
        if (len < 0) {
            bdrv_graph_rdunlock_main_loop();
            error_setg_errno(errp, -len, "could not get the file child size");
            ret = len;
            goto out;
        }
        memset(sniff, 0, sizeof(sniff));
        ret = bdrv_pread(bs->file, 0, MIN((uint64_t)len, sizeof(sniff)),
                         sniff, 0);
        if (ret < 0) {
            bdrv_graph_rdunlock_main_loop();
            error_setg_errno(errp, -ret, "could not read the file child");
            goto out;
        }
        location = bs->file->bs->filename;
        end = sniff + MIN((uint64_t)len, sizeof(sniff));
        for (p = sniff; p < end && g_ascii_isspace(*p); p++) {
        }
        if (p == end || *p != '{') {
            file_layer = bs->file;
            nb_layers = 1;
            bdrv_graph_rdunlock_main_loop();
        } else {
            /*
             * @len comes from the child rather than from overlaybd, so it
             * cannot be handed to g_malloc() unchecked; a real manifest is
             * a few kilobytes.
             */
            if (len > OCI_MANIFEST_MAX_SIZE) {
                bdrv_graph_rdunlock_main_loop();
                error_setg(errp, "file child starts like a JSON manifest but "
                           "is %" PRId64 " bytes, above the %u byte limit",
                           len, OCI_MANIFEST_MAX_SIZE);
                ret = -EINVAL;
                goto out;
            }
            content = g_malloc(len + 1);
            ret = bdrv_pread(bs->file, 0, len, content, 0);
            if (ret >= 0) {
                content[len] = 0;
                ret = 0;
            } else {
                error_setg_errno(errp, -ret,
                                 "could not read overlaybd manifest");
            }
            bdrv_graph_rdunlock_main_loop();
            if (ret < 0) {
                goto out;
            }
        }
    }

    if (!file_layer) {
        if (content) {
            ret = overlaybd_parse_manifest(content, &manifest, errp);
            if (ret < 0) {
                goto out;
            }
            root.blobs = g_ptr_array_new_with_free_func(g_free);
            if (blob_path) {
                ret = overlaybd_blob_root_from_option(blob_path, &root, errp);
                if (ret < 0) {
                    goto out;
                }
            }
            ret = overlaybd_paths_from_manifest(manifest, location, &root,
                                                errp);
            if (ret < 0) {
                goto out;
            }
            nb_layers = root.blobs->len;
            blobs = &root;
        } else {
            ret = overlaybd_layers_from_options(options, nb_layers, errp);
            if (ret < 0) {
                goto out;
            }
        }
    }

    ret = overlaybd_open_layers(bs, options, blobs, nb_layers, file_layer,
                                errp);

out:
    g_free(root.path);
    if (root.blobs) {
        g_ptr_array_free(root.blobs, TRUE);
    }
    qobject_unref(manifest);
    g_free(content);
    if (ret < 0) {
        overlaybd_close(bs);
    }
    return ret;
}

static BlockDriver bdrv_overlaybd = {
    .format_name         = "overlaybd",
    .instance_size       = sizeof(BDRVOverlaybdState),
    .bdrv_probe          = overlaybd_probe,
    .bdrv_open           = overlaybd_open,
    .bdrv_child_perm     = bdrv_default_perms,
    .bdrv_refresh_limits = overlaybd_refresh_limits,
    .bdrv_co_preadv      = overlaybd_co_preadv,
    .bdrv_close          = overlaybd_close,
    .is_format           = true,
};

static void bdrv_overlaybd_init(void)
{
    bdrv_register(&bdrv_overlaybd);
}

block_init(bdrv_overlaybd_init);
