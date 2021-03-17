/*
 * QEMU External Snapshot Utility
 *
 * Copyright Virtuozzo GmbH, 2021
 *
 * Authors:
 *  Andrey Gruzdev   <andrey.gruzdev@virtuozzo.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later. See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "sysemu/block-backend.h"
#include "qemu/coroutine.h"
#include "qemu/cutils.h"
#include "qemu/bitmap.h"
#include "qemu/error-report.h"
#include "io/channel-buffer.h"
#include "migration/qemu-file-channel.h"
#include "migration/qemu-file.h"
#include "migration/savevm.h"
#include "migration/ram.h"
#include "qemu-snap.h"

/* BDRV vmstate area MAGIC for state header */
#define VMSTATE_MAGIC               0x5354564d
/* BDRV vmstate area header size */
#define VMSTATE_HEADER_SIZE         28
/* BDRV vmstate area header eof_pos field offset */
#define VMSTATE_HEADER_EOF_OFFSET   24

/* Alignment of QEMU RAM block on backing storage */
#define BLK_RAM_BLOCK_ALIGN         (1024 * 1024)
/* Max. byte count for page coalescing buffer */
#define PAGE_COALESC_MAX            (512 * 1024)

/* RAM block descriptor */
typedef struct RAMBlockDesc {
    int64_t bdrv_offset;        /* Offset on backing storage */
    int64_t length;             /* RAM block used_length */
    int64_t nr_pages;           /* RAM block page count (length >> page_bits) */

    int64_t last_offset;        /* Last offset sent in precopy */

    char idstr[256];            /* RAM block id string */

    unsigned long *bitmap;      /* Loaded pages bitmap */

    /* Link into ram_list */
    QSIMPLEQ_ENTRY(RAMBlockDesc) next;
} RAMBlockDesc;

/* Reference to the RAM page with block/page tuple */
typedef struct RAMPageRef {
    RAMBlockDesc *block;        /* RAM block containing page */
    int64_t page;               /* Page index in RAM block */
} RAMPageRef;

/* State reflecting RAM part of snapshot */
typedef struct RAMState {
    int64_t page_size;          /* Page size */
    int64_t page_mask;          /* Page mask */
    int page_bits;              /* Page size bits */

    int64_t normal_pages;       /* Total number of normal (non-zero) pages */
    int64_t loaded_pages;       /* Current number of normal pages loaded */

    /* Last RAM block touched by load_send_ram_iterate() */
    RAMBlockDesc *last_block;
    /* Last page touched by load_send_ram_iterate() */
    int64_t last_page;

    /* Last RAM block sent by load_send_ram_iterate() */
    RAMBlockDesc *last_sent_block;

    /* List of RAM blocks */
    QSIMPLEQ_HEAD(, RAMBlockDesc) ram_block_list;
} RAMState;

/* Section handler ops */
typedef struct SectionHandlerOps {
    int (*save_section)(QEMUFile *f, void *opaque, int version_id);
    int (*load_section)(QEMUFile *f, void *opaque, int version_id);
} SectionHandlerOps;

/* Section handler */
typedef struct SectionHandlersEntry {
    const char *idstr;          /* Section id string */
    const int instance_id;      /* Section instance id */
    const int version_id;       /* Max. supported section version id */

    int state_section_id;       /* Section id from migration stream */
    int state_version_id;       /* Version id from migration stream */

    SectionHandlerOps *ops;     /* Section handler callbacks */
} SectionHandlersEntry;

/* Available section handlers */
typedef struct SectionHandlers {
    /* Handler for sections not identified by 'handlers' array */
    SectionHandlersEntry default_entry;
    /* Array of section save/load handlers */
    SectionHandlersEntry entries[];
} SectionHandlers;

#define SECTION_HANDLERS_ENTRY(_idstr, _instance_id, _version_id, _ops) {   \
    .idstr          = _idstr,           \
    .instance_id    = (_instance_id),   \
    .version_id     = (_version_id),    \
    .ops            = (_ops),           \
}

#define SECTION_HANDLERS_END()  { NULL, }

/* Forward declarations */
static int default_save(QEMUFile *f, void *opaque, int version_id);
static int default_load(QEMUFile *f, void *opaque, int version_id);
static int ram_save(QEMUFile *f, void *opaque, int version_id);
static int ram_load(QEMUFile *f, void *opaque, int version_id);
static int save_state_complete(SnapSaveState *sn);
static int coroutine_fn load_send_pages_flush(SnapLoadState *sn);

static RAMState ram_state;

static SectionHandlerOps default_handler_ops = {
    .save_section = default_save,
    .load_section = default_load,
};

static SectionHandlerOps ram_handler_ops = {
    .save_section = ram_save,
    .load_section = ram_load,
};

static SectionHandlers section_handlers = {
    .default_entry =
        SECTION_HANDLERS_ENTRY("default", 0, 0, &default_handler_ops),
    .entries = {
        SECTION_HANDLERS_ENTRY("ram", 0, 4, &ram_handler_ops),
        SECTION_HANDLERS_END(),
    },
};

static SectionHandlersEntry *find_se(const char *idstr, int instance_id)
{
    SectionHandlersEntry *se;

    for (se = section_handlers.entries; se->idstr; se++) {
        if (!strcmp(se->idstr, idstr) && (instance_id == se->instance_id)) {
            return se;
        }
    }
    return NULL;
}

static SectionHandlersEntry *find_se_by_section_id(int section_id)
{
    SectionHandlersEntry *se;

    for (se = section_handlers.entries; se->idstr; se++) {
        if (section_id == se->state_section_id) {
            return se;
        }
    }
    return NULL;
}

static bool check_section_footer(QEMUFile *f, SectionHandlersEntry *se)
{
    uint8_t token;
    int section_id;

    token = qemu_get_byte(f);
    if (token != QEMU_VM_SECTION_FOOTER) {
        error_report("Missing footer for section '%s'", se->idstr);
        return false;
    }

    section_id = qemu_get_be32(f);
    if (section_id != se->state_section_id) {
        error_report("Mismatched section_id in footer for section '%s':"
                     " read_id=%d expected_id=%d",
                se->idstr, section_id, se->state_section_id);
        return false;
    }
    return true;
}

static inline
bool ram_offset_in_block(RAMBlockDesc *block, int64_t offset)
{
    return (block && (offset < block->length));
}

static inline
bool ram_bdrv_offset_in_block(RAMBlockDesc *block, int64_t bdrv_offset)
{
    return (block && (bdrv_offset >= block->bdrv_offset) &&
            (bdrv_offset < block->bdrv_offset + block->length));
}

static inline
int64_t ram_bdrv_from_block_offset(RAMBlockDesc *block, int64_t offset)
{
    if (!ram_offset_in_block(block, offset)) {
        return INVALID_OFFSET;
    }

    return block->bdrv_offset + offset;
}

static inline
int64_t ram_block_offset_from_bdrv(RAMBlockDesc *block, int64_t bdrv_offset)
{
    int64_t offset;

    if (!block) {
        return INVALID_OFFSET;
    }
    offset = bdrv_offset - block->bdrv_offset;
    return offset >= 0 ? offset : INVALID_OFFSET;
}

static RAMBlockDesc *ram_block_by_idstr(const char *idstr)
{
    RAMBlockDesc *block;

    QSIMPLEQ_FOREACH(block, &ram_state.ram_block_list, next) {
        if (!strcmp(idstr, block->idstr)) {
            return block;
        }
    }
    return NULL;
}

static RAMBlockDesc *ram_block_by_bdrv_offset(int64_t bdrv_offset)
{
    RAMBlockDesc *block;

    QSIMPLEQ_FOREACH(block, &ram_state.ram_block_list, next) {
        if (ram_bdrv_offset_in_block(block, bdrv_offset)) {
            return block;
        }
    }
    return NULL;
}

static RAMBlockDesc *ram_block_from_stream(QEMUFile *f, int flags)
{
    static RAMBlockDesc *block;
    char idstr[256];

    if (flags & RAM_SAVE_FLAG_CONTINUE) {
        if (!block) {
            error_report("Corrupted 'ram' section: offset=0x%" PRIx64,
                    qemu_ftell2(f));
            return NULL;
        }
        return block;
    }

    if (!qemu_get_counted_string(f, idstr)) {
        return NULL;
    }
    block = ram_block_by_idstr(idstr);
    if (!block) {
        error_report("Can't find RAM block '%s'", idstr);
        return NULL;
    }
    return block;
}

static int64_t ram_block_next_bdrv_offset(void)
{
    RAMBlockDesc *last_block;
    int64_t offset;

    last_block = QSIMPLEQ_LAST(&ram_state.ram_block_list, RAMBlockDesc, next);
    if (!last_block) {
        return 0;
    }
    offset = last_block->bdrv_offset + last_block->length;
    return ROUND_UP(offset, BLK_RAM_BLOCK_ALIGN);
}

static void ram_block_add(const char *idstr, int64_t size)
{
    RAMBlockDesc *block;

    block = g_new0(RAMBlockDesc, 1);
    block->length = size;
    block->bdrv_offset = ram_block_next_bdrv_offset();
    strcpy(block->idstr, idstr);

    QSIMPLEQ_INSERT_TAIL(&ram_state.ram_block_list, block, next);
}

static void ram_block_list_from_stream(QEMUFile *f, int64_t mem_size)
{
    int64_t total_ram_bytes;

    total_ram_bytes = mem_size;
    while (total_ram_bytes > 0) {
        char idstr[256];
        int64_t size;

        if (!qemu_get_counted_string(f, idstr)) {
            error_report("Can't get RAM block id string in 'ram' "
                         "MEM_SIZE: offset=0x%" PRIx64 " error=%d",
                    qemu_ftell2(f), qemu_file_get_error(f));
            return;
        }
        size = (int64_t) qemu_get_be64(f);

        ram_block_add(idstr, size);
        total_ram_bytes -= size;
    }
    if (total_ram_bytes != 0) {
        error_report("Mismatched MEM_SIZE vs sum of RAM block lengths:"
                     " mem_size=%" PRId64 " block_sum=%" PRId64,
                mem_size, (mem_size - total_ram_bytes));
    }
}

static void ram_block_list_init_bitmaps(void)
{
    RAMBlockDesc *block;

    QSIMPLEQ_FOREACH(block, &ram_state.ram_block_list, next) {
        block->nr_pages = block->length >> ram_state.page_bits;

        block->bitmap = bitmap_new(block->nr_pages);
        bitmap_set(block->bitmap, 0, block->nr_pages);
    }
}

static inline
int64_t ram_block_bitmap_find_next(RAMBlockDesc *block, int64_t start)
{
    return find_next_bit(block->bitmap, block->nr_pages, start);
}

static inline
int64_t ram_block_bitmap_find_next_clear(RAMBlockDesc *block, int64_t start)
{
    return find_next_zero_bit(block->bitmap, block->nr_pages, start);
}

static inline
void ram_block_bitmap_clear(RAMBlockDesc *block, int64_t start, int64_t count)
{
    bitmap_clear(block->bitmap, start, count);
}

static void save_check_file_errors(SnapSaveState *sn, int *res)
{
    /* Check for -EIO that indicates plane EOF */
    if (*res == -EIO) {
        *res = 0;
    }
    /* Check file errors for success and -EINVAL retcodes */
    if (*res >= 0 || *res == -EINVAL) {
        int f_res;

        f_res = qemu_file_get_error(sn->f_fd);
        f_res = (f_res == -EIO) ? 0 : f_res;
        if (!f_res) {
            f_res = qemu_file_get_error(sn->f_vmstate);
        }
        *res = f_res ? f_res : *res;
    }
}

static int ram_save_page(SnapSaveState *sn, uint8_t *page_ptr, int64_t bdrv_offset)
{
    size_t pbuf_usage = sn->ioc_pbuf->usage;
    int page_size = ram_state.page_size;
    int res = 0;

    if (bdrv_offset != sn->last_bdrv_offset ||
        (pbuf_usage + page_size) >= PAGE_COALESC_MAX) {
        if (pbuf_usage) {
            /* Flush coalesced pages to block device */
            res = blk_pwrite(sn->blk, sn->bdrv_offset,
                    sn->ioc_pbuf->data, pbuf_usage, 0);
            res = res < 0 ? res : 0;
        }

        /* Reset coalescing buffer state */
        sn->ioc_pbuf->usage = 0;
        sn->ioc_pbuf->offset = 0;
        /* Switch to new starting bdrv_offset */
        sn->bdrv_offset = bdrv_offset;
    }

    qio_channel_write(QIO_CHANNEL(sn->ioc_pbuf),
            (char *) page_ptr, page_size, NULL);
    sn->last_bdrv_offset = bdrv_offset + page_size;
    return res;
}

static int ram_save_page_flush(SnapSaveState *sn)
{
    size_t pbuf_usage = sn->ioc_pbuf->usage;
    int res = 0;

    if (pbuf_usage) {
        /* Flush coalesced pages to block device */
        res = blk_pwrite(sn->blk, sn->bdrv_offset,
                sn->ioc_pbuf->data, pbuf_usage, 0);
        res = res < 0 ? res : 0;
    }

    /* Reset coalescing buffer state */
    sn->ioc_pbuf->usage = 0;
    sn->ioc_pbuf->offset = 0;

    sn->last_bdrv_offset = INVALID_OFFSET;
    return res;
}

static int ram_save(QEMUFile *f, void *opaque, int version_id)
{
    SnapSaveState *sn = (SnapSaveState *) opaque;
    RAMState *rs = &ram_state;
    int incompat_flags = (RAM_SAVE_FLAG_COMPRESS_PAGE | RAM_SAVE_FLAG_XBZRLE);
    int page_size = rs->page_size;
    int flags = 0;
    int res = 0;

    if (version_id != 4) {
        error_report("Unsupported version %d for 'ram' handler v4", version_id);
        return -EINVAL;
    }

    while (!res && !(flags & RAM_SAVE_FLAG_EOS)) {
        RAMBlockDesc *block;
        int64_t bdrv_offset = INVALID_OFFSET;
        uint8_t *page_ptr;
        ssize_t count;
        int64_t addr;

        addr = qemu_get_be64(f);
        flags = addr & ~rs->page_mask;
        addr &= rs->page_mask;

        if (flags & incompat_flags) {
            error_report("RAM page with incompatible flags: offset=0x%" PRIx64
                         " flags=0x%x", qemu_ftell2(f), flags);
            res = -EINVAL;
            break;
        }

        if (flags & (RAM_SAVE_FLAG_ZERO | RAM_SAVE_FLAG_PAGE)) {
            block = ram_block_from_stream(f, flags);
            bdrv_offset = ram_bdrv_from_block_offset(block, addr);
            if (bdrv_offset == INVALID_OFFSET) {
                error_report("Corrupted RAM page: offset=0x%" PRIx64
                             " page_addr=0x%" PRIx64, qemu_ftell2(f), addr);
                res = -EINVAL;
                break;
            }
        }

        switch (flags & ~RAM_SAVE_FLAG_CONTINUE) {
        case RAM_SAVE_FLAG_MEM_SIZE:
            /* Save position of the section containing list of RAM blocks */
            if (sn->ram_list_pos) {
                error_report("Unexpected RAM page with FLAG_MEM_SIZE:"
                             " offset=0x%" PRIx64 " page_addr=0x%" PRIx64
                             " flags=0x%x", qemu_ftell2(f), addr, flags);
                res = -EINVAL;
                break;
            }
            sn->ram_list_pos = sn->stream_pos;

            /* Fill RAM block list */
            ram_block_list_from_stream(f, addr);
            break;

        case RAM_SAVE_FLAG_ZERO:
            /* Nothing to do with zero page */
            qemu_get_byte(f);
            break;

        case RAM_SAVE_FLAG_PAGE:
            count = qemu_peek_buffer(f, &page_ptr, page_size, 0);
            qemu_file_skip(f, count);
            if (count != page_size) {
                /* I/O error */
                break;
            }

            res = ram_save_page(sn, page_ptr, bdrv_offset);
            /* Update normal page count */
            ram_state.normal_pages++;
            break;

        case RAM_SAVE_FLAG_EOS:
            /* Normal exit */
            break;

        default:
            error_report("RAM page with unknown combination of flags:"
                         " offset=0x%" PRIx64 " page_addr=0x%" PRIx64
                         " flags=0x%x", qemu_ftell2(f), addr, flags);
            res = -EINVAL;
        }

        /* Make additional check for file errors */
        if (!res) {
            res = qemu_file_get_error(f);
        }
    }

    /* Flush page coalescing buffer at RAM_SAVE_FLAG_EOS */
    if (!res) {
        res = ram_save_page_flush(sn);
    }
    return res;
}

static int default_save(QEMUFile *f, void *opaque, int version_id)
{
    SnapSaveState *sn = (SnapSaveState *) opaque;

    if (!sn->ram_pos) {
        error_report("Section with unknown ID before first 'ram' section:"
                     " offset=0x%" PRIx64, sn->stream_pos);
        return -EINVAL;
    }
    if (!sn->device_pos) {
        sn->device_pos = sn->stream_pos;
        /*
         * Save the rest of migration data needed to restore VM state.
         * It is the header, configuration section, first 'ram' section
         * with the list of RAM blocks and device state data.
         */
        return save_state_complete(sn);
    }

    /* Should never get here */
    assert(false);
    return -EINVAL;
}

static int save_state_complete(SnapSaveState *sn)
{
    QEMUFile *f = sn->f_fd;
    int64_t pos;
    int64_t eof_pos;

    /* Current read position */
    pos = qemu_ftell2(f);

    /* Put specific MAGIC at the beginning of saved BDRV vmstate stream */
    qemu_put_be32(sn->f_vmstate, VMSTATE_MAGIC);
    /* Target page size */
    qemu_put_be32(sn->f_vmstate, ram_state.page_size);
    /* Number of normal (non-zero) pages in snapshot */
    qemu_put_be64(sn->f_vmstate, ram_state.normal_pages);
    /* Offset of RAM block list section relative to QEMU_VM_FILE_MAGIC */
    qemu_put_be32(sn->f_vmstate, sn->ram_list_pos);
    /* Offset of first device state section relative to QEMU_VM_FILE_MAGIC */
    qemu_put_be32(sn->f_vmstate, sn->ram_pos);
    /*
     * Put a slot here since we don't really know how
     * long is the rest of migration stream.
     */
    qemu_put_be32(sn->f_vmstate, 0);

    /*
     * At the completion stage we save the leading part of migration stream
     * which contains header, configuration section and the 'ram' section
     * with QEMU_VM_SECTION_FULL type containing the list of RAM blocks.
     *
     * All of this comes before the first QEMU_VM_SECTION_PART token for 'ram'.
     * That QEMU_VM_SECTION_PART token is pointed by sn->ram_pos.
     */
    qemu_put_buffer(sn->f_vmstate, sn->ioc_lbuf->data, sn->ram_pos);
    /*
     * And then we save the trailing part with device state.
     *
     * First we take section header which has already been skipped
     * by QEMUFile but we can get it from sn->section_header.
     */
    qemu_put_buffer(sn->f_vmstate, sn->section_header, (pos - sn->device_pos));

    /* Forward the rest of stream data to the BDRV vmstate file */
    file_transfer_to_eof(sn->f_vmstate, f);
    /* It does qemu_fflush() internally */
    eof_pos = qemu_ftell(sn->f_vmstate);

    /* Hack: simulate negative seek() */
    qemu_update_position(sn->f_vmstate,
            (size_t)(ssize_t) (VMSTATE_HEADER_EOF_OFFSET - eof_pos));
    qemu_put_be32(sn->f_vmstate, eof_pos - VMSTATE_HEADER_SIZE);
    /* Final flush to deliver eof_offset header field */
    qemu_fflush(sn->f_vmstate);

    return 1;
}

static int save_section_config(SnapSaveState *sn)
{
    QEMUFile *f = sn->f_fd;
    uint32_t id_len;

    id_len = qemu_get_be32(f);
    if (id_len > 255) {
        error_report("Corrupted QEMU_VM_CONFIGURATION section");
        return -EINVAL;
    }
    qemu_file_skip(f, id_len);
    return 0;
}

static int save_section_start_full(SnapSaveState *sn)
{
    QEMUFile *f = sn->f_fd;
    SectionHandlersEntry *se;
    int section_id;
    int instance_id;
    int version_id;
    char id_str[256];
    int res;

    /* Read section start */
    section_id = qemu_get_be32(f);
    if (!qemu_get_counted_string(f, id_str)) {
        return qemu_file_get_error(f);
    }
    instance_id = qemu_get_be32(f);
    version_id = qemu_get_be32(f);

    se = find_se(id_str, instance_id);
    if (!se) {
        se = &section_handlers.default_entry;
    } else if (version_id > se->version_id) {
        /* Validate version */
        error_report("Unsupported version %d for '%s' v%d",
                version_id, id_str, se->version_id);
        return -EINVAL;
    }

    se->state_section_id = section_id;
    se->state_version_id = version_id;

    res = se->ops->save_section(f, sn, se->state_version_id);
    /*
     * Positive return value indicates save completion,
     * no need to check section footer.
     */
    if (res) {
        return res;
    }

    /* Finally check section footer */
    if (!check_section_footer(f, se)) {
        return -EINVAL;
    }
    return 0;
}

static int save_section_part_end(SnapSaveState *sn)
{
    QEMUFile *f = sn->f_fd;
    SectionHandlersEntry *se;
    int section_id;
    int res;

    /* First section with QEMU_VM_SECTION_PART type must be the 'ram' section */
    if (!sn->ram_pos) {
        sn->ram_pos = sn->stream_pos;
    }

    section_id = qemu_get_be32(f);
    se = find_se_by_section_id(section_id);
    if (!se) {
        error_report("Unknown section ID: %d", section_id);
        return -EINVAL;
    }

    res = se->ops->save_section(f, sn, se->state_version_id);
    if (res) {
        error_report("Error while saving section: id_str='%s' section_id=%d",
                se->idstr, section_id);
        return res;
    }

    /* Finally check section footer */
    if (!check_section_footer(f, se)) {
        return -EINVAL;
    }
    return 0;
}

static int save_state_header(SnapSaveState *sn)
{
    QEMUFile *f = sn->f_fd;
    uint32_t v;

    /* Validate QEMU MAGIC */
    v = qemu_get_be32(f);
    if (v != QEMU_VM_FILE_MAGIC) {
        error_report("Not a migration stream");
        return -EINVAL;
    }
    v = qemu_get_be32(f);
    if (v == QEMU_VM_FILE_VERSION_COMPAT) {
        error_report("SaveVM v2 format is obsolete");
        return -EINVAL;
    }
    if (v != QEMU_VM_FILE_VERSION) {
        error_report("Unsupported migration stream version");
        return -EINVAL;
    }
    return 0;
}

/* Save snapshot data from incoming migration stream */
int coroutine_fn snap_save_state_main(SnapSaveState *sn)
{
    QEMUFile *f = sn->f_fd;
    uint8_t *buf;
    uint8_t section_type;
    int res = 0;

    res = save_state_header(sn);
    if (res) {
        save_check_file_errors(sn, &res);
        return res;
    }

    while (!res) {
        /* Update current stream position so it points to the section type token */
        sn->stream_pos = qemu_ftell2(f);

        /*
         * Keep some data from the beginning of the section to use it if it appears
         * that we have reached device state section and go into 'default_handler'.
         */
        qemu_peek_buffer(f, &buf, sizeof(sn->section_header), 0);
        memcpy(sn->section_header, buf, sizeof(sn->section_header));

        /* Read section type token */
        section_type = qemu_get_byte(f);

        switch (section_type) {
        case QEMU_VM_CONFIGURATION:
            res = save_section_config(sn);
            break;

        case QEMU_VM_SECTION_FULL:
        case QEMU_VM_SECTION_START:
            res = save_section_start_full(sn);
            break;

        case QEMU_VM_SECTION_PART:
        case QEMU_VM_SECTION_END:
            res = save_section_part_end(sn);
            break;

        case QEMU_VM_EOF:
            /*
             * End of migration stream, but normally we will never really get here
             * since final part of migration stream is a series of QEMU_VM_SECTION_FULL
             * sections holding non-iterable device state. In our case all this
             * state is saved with single call to snap_save_section_start_full()
             * when we first meet unknown section id string.
             */
            res = -EINVAL;
            break;

        default:
            error_report("Unknown section type %d", section_type);
            res = -EINVAL;
        }

        /* Additional check for file errors on success and -EINVAL */
        save_check_file_errors(sn, &res);
    }

    /* Replace positive exit code with 0 */
    sn->status = res < 0 ? res : 0;
    return sn->status;
}

static void load_check_file_errors(SnapLoadState *sn, int *res)
{
    /* Check file errors even on success */
    if (*res >= 0 || *res == -EINVAL) {
        int f_res;

        f_res = qemu_file_get_error(sn->f_fd);
        if (!f_res) {
            f_res = qemu_file_get_error(sn->f_vmstate);
        }
        *res = f_res ? f_res : *res;
    }
}

static int ram_load(QEMUFile *f, void *opaque, int version_id)
{
    int compat_flags = (RAM_SAVE_FLAG_MEM_SIZE | RAM_SAVE_FLAG_EOS);
    int64_t page_mask = ram_state.page_mask;
    int flags = 0;
    int res = 0;

    if (version_id != 4) {
        error_report("Unsupported version %d for 'ram' handler v4", version_id);
        return -EINVAL;
    }

    while (!res && !(flags & RAM_SAVE_FLAG_EOS)) {
        int64_t addr;

        addr = qemu_get_be64(f);
        flags = addr & ~page_mask;
        addr &= page_mask;

        if (flags & ~compat_flags) {
            error_report("RAM page with incompatible flags: offset=0x%" PRIx64
                         " flags=0x%x", qemu_ftell2(f), flags);
            res = -EINVAL;
            break;
        }

        switch (flags) {
        case RAM_SAVE_FLAG_MEM_SIZE:
            /* Fill RAM block list */
            ram_block_list_from_stream(f, addr);
            break;

        case RAM_SAVE_FLAG_EOS:
            /* Normal exit */
            break;

        default:
            error_report("RAM page with unknown combination of flags:"
                         " offset=0x%" PRIx64 " page_addr=0x%" PRIx64
                         " flags=0x%x", qemu_ftell2(f), addr, flags);
            res = -EINVAL;
        }

        /* Check for file errors even if all looks good */
        if (!res) {
            res = qemu_file_get_error(f);
        }
    }
    return res;
}

static int default_load(QEMUFile *f, void *opaque, int version_id)
{
    error_report("Section with unknown ID: offset=0x%" PRIx64,
            qemu_ftell2(f));
    return -EINVAL;
}

static void send_page_header(QEMUFile *f, RAMBlockDesc *block, int64_t offset)
{
    uint8_t hdr_buf[512];
    int hdr_len = 8;

    stq_be_p(hdr_buf, offset);
    if (!(offset & RAM_SAVE_FLAG_CONTINUE)) {
        int id_len;

        id_len = strlen(block->idstr);
        assert(id_len < 256);

        hdr_buf[hdr_len] = id_len;
        memcpy((hdr_buf + hdr_len + 1), block->idstr, id_len);

        hdr_len += 1 + id_len;
    }

    qemu_put_buffer(f, hdr_buf, hdr_len);
}

static void send_zeropage(QEMUFile *f, RAMBlockDesc *block, int64_t offset)
{
    send_page_header(f, block, offset | RAM_SAVE_FLAG_ZERO);
    qemu_put_byte(f, 0);
}

static int send_pages_from_buffer(QEMUFile *f, AioBuffer *buffer)
{
    RAMState *rs = &ram_state;
    int page_size = rs->page_size;
    RAMBlockDesc *block = rs->last_sent_block;
    int64_t bdrv_offset = buffer->status.offset;
    int64_t flags = RAM_SAVE_FLAG_CONTINUE;
    int pages = 0;

    /* Need to switch to the another RAM block? */
    if (!ram_bdrv_offset_in_block(block, bdrv_offset)) {
        /*
         * Lookup RAM block by BDRV offset cause in postcopy we
         * can issue AIO buffer loads from non-contiguous blocks.
         */
        block = ram_block_by_bdrv_offset(bdrv_offset);
        rs->last_sent_block = block;
        /* Reset RAM_SAVE_FLAG_CONTINUE */
        flags = 0;
    }

    for (int offset = 0; offset < buffer->status.count;
            offset += page_size, bdrv_offset += page_size) {
        void *page_buf = buffer->data + offset;
        int64_t addr;

        addr = ram_block_offset_from_bdrv(block, bdrv_offset);

        if (buffer_is_zero(page_buf, page_size)) {
            send_zeropage(f, block, (addr | flags));
        } else {
            send_page_header(f, block,
                    (addr | RAM_SAVE_FLAG_PAGE | flags));
            qemu_put_buffer_async(f, page_buf, page_size, false);

            /* Update non-zero page count */
            rs->loaded_pages++;
        }
        /*
         * AioBuffer is always within a single RAM block so we need
         * to set RAM_SAVE_FLAG_CONTINUE here unconditionally.
         */
        flags = RAM_SAVE_FLAG_CONTINUE;
        pages++;
    }

    /* Need to flush cause we use qemu_put_buffer_async() */
    qemu_fflush(f);
    return pages;
}

static bool find_next_unsent_page(RAMPageRef *p_ref)
{
    RAMState *rs = &ram_state;
    RAMBlockDesc *block = rs->last_block;
    int64_t page = rs->last_page;
    bool found = false;
    bool full_round = false;

    if (!block) {
restart:
        block = QSIMPLEQ_FIRST(&rs->ram_block_list);
        page = 0;
        full_round = true;
    }

    while (!found && block) {
        page = ram_block_bitmap_find_next(block, page);
        if (page >= block->nr_pages) {
            block = QSIMPLEQ_NEXT(block, next);
            page = 0;
            continue;
        }
        found = true;
    }

    if (!found && !full_round) {
        goto restart;
    }

    if (found) {
        p_ref->block = block;
        p_ref->page = page;
    }
    return found;
}

static inline
void get_unsent_page_range(RAMPageRef *p_ref, RAMBlockDesc **block,
        int64_t *offset, int64_t *limit)
{
    int64_t page_limit;

    *block = p_ref->block;
    *offset = p_ref->page << ram_state.page_bits;
    page_limit = ram_block_bitmap_find_next_clear(p_ref->block, (p_ref->page + 1));
    *limit = page_limit << ram_state.page_bits;
}

static AioBufferStatus coroutine_fn load_buffers_task_co(AioBufferTask *task)
{
    SnapLoadState *sn = snap_load_get_state();
    AioBufferStatus ret;
    int count;

    count = blk_pread(sn->blk, task->offset, task->buffer->data, task->size);

    ret.offset = task->offset;
    ret.count = count;

    return ret;
}

static void coroutine_fn load_buffers_fill_queue(SnapLoadState *sn)
{
    RAMState *rs = &ram_state;
    RAMPageRef p_ref;
    RAMBlockDesc *block;
    int64_t offset;
    int64_t limit;
    int64_t pages;

    if (!find_next_unsent_page(&p_ref)) {
        return;
    }

    get_unsent_page_range(&p_ref, &block, &offset, &limit);

    do {
        AioBuffer *buffer;
        int64_t bdrv_offset;
        int size;

        /* Try to acquire next buffer from the pool */
        buffer = aio_pool_try_acquire_next(sn->aio_pool);
        if (!buffer) {
            break;
        }

        bdrv_offset = ram_bdrv_from_block_offset(block, offset);
        assert(bdrv_offset != INVALID_OFFSET);

        /* Get maximum transfer size for current RAM block and offset */
        size = MIN((limit - offset), buffer->size);
        aio_buffer_start_task(buffer, load_buffers_task_co, bdrv_offset, size);

        offset += size;
    } while (offset < limit);

    rs->last_block = block;
    rs->last_page = offset >> rs->page_bits;

    block->last_offset = offset;

    pages = rs->last_page - p_ref.page;
    ram_block_bitmap_clear(block, p_ref.page, pages);
}

static int coroutine_fn load_send_pages(SnapLoadState *sn)
{
    AioBuffer *compl_buffer;
    int pages = 0;

    load_buffers_fill_queue(sn);

    compl_buffer = aio_pool_wait_compl_next(sn->aio_pool);
    if (compl_buffer) {
        /* Check AIO completion status */
        pages = aio_pool_status(sn->aio_pool);
        if (pages < 0) {
            return pages;
        }

        pages = send_pages_from_buffer(sn->f_fd, compl_buffer);
        aio_buffer_release(compl_buffer);
    }

    return pages;
}

static int coroutine_fn load_send_pages_flush(SnapLoadState *sn)
{
    AioBuffer *compl_buffer;

    while ((compl_buffer = aio_pool_wait_compl_next(sn->aio_pool))) {
        int res = aio_pool_status(sn->aio_pool);
        /* Check AIO completion status */
        if (res < 0) {
            return res;
        }

        send_pages_from_buffer(sn->f_fd, compl_buffer);
        aio_buffer_release(compl_buffer);
    }

    return 0;
}

static void send_section_header_part_end(QEMUFile *f, SectionHandlersEntry *se,
        uint8_t section_type)
{
    assert(section_type == QEMU_VM_SECTION_PART ||
           section_type == QEMU_VM_SECTION_END);

    qemu_put_byte(f, section_type);
    qemu_put_be32(f, se->state_section_id);
}

static void send_section_footer(QEMUFile *f, SectionHandlersEntry *se)
{
    qemu_put_byte(f, QEMU_VM_SECTION_FOOTER);
    qemu_put_be32(f, se->state_section_id);
}

#define YIELD_AFTER_MS  500 /* ms */

static int coroutine_fn load_send_ram_iterate(SnapLoadState *sn)
{
    SectionHandlersEntry *se;
    int64_t t_start;
    int tmp_res;
    int res = 1;

    /* Send 'ram' section header with QEMU_VM_SECTION_PART type */
    se = find_se("ram", 0);
    send_section_header_part_end(sn->f_fd, se, QEMU_VM_SECTION_PART);

    t_start = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
    for (int iter = 0; res > 0; iter++) {
        res = load_send_pages(sn);

        if (!(iter & 7)) {
            int64_t t_cur = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
            if ((t_cur - t_start) > YIELD_AFTER_MS) {
                break;
            }
        }
    }

    /* Zero return code means that there're no more pages to send */
    if (res >= 0) {
        res = res ? 0 : 1;
    }

    /* Flush AIO buffers cause some may still remain unsent */
    tmp_res = load_send_pages_flush(sn);
    res = tmp_res ? tmp_res : res;

    /* Send EOS flag before section footer */
    qemu_put_be64(sn->f_fd, RAM_SAVE_FLAG_EOS);
    send_section_footer(sn->f_fd, se);

    qemu_fflush(sn->f_fd);
    return res;
}

static int load_send_leader(SnapLoadState *sn)
{
    qemu_put_buffer(sn->f_fd, (sn->ioc_lbuf->data + VMSTATE_HEADER_SIZE),
            sn->state_device_offset);
    return qemu_file_get_error(sn->f_fd);
}

static int load_send_complete(SnapLoadState *sn)
{
    /* Transfer device state to the output pipe */
    file_transfer_bytes(sn->f_fd, sn->f_vmstate,
            (sn->state_eof - sn->state_device_offset));
    qemu_fflush(sn->f_fd);
    return 1;
}

static int load_section_start_full(SnapLoadState *sn)
{
    QEMUFile *f = sn->f_vmstate;
    int section_id;
    int instance_id;
    int version_id;
    char idstr[256];
    SectionHandlersEntry *se;
    int res;

    /* Read section start */
    section_id = qemu_get_be32(f);
    if (!qemu_get_counted_string(f, idstr)) {
        return qemu_file_get_error(f);
    }
    instance_id = qemu_get_be32(f);
    version_id = qemu_get_be32(f);

    se = find_se(idstr, instance_id);
    if (!se) {
        se = &section_handlers.default_entry;
    } else if (version_id > se->version_id) {
        /* Validate version */
        error_report("Unsupported version %d for '%s' v%d",
                version_id, idstr, se->version_id);
        return -EINVAL;
    }

    se->state_section_id = section_id;
    se->state_version_id = version_id;

    res = se->ops->load_section(f, sn, se->state_version_id);
    if (res) {
        return res;
    }

    /* Finally check section footer */
    if (!check_section_footer(f, se)) {
        return -EINVAL;
    }
    return 0;
}

static int load_setup_ramlist(SnapLoadState *sn)
{
    QEMUFile *f = sn->f_vmstate;
    uint8_t section_type;
    int64_t section_pos;
    int res;

    section_pos = qemu_ftell2(f);

    /* Read section type token */
    section_type = qemu_get_byte(f);
    if (section_type == QEMU_VM_EOF) {
        error_report("Unexpected EOF token: offset=0x%" PRIx64, section_pos);
        return -EINVAL;
    } else if (section_type != QEMU_VM_SECTION_FULL &&
               section_type != QEMU_VM_SECTION_START) {
        error_report("Unexpected section type %d: offset=0x%" PRIx64,
                section_type, section_pos);
        return -EINVAL;
    }

    res = load_section_start_full(sn);
    if (!res) {
        ram_block_list_init_bitmaps();
    }
    return res;
}

static int load_state_header(SnapLoadState *sn)
{
    QEMUFile *f = sn->f_vmstate;
    uint32_t v;

    /* Validate specific MAGIC in vmstate area */
    v = qemu_get_be32(f);
    if (v != VMSTATE_MAGIC) {
        error_report("Not a valid VMSTATE");
        return -EINVAL;
    }
    v = qemu_get_be32(f);
    if (v != ram_state.page_size) {
        error_report("VMSTATE page size not matching target");
        return -EINVAL;
    }

    /* Number of non-zero pages in all RAM blocks */
    ram_state.normal_pages = qemu_get_be64(f);

    /* VMSTATE area offsets, counted from QEMU_FILE_MAGIC */
    sn->state_ram_list_offset = qemu_get_be32(f);
    sn->state_device_offset = qemu_get_be32(f);
    sn->state_eof = qemu_get_be32(f);

    /* Check that offsets are within the limits */
    if ((VMSTATE_HEADER_SIZE + sn->state_device_offset) > INPLACE_READ_MAX ||
        sn->state_device_offset <= sn->state_ram_list_offset) {
        error_report("Corrupted VMSTATE header");
        return -EINVAL;
    }

    /* Skip up to RAM block list section */
    qemu_file_skip(f, sn->state_ram_list_offset);
    return 0;
}

/* Load snapshot data and send it with outgoing migration stream */
int coroutine_fn snap_load_state_main(SnapLoadState *sn)
{
    int res;

    res = load_state_header(sn);
    if (res) {
        goto fail;
    }
    res = load_setup_ramlist(sn);
    if (res) {
        goto fail;
    }
    res = load_send_leader(sn);
    if (res) {
        goto fail;
    }

    do {
        res = load_send_ram_iterate(sn);
        /* Make additional check for file errors */
        load_check_file_errors(sn, &res);
    } while (!res);

    if (res == 1) {
        res = load_send_complete(sn);
    }

fail:
    load_check_file_errors(sn, &res);
    /* Replace positive exit code with 0 */
    return res < 0 ? res : 0;
}

/* Initialize snapshot RAM state */
void snap_ram_init_state(int page_bits)
{
    RAMState *rs = &ram_state;

    memset(rs, 0, sizeof(ram_state));

    rs->page_bits = page_bits;
    rs->page_size = (int64_t) 1 << page_bits;
    rs->page_mask = ~(rs->page_size - 1);

    /* Initialize RAM block list head */
    QSIMPLEQ_INIT(&rs->ram_block_list);
}

/* Destroy snapshot RAM state */
void snap_ram_destroy_state(void)
{
    RAMBlockDesc *block;
    RAMBlockDesc *next_block;

    /* Free RAM blocks */
    QSIMPLEQ_FOREACH_SAFE(block, &ram_state.ram_block_list, next, next_block) {
        g_free(block->bitmap);
        g_free(block);
    }
}
