/*
 * QEMU PowerNV PNOR simple model
 *
 * Copyright (c) 2015-2019, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "sysemu/block-backend.h"
#include "sysemu/blockdev.h"
#include "hw/loader.h"
#include "hw/ppc/ffs.h"
#include "hw/ppc/pnv_pnor.h"
#include "hw/qdev-properties.h"
#include "libxz/xz.h"

static uint32_t ffs_checksum(void *data, size_t size)
{
    uint32_t i, csum = 0;

    for (i = csum = 0; i < (size / 4); i++) {
        csum ^= ((uint32_t *)data)[i];
    }
    return csum;
}

static int ffs_check_convert_header(struct ffs_hdr *dst, struct ffs_hdr *src)
{
    dst->magic = be32_to_cpu(src->magic);
    if (dst->magic != FFS_MAGIC) {
        return -1;
    }
    dst->version = be32_to_cpu(src->version);
    if (dst->version != FFS_VERSION_1) {
        return -1;
    }
    if (ffs_checksum(src, FFS_HDR_SIZE) != 0) {
        return -1;
    }
    dst->size = be32_to_cpu(src->size);
    dst->entry_size = be32_to_cpu(src->entry_size);
    dst->entry_count = be32_to_cpu(src->entry_count);
    dst->block_size = be32_to_cpu(src->block_size);
    dst->block_count = be32_to_cpu(src->block_count);

    return 0;
}

static int ffs_check_convert_entry(struct ffs_entry *dst, struct ffs_entry *src)
{
    if (ffs_checksum(src, FFS_ENTRY_SIZE) != 0) {
        return -1;
    }

    memcpy(dst->name, src->name, sizeof(dst->name));
    dst->base = be32_to_cpu(src->base);
    dst->size = be32_to_cpu(src->size);
    dst->pid = be32_to_cpu(src->pid);
    dst->id = be32_to_cpu(src->id);
    dst->type = be32_to_cpu(src->type);
    dst->flags = be32_to_cpu(src->flags);
    dst->actual = be32_to_cpu(src->actual);
    dst->user.datainteg = be16_to_cpu(src->user.datainteg);

    return 0;
}

static int decompress(void *dst, size_t dst_size, void *src, size_t src_size,
                      Error **errp)
{
    struct xz_dec *s;
    struct xz_buf b;
    int ret = 0;

    /* Initialize the xz library first */
    xz_crc32_init();
    s = xz_dec_init(XZ_SINGLE, 0);
    if (!s) {
        error_report("failed to initialize xz");
        return -1;
    }

    b.in = src;
    b.in_pos = 0;
    b.in_size = src_size;
    b.out = dst;
    b.out_pos = 0;
    b.out_size = dst_size;

    /* Start decompressing */
    ret = xz_dec_run(s, &b);
    if (ret != XZ_STREAM_END) {
        error_setg(errp, "failed to decompress : %d", ret);
        ret = -1;
    } else {
        ret = 0;
    }

    /* Clean up memory */
    xz_dec_end(s);
    return ret;
}

int pnv_pnor_load_skiboot(PnvPnor *s, hwaddr addr, size_t max_size,
                          Error **errp)
{
    int rc;
    void *buffer = g_malloc0(max_size);

    rc = decompress(buffer, max_size, &s->storage[s->skiboot_addr],
                    s->skiboot_size, errp);
    if (rc == 0) {
        rom_add_blob_fixed("pnor.skiboot", buffer, max_size, addr);
    }
    g_free(buffer);
    return rc;
}

#define SECUREBOOT_HEADER_MAGIC    0x17082011
#define SECUREBOOT_HEADER_SIZE     4096

static void pnv_pnor_find_skiboot(PnvPnor *s, Error **errp)
{
    uint8_t *storage = s->storage;
    struct ffs_hdr hdr;
    struct ffs_entry ent;
    uint32_t magic;
    uint32_t i;
    int rc;

    rc = ffs_check_convert_header(&hdr, (struct ffs_hdr *) storage);
    if (rc) {
        error_setg(errp, "bad header");
        return;
    }

    for (i = 0; i < hdr.entry_count; i++) {
        uint32_t esize = hdr.entry_size;
        uint32_t offset = FFS_HDR_SIZE + i * esize;
        struct ffs_entry *src_ent = (struct ffs_entry *)(storage + offset);

        rc = ffs_check_convert_entry(&ent, src_ent);
        if (rc) {
            error_report("bad partition entry %d", i);
            continue;
        }

        if (strcmp("PAYLOAD", ent.name)) {
            continue;
        }

        s->skiboot_addr = ent.base * 0x1000,
        s->skiboot_size = ent.size * 0x1000;

        /* Check for secure boot header */
        magic = be32_to_cpu(*(uint32_t *)&s->storage[s->skiboot_addr]);
        if (magic == SECUREBOOT_HEADER_MAGIC) {
            s->skiboot_addr += SECUREBOOT_HEADER_SIZE;
            s->skiboot_size -= SECUREBOOT_HEADER_SIZE;
        }

        return;
    }

    error_setg(errp, "pnv_pnor: no skiboot partition !?");
}

static uint64_t pnv_pnor_read(void *opaque, hwaddr addr, unsigned size)
{
    PnvPnor *s = PNV_PNOR(opaque);
    uint64_t ret = 0;
    int i;

    for (i = 0; i < size; i++) {
        ret |= (uint64_t) s->storage[addr + i] << (8 * (size - i - 1));
    }

    return ret;
}

static void pnv_pnor_update(PnvPnor *s, int offset, int size)
{
    int offset_end;

    if (s->blk) {
        return;
    }

    offset_end = offset + size;
    offset = QEMU_ALIGN_DOWN(offset, BDRV_SECTOR_SIZE);
    offset_end = QEMU_ALIGN_UP(offset_end, BDRV_SECTOR_SIZE);

    blk_pwrite(s->blk, offset, s->storage + offset,
               offset_end - offset, 0);
}

static void pnv_pnor_write(void *opaque, hwaddr addr, uint64_t data,
                           unsigned size)
{
    PnvPnor *s = PNV_PNOR(opaque);
    int i;

    for (i = 0; i < size; i++) {
        s->storage[addr + i] = (data >> (8 * (size - i - 1))) & 0xFF;
    }
    pnv_pnor_update(s, addr, size);
}

/*
 * TODO: Check endianness: skiboot is BIG, Aspeed AHB is LITTLE, flash
 * is BIG.
 */
static const MemoryRegionOps pnv_pnor_ops = {
    .read = pnv_pnor_read,
    .write = pnv_pnor_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void pnv_pnor_realize(DeviceState *dev, Error **errp)
{
    PnvPnor *s = PNV_PNOR(dev);
    int ret;

    if (s->blk) {
        uint64_t perm = BLK_PERM_CONSISTENT_READ |
                        (blk_is_read_only(s->blk) ? 0 : BLK_PERM_WRITE);
        ret = blk_set_perm(s->blk, perm, BLK_PERM_ALL, errp);
        if (ret < 0) {
            return;
        }

        s->size = blk_getlength(s->blk);
        if (s->size <= 0) {
            error_setg(errp, "failed to get flash size");
            return;
        }

        s->storage = blk_blockalign(s->blk, s->size);

        if (blk_pread(s->blk, 0, s->storage, s->size) != s->size) {
            error_setg(errp, "failed to read the initial flash content");
            return;
        }

        /* Read partitions to validate contents */
        pnv_pnor_find_skiboot(s, errp);
    } else {
        s->storage = blk_blockalign(NULL, s->size);
        memset(s->storage, 0xFF, s->size);
    }

    memory_region_init_io(&s->mmio, OBJECT(s), &pnv_pnor_ops, s,
                          TYPE_PNV_PNOR, s->size);
}

static Property pnv_pnor_properties[] = {
    DEFINE_PROP_UINT32("size", PnvPnor, size, 128 << 20),
    DEFINE_PROP_DRIVE("drive", PnvPnor, blk),
    DEFINE_PROP_END_OF_LIST(),
};

static void pnv_pnor_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pnv_pnor_realize;
    dc->props = pnv_pnor_properties;
}

static const TypeInfo pnv_pnor_info = {
    .name          = TYPE_PNV_PNOR,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PnvPnor),
    .class_init    = pnv_pnor_class_init,
};

static void pnv_pnor_register_types(void)
{
    type_register_static(&pnv_pnor_info);
}

type_init(pnv_pnor_register_types)
