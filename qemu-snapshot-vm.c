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
#include "qemu-snapshot.h"

/* vmstate header magic */
#define VMSTATE_HEADER_MAGIC        0x5354564d
/* vmstate header eof_offset position */
#define VMSTATE_HEADER_EOF_OFFSET   24
/* vmstate header size */
#define VMSTATE_HEADER_SIZE         28

/* Maximum size of page coalescing buffer */
#define PAGE_COALESC_MAX            (512 * 1024)

/* RAM block */
typedef struct RAMBlock {
    int64_t bdrv_offset;        /* Offset on backing storage */
    int64_t length;             /* Length */
    int64_t nr_pages;           /* Page count */
    int64_t nr_slices;          /* Number of slices (for bitmap bookkeeping) */

    int64_t discard_offset;     /* Last page offset sent in precopy */

    unsigned long *bitmap;      /* Bitmap of transferred RAM slices */

    /* Link into ram_list */
    QSIMPLEQ_ENTRY(RAMBlock) next;

    char idstr[256];            /* RAM block id string */
} RAMBlock;

/* RAM block page */
typedef struct RAMPage {
    RAMBlock *block;            /* RAM block containing the page */
    int64_t offset;             /* Page offset in RAM block */
} RAMPage;

/* Page request from destination in postcopy */
typedef struct RAMPageRequest {
    RAMBlock *block;            /* RAM block */
    int64_t offset;             /* Offset in RAM block */
    unsigned size;              /* Size of request */

    /* Link into ram_ctx.page_req */
    QSIMPLEQ_ENTRY(RAMPageRequest) next;
} RAMPageRequest;

/* RAM transfer context */
typedef struct RAMCtx {
    int64_t normal_pages;       /* Total number of normal pages */
    int64_t precopy_pages;      /* Normal pages to load in precopy */
    int64_t loaded_pages;       /* Number of normal pages loaded */

    RAMPage last_page;          /* Last loaded page */

    RAMBlock *last_sent_block;  /* RAM block of last sent page */
    RAMBlock *last_req_block;   /* RAM block of last page request */

    /* RAM block list head */
    QSIMPLEQ_HEAD(, RAMBlock) ram_block_list;

    /* Page request queue for postcopy */
    QemuMutex page_req_mutex;
    QSIMPLEQ_HEAD(, RAMPageRequest) page_req;
} RAMCtx;

/* Section handler ops */
typedef struct SectionHandlerOps {
    int (*save_state)(QEMUFile *f, void *opaque, int version_id);
    int (*load_state)(QEMUFile *f, void *opaque, int version_id);
    int (*load_state_iterate)(QEMUFile *f, void *opaque, int version_id);
} SectionHandlerOps;

/* Section handlers entry */
typedef struct SectionHandlersEntry {
    const char *idstr;          /* Section id string */
    const int instance_id;      /* Section instance id */
    const int version_id;       /* Max. supported section version id */

    int real_section_id;        /* Section id from migration stream */
    int real_version_id;        /* Version id from migration stream */

    SectionHandlerOps *ops;     /* Section handler callbacks */
} SectionHandlersEntry;

/* Section handlers */
typedef struct SectionHandlers {
    /* Default handler */
    SectionHandlersEntry default_;
    /* Handlers */
    SectionHandlersEntry handlers[];
} SectionHandlers;

#define SECTION_HANDLERS_ENTRY(_idstr, _instance_id, _version_id, _ops) {   \
    .idstr          = _idstr,   \
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
static int ram_load_iterate(QEMUFile *f, void *opaque, int version_id);

static int save_state_complete(StateSaveCtx *s);
static int load_section_start_full(StateLoadCtx *s);

static RAMCtx ram_ctx;

static SectionHandlerOps default_handler_ops = {
    .save_state = default_save,
    .load_state = default_load,
};

static SectionHandlerOps ram_handler_ops = {
    .save_state = ram_save,
    .load_state = ram_load,
    .load_state_iterate = ram_load_iterate,
};

static SectionHandlers section_handlers = {
    .default_ = SECTION_HANDLERS_ENTRY("default", 0, 0, &default_handler_ops),
    .handlers = {
        SECTION_HANDLERS_ENTRY("ram", 0, 4, &ram_handler_ops),
        SECTION_HANDLERS_END(),
    },
};

static SectionHandlersEntry *find_se(const char *idstr, int instance_id)
{
    SectionHandlersEntry *se;

    for (se = section_handlers.handlers; se->idstr; se++) {
        if (!strcmp(se->idstr, idstr) && (instance_id == se->instance_id)) {
            return se;
        }
    }

    return NULL;
}

static SectionHandlersEntry *find_se_by_section_id(int section_id)
{
    SectionHandlersEntry *se;

    for (se = section_handlers.handlers; se->idstr; se++) {
        if (section_id == se->real_section_id) {
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
        error_report("Missing footer for section %s(%d)",
                     se->idstr, se->real_section_id);
        return false;
    }

    section_id = qemu_get_be32(f);
    if (section_id != se->real_section_id) {
        error_report("Unmatched footer for for section %s(%d): %d",
                     se->idstr, se->real_section_id, section_id);
        return false;
    }

    return true;
}

static inline
bool ram_offset_in_block(RAMBlock *block, int64_t offset)
{
    return block && offset < block->length;
}

static inline
bool ram_bdrv_offset_in_block(RAMBlock *block, int64_t bdrv_offset)
{
    return block && bdrv_offset >= block->bdrv_offset &&
            bdrv_offset < block->bdrv_offset + block->length;
}

static inline
int64_t ram_bdrv_from_block_offset(RAMBlock *block, int64_t offset)
{
    if (!ram_offset_in_block(block, offset)) {
        return INVALID_OFFSET;
    }

    return block->bdrv_offset + offset;
}

static inline
int64_t ram_block_offset_from_bdrv(RAMBlock *block, int64_t bdrv_offset)
{
    int64_t offset;

    if (!block) {
        return INVALID_OFFSET;
    }

    offset = bdrv_offset - block->bdrv_offset;
    return offset >= 0 ? offset : INVALID_OFFSET;
}

static RAMBlock *ram_block_by_idstr(const char *idstr)
{
    RAMBlock *block;

    QSIMPLEQ_FOREACH(block, &ram_ctx.ram_block_list, next) {
        if (!strcmp(idstr, block->idstr)) {
            return block;
        }
    }

    return NULL;
}

static RAMBlock *ram_block_by_bdrv_offset(int64_t bdrv_offset)
{
    RAMBlock *block;

    QSIMPLEQ_FOREACH(block, &ram_ctx.ram_block_list, next) {
        if (ram_bdrv_offset_in_block(block, bdrv_offset)) {
            return block;
        }
    }

    return NULL;
}

static RAMBlock *ram_block_from_stream(QEMUFile *f, int flags)
{
    static RAMBlock *block;
    char idstr[256];

    if (flags & RAM_SAVE_FLAG_CONTINUE) {
        if (!block) {
            error_report("RAM_SAVE_FLAG_CONTINUE outside RAM block");
            return NULL;
        }

        return block;
    }

    if (!qemu_get_counted_string(f, idstr)) {
        error_report("Failed to get RAM block name");
        return NULL;
    }

    block = ram_block_by_idstr(idstr);
    if (!block) {
        error_report("Can't find RAM block %s", idstr);
        return NULL;
    }

    return block;
}

static int64_t ram_block_next_bdrv_offset(void)
{
    RAMBlock *last_block;
    int64_t offset;

    last_block = QSIMPLEQ_LAST(&ram_ctx.ram_block_list, RAMBlock, next);
    if (!last_block) {
        return 0;
    }

    offset = last_block->bdrv_offset + last_block->length;
    return ROUND_UP(offset, BDRV_CLUSTER_SIZE);
}

static void ram_block_add(const char *idstr, int64_t size)
{
    RAMBlock *block;

    block = g_new0(RAMBlock, 1);
    block->length = size;
    block->bdrv_offset = ram_block_next_bdrv_offset();
    strcpy(block->idstr, idstr);

    QSIMPLEQ_INSERT_TAIL(&ram_ctx.ram_block_list, block, next);
}

static void ram_block_list_init_bitmaps(void)
{
    RAMBlock *block;

    QSIMPLEQ_FOREACH(block, &ram_ctx.ram_block_list, next) {
        block->nr_pages = block->length >> page_bits;
        block->nr_slices = ROUND_UP(block->length, slice_size) >> slice_bits;

        block->bitmap = bitmap_new(block->nr_slices);
        bitmap_set(block->bitmap, 0, block->nr_slices);
    }
}

static bool ram_block_list_from_stream(QEMUFile *f, int64_t mem_size)
{
    int64_t total_ram_bytes;

    total_ram_bytes = mem_size;
    while (total_ram_bytes > 0) {
        char idstr[256];
        int64_t size;

        if (!qemu_get_counted_string(f, idstr)) {
            error_report("Failed to get RAM block list");
            return false;
        }
        size = qemu_get_be64(f);

        ram_block_add(idstr, size);
        total_ram_bytes -= size;
    }

    if (total_ram_bytes != 0) {
        error_report("Corrupted RAM block list");
        return false;
    }

    /* Initialize per-block bitmaps */
    ram_block_list_init_bitmaps();

    return true;
}

static void save_state_check_errors(StateSaveCtx *s, int *res)
{
    /* Check for -EIO which indicates input stream EOF */
    if (*res == -EIO) {
        *res = 0;
    }

    /*
     * Check for file errors on success. Replace generic -EINVAL
     * retcode with file error if possible.
     */
    if (*res >= 0 || *res == -EINVAL) {
        int f_res = qemu_file_get_error(s->f_fd);

        f_res = (f_res == -EIO) ? 0 : f_res;
        if (!f_res) {
            f_res = qemu_file_get_error(s->f_vmstate);
        }
        if (f_res) {
            *res = f_res;
        }
    }
}

static int ram_alloc_page_backing(StateSaveCtx *s, RAMPage *page,
                                  int64_t bdrv_offset)
{
    int res = 0;

    /*
     * Reduce the number of unwritten extents in image backing file.
     *
     * We can achieve that by using a bitmap of RAM block 'slices' to
     * enforce zero blockdev write once we are going to store a memory
     * page within that slice.
     */
    if (test_and_clear_bit(page->offset >> slice_bits, page->block->bitmap)) {
        res = blk_pwrite(s->blk, bdrv_offset & slice_mask,
                         s->zero_buf, slice_size, 0);
    }

    return MIN(res, 0);
}

static int ram_save_page(StateSaveCtx *s, RAMPage *page, uint8_t *data)
{
    size_t usage = s->ioc_pages->usage;
    int64_t bdrv_offset;
    int res = 0;

    bdrv_offset = ram_bdrv_from_block_offset(page->block, page->offset);
    if (bdrv_offset == INVALID_OFFSET) {
        error_report("Corrupted RAM page");
        return -EINVAL;
    }

    /* Deal with fragmentation of the image backing file */
    res = ram_alloc_page_backing(s, page, bdrv_offset);
    if (res) {
        return res;
    }

    /* Are we saving a contiguous page? */
    if (bdrv_offset != s->last_bdrv_offset ||
            (usage + page_size) >= PAGE_COALESC_MAX) {
        if (usage) {
            /* Flush coalesced pages to block device */
            res = blk_pwrite(s->blk, s->bdrv_offset, s->ioc_pages->data,
                             usage, 0);
            res = MIN(res, 0);
        }

        /* Reset coalescing buffer state */
        s->ioc_pages->usage = 0;
        s->ioc_pages->offset = 0;
        /* Switch to the new bdrv_offset */
        s->bdrv_offset = bdrv_offset;
    }

    qio_channel_write(QIO_CHANNEL(s->ioc_pages), (char *) data,
                      page_size, NULL);
    s->last_bdrv_offset = bdrv_offset + page_size;

    return res;
}

static int ram_save_page_flush(StateSaveCtx *s)
{
    size_t usage = s->ioc_pages->usage;
    int res = 0;

    if (usage) {
        /* Flush coalesced pages to block device */
        res = blk_pwrite(s->blk, s->bdrv_offset,
                         s->ioc_pages->data, usage, 0);
        res = MIN(res, 0);
    }

    /* Reset coalescing buffer state */
    s->ioc_pages->usage = 0;
    s->ioc_pages->offset = 0;

    s->last_bdrv_offset = INVALID_OFFSET;

    return res;
}

static int ram_save(QEMUFile *f, void *opaque, int version_id)
{
    StateSaveCtx *s = (StateSaveCtx *) opaque;
    int incompat_flags = RAM_SAVE_FLAG_COMPRESS_PAGE | RAM_SAVE_FLAG_XBZRLE;
    int flags = 0;
    int res = 0;

    if (version_id != 4) {
        error_report("Unsupported version %d for 'ram' handler v4", version_id);
        return -EINVAL;
    }

    while (!res && !(flags & RAM_SAVE_FLAG_EOS)) {
        RAMBlock *block = NULL;
        int64_t offset;

        offset = qemu_get_be64(f);
        flags = offset & ~page_mask;
        offset &= page_mask;

        if (flags & incompat_flags) {
            error_report("Incompatible RAM page flags 0x%x", flags);
            res = -EINVAL;
            break;
        }

        /* Lookup RAM block for the page */
        if (flags & (RAM_SAVE_FLAG_ZERO | RAM_SAVE_FLAG_PAGE)) {
            block = ram_block_from_stream(f, flags);
            if (!block) {
                res = -EINVAL;
                break;
            }
        }

        switch (flags & ~RAM_SAVE_FLAG_CONTINUE) {
        case RAM_SAVE_FLAG_MEM_SIZE:
            if (s->ram_list_offset) {
                error_report("Repeated RAM page with RAM_SAVE_FLAG_MEM_SIZE");
                res = -EINVAL;
                break;
            }

            /* Save position of section with the list of RAM blocks */
            s->ram_list_offset = s->section_offset;

            /* Get RAM block list */
            if (!ram_block_list_from_stream(f, offset)) {
                res = -EINVAL;
            }
            break;

        case RAM_SAVE_FLAG_ZERO:
            /* Nothing to do with zero page */
            qemu_get_byte(f);
            break;

        case RAM_SAVE_FLAG_PAGE:
        {
            RAMPage page = { .block = block, .offset = offset };
            uint8_t *data;
            ssize_t count;

            count = qemu_peek_buffer(f, &data, page_size, 0);
            qemu_file_skip(f, count);
            if (count != page_size) {
                /* I/O error */
                break;
            }

            res = ram_save_page(s, &page, data);

            /* Update normal page count */
            ram_ctx.normal_pages++;
            break;
        }

        case RAM_SAVE_FLAG_EOS:
            /* Normal exit */
            break;

        default:
            error_report("RAM page with unknown combination of flags 0x%x", flags);
            res = -EINVAL;

        }

        /* Make additional check for file errors */
        if (!res) {
            res = qemu_file_get_error(f);
        }
    }

    /* Flush page coalescing buffer */
    if (!res) {
        res = ram_save_page_flush(s);
    }

    return res;
}

static int default_save(QEMUFile *f, void *opaque, int version_id)
{
    StateSaveCtx *s = (StateSaveCtx *) opaque;

    if (!s->ram_offset) {
        error_report("Unexpected (non-iterable device state) section");
        return -EINVAL;
    }

    if (!s->device_offset) {
        s->device_offset = s->section_offset;
        /* Save the rest of vmstate, including non-iterable device state */
        return save_state_complete(s);
    }

    /* Should never get here */
    assert(false);
    return -EINVAL;
}

static int save_state_complete(StateSaveCtx *s)
{
    QEMUFile *f = s->f_fd;
    int64_t eof_pos;
    int64_t pos;

    /* Current read offset */
    pos = qemu_ftell2(f);

    /* vmstate magic */
    qemu_put_be32(s->f_vmstate, VMSTATE_HEADER_MAGIC);
    /* Target page size */
    qemu_put_be32(s->f_vmstate, page_size);
    /* Number of non-zero pages */
    qemu_put_be64(s->f_vmstate, ram_ctx.normal_pages);

    /* Offsets relative to QEMU_VM_FILE_MAGIC: */

    /* RAM block list section */
    qemu_put_be32(s->f_vmstate, s->ram_list_offset);
    /*
     * First non-iterable device section.
     *
     * Partial RAM sections are skipped in the vmstate stream so
     * ram_offset shall become the device_offset.
     */
    qemu_put_be32(s->f_vmstate, s->ram_offset);
    /* Slot for eof_offset */
    qemu_put_be32(s->f_vmstate, 0);

    /*
     * At the completion stage we save the leading part of migration stream
     * which contains header, configuration section and the 'ram' section
     * with QEMU_VM_SECTION_FULL type containing list of RAM blocks.
     *
     * Migration leader ends at the first partial RAM section.
     * QEMU_VM_SECTION_PART token for that section is pointed by s->ram_offset.
     */
    qemu_put_buffer(s->f_vmstate, s->ioc_leader->data, s->ram_offset);
    /*
     * Trailing part with non-iterable device state.
     *
     * First goes the section header which was skipped with QEMUFile
     * so we need to take it from s->section_header.
     */
    qemu_put_buffer(s->f_vmstate, s->section_header, pos - s->device_offset);

    /* Finally we forward the tail of migration stream to vmstate on backing */
    qemu_fsplice_tail(s->f_vmstate, f);
    eof_pos = qemu_ftell(s->f_vmstate);

    /* Put eof_offset to the slot in vmstate stream: */

    /* Simulate negative seek() */
    qemu_update_position(s->f_vmstate,
                         (size_t)(ssize_t) (VMSTATE_HEADER_EOF_OFFSET - eof_pos));
    /* Write to the eof_offset header field */
    qemu_put_be32(s->f_vmstate, eof_pos - VMSTATE_HEADER_SIZE);
    qemu_fflush(s->f_vmstate);

    return 1;
}

static int save_section_config(StateSaveCtx *s)
{
    QEMUFile *f = s->f_fd;
    uint32_t id_len;

    id_len = qemu_get_be32(f);
    if (id_len > 255) {
        error_report("Corrupted configuration section");
        return -EINVAL;
    }
    qemu_file_skip(f, id_len);

    return 0;
}

static int save_section_start_full(StateSaveCtx *s)
{
    QEMUFile *f = s->f_fd;
    SectionHandlersEntry *se;
    int section_id;
    int instance_id;
    int version_id;
    char idstr[256];
    int res;

    section_id = qemu_get_be32(f);
    if (!qemu_get_counted_string(f, idstr)) {
        error_report("Failed to get section name(%d)", section_id);
        return -EINVAL;
    }

    instance_id = qemu_get_be32(f);
    version_id = qemu_get_be32(f);

    /* Find section handler */
    se = find_se(idstr, instance_id);
    if (!se) {
        se = &section_handlers.default_;
    } else if (version_id > se->version_id) {
        /* Validate version */
        error_report("Unsupported version %d for '%s' v%d",
                version_id, idstr, se->version_id);
        return -EINVAL;
    }

    se->real_section_id = section_id;
    se->real_version_id = version_id;

    res = se->ops->save_state(f, s, se->real_version_id);
    /* Positive value indicates completion, no need to check footer */
    if (res) {
        return res;
    }

    /* Check section footer */
    if (!check_section_footer(f, se)) {
        return -EINVAL;
    }

    return 0;
}

static int save_section_part_end(StateSaveCtx *s)
{
    QEMUFile *f = s->f_fd;
    SectionHandlersEntry *se;
    int section_id;
    int res;

    /* First section with QEMU_VM_SECTION_PART type must be a 'ram' section */
    if (!s->ram_offset) {
        s->ram_offset = s->section_offset;
    }

    section_id = qemu_get_be32(f);

    /* Lookup section handler by numeric section id */
    se = find_se_by_section_id(section_id);
    if (!se) {
        error_report("Unknown section id %d", section_id);
        return -EINVAL;
    }

    res = se->ops->save_state(f, s, se->real_version_id);
    /* With partial sections we won't have positive success retcodes */
    if (res) {
        return res;
    }

    /* Check section footer */
    if (!check_section_footer(f, se)) {
        return -EINVAL;
    }

    return 0;
}

static int save_state_header(StateSaveCtx *s)
{
    QEMUFile *f = s->f_fd;
    uint32_t v;

    /* Validate qemu magic */
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

int coroutine_fn save_state_main(StateSaveCtx *s)
{
    QEMUFile *f = s->f_fd;
    uint8_t *buf;
    uint8_t section_type;
    int res = 0;

    /* Deal with migration stream header */
    res = save_state_header(s);
    if (res) {
        /* Check for file errors in case we have -EINVAL */
        save_state_check_errors(s, &res);
        return res;
    }

    while (!res) {
        /* Update current section offset */
        s->section_offset = qemu_ftell2(f);

        /*
         * We need to keep some data from the beginning of each section.
         *
         * When first non-iterable device section is reached and we are going
         * to write to the vmstate stream in 'default_handler', it is used to
         * restore the already skipped part of migration stream.
         */
        qemu_peek_buffer(f, &buf, sizeof(s->section_header), 0);
        memcpy(s->section_header, buf, sizeof(s->section_header));

        /* Read section type token */
        section_type = qemu_get_byte(f);

        switch (section_type) {
        case QEMU_VM_CONFIGURATION:
            res = save_section_config(s);
            break;

        case QEMU_VM_SECTION_FULL:
        case QEMU_VM_SECTION_START:
            res = save_section_start_full(s);
            break;

        case QEMU_VM_SECTION_PART:
        case QEMU_VM_SECTION_END:
            res = save_section_part_end(s);
            break;

        case QEMU_VM_EOF:
            /*
             * End of migration stream.
             *
             * Normally we will never get here since the ending part of migration
             * stream is a series of QEMU_VM_SECTION_FULL sections holding
             * state for non-iterable devices. In our case all those sections
             * are saved with a single call to save_section_start_full() once
             * we get an unknown section id and invoke default handler.
             */
            res = -EINVAL;
            break;

        default:
            error_report("Unknown section type %d", section_type);
            res = -EINVAL;

        }

        /* Additional check for file errors */
        save_state_check_errors(s, &res);
    }

    /* Replace positive retcode with 0 */
    return MIN(res, 0);
}

static void load_state_check_errors(StateLoadCtx *s, int *res)
{
    /*
     * Check for file errors on success. Replace generic -EINVAL
     * retcode with file error if possible.
     */
    if (*res >= 0 || *res == -EINVAL) {
        int f_res = qemu_file_get_error(s->f_fd);

        if (!f_res) {
            f_res = qemu_file_get_error(s->f_vmstate);
        }
        if (f_res) {
            *res = f_res;
        }
    }
}

static bool get_queued_page(RAMPage *page)
{
    RAMCtx *ram = &ram_ctx;

    if (QSIMPLEQ_EMPTY_ATOMIC(&ram->page_req)) {
        return false;
    }

    QEMU_LOCK_GUARD(&ram->page_req_mutex);

    while (!QSIMPLEQ_EMPTY(&ram->page_req)) {
        RAMPageRequest *entry = QSIMPLEQ_FIRST(&ram->page_req);
        RAMBlock *block = entry->block;
        int64_t slice = entry->offset >> slice_bits;

        QSIMPLEQ_REMOVE_HEAD(&ram->page_req, next);
        g_free(entry);

        /*
         * Test respective bit in RAM block's slice bitmap to check if
         * we still haven't read that slice from the image.
         */
        if (test_bit(slice, block->bitmap)) {
            page->block = block;
            page->offset = slice << slice_bits;

            return true;
        }
    }

    return false;
}

static int queue_page_request(const char *idstr, int64_t offset, unsigned size)
{
    RAMCtx *ram = &ram_ctx;
    RAMBlock *block;
    RAMPageRequest *new_entry;

    if (!idstr) {
        block = ram->last_req_block;
        if (!block) {
            error_report("RP-REQ_PAGES: no previous block");
            return -EINVAL;
        }
    } else {
        block = ram_block_by_idstr(idstr);
        if (!block) {
            error_report("RP-REQ_PAGES: cannot find block %s", idstr);
            return -EINVAL;
        }

        ram->last_req_block = block;
    }

    if (!ram_offset_in_block(block, offset)) {
        error_report("RP-REQ_PAGES: offset 0x%" PRIx64 " out of RAM block %s",
                     offset, idstr);
        return -EINVAL;
    }

    new_entry = g_new0(RAMPageRequest, 1);
    new_entry->block = block;
    new_entry->offset = offset;
    new_entry->size = size;

    qemu_mutex_lock(&ram->page_req_mutex);
    QSIMPLEQ_INSERT_TAIL(&ram->page_req, new_entry, next);
    qemu_mutex_unlock(&ram->page_req_mutex);

    return 0;
}

/* QEMU_VM_COMMAND sub-commands */
typedef enum VmSubCmd {
    MIG_CMD_OPEN_RETURN_PATH = 1,
    MIG_CMD_POSTCOPY_ADVISE = 3,
    MIG_CMD_POSTCOPY_LISTEN = 4,
    MIG_CMD_POSTCOPY_RUN = 5,
    MIG_CMD_POSTCOPY_RAM_DISCARD = 6,
    MIG_CMD_PACKAGED = 7,
} VmSubCmd;

/* Return-path message types */
typedef enum RpMsgType {
    MIG_RP_MSG_INVALID = 0,
    MIG_RP_MSG_SHUT = 1,
    MIG_RP_MSG_REQ_PAGES_ID = 3,
    MIG_RP_MSG_REQ_PAGES = 4,
    MIG_RP_MSG_MAX = 7,
} RpMsgType;

typedef struct RpMsgArgs {
    int len;
    const char *name;
} RpMsgArgs;

/*
 * Return-path message length/name indexed by message type.
 * -1 value stands for variable message length.
 */
static RpMsgArgs rp_msg_args[] = {
    [MIG_RP_MSG_INVALID]        = { .len = -1, .name = "INVALID" },
    [MIG_RP_MSG_SHUT]           = { .len =  4, .name = "SHUT" },
    [MIG_RP_MSG_REQ_PAGES_ID]   = { .len = -1, .name = "REQ_PAGES_ID" },
    [MIG_RP_MSG_REQ_PAGES]      = { .len = 12, .name = "REQ_PAGES" },
    [MIG_RP_MSG_MAX]            = { .len = -1, .name = "MAX" },
};

/* Return-path message processing thread */
static void *rp_listen_thread(void *opaque)
{
    StateLoadCtx *s = (StateLoadCtx *) opaque;
    QEMUFile *f = s->f_rp_fd;
    int res = 0;

    while (!res) {
        uint8_t h_buf[512];
        const int h_max_len = sizeof(h_buf);
        int h_type;
        int h_len;
        size_t count;

        h_type = qemu_get_be16(f);
        h_len = qemu_get_be16(f);

        /* Make early check for input errors */
        res = qemu_file_get_error(f);
        if (res) {
            break;
        }

        /* Check message type */
        if (h_type >= MIG_RP_MSG_MAX || h_type == MIG_RP_MSG_INVALID) {
            error_report("RP: received invalid message type %d length %d",
                         h_type, h_len);
            res = -EINVAL;
            break;
        }

        /* Check message length */
        if (rp_msg_args[h_type].len != -1 && h_len != rp_msg_args[h_type].len) {
            error_report("RP: received %s message len %d expected %d",
                         rp_msg_args[h_type].name,
                         h_len, rp_msg_args[h_type].len);
            res = -EINVAL;
            break;
        } else if (h_len > h_max_len) {
            error_report("RP: received %s message len %d max_len %d",
                         rp_msg_args[h_type].name, h_len, h_max_len);
            res = -EINVAL;
            break;
        }

        count = qemu_get_buffer(f, h_buf, h_len);
        if (count != h_len) {
            break;
        }

        switch (h_type) {
        case MIG_RP_MSG_SHUT:
        {
            int shut_error;

            shut_error = be32_to_cpu(*(uint32_t *) h_buf);
            if (shut_error) {
                error_report("RP: sibling shutdown, error %d", shut_error);
            }

            /* Exit processing loop */
            res = 1;
            break;
        }

        case MIG_RP_MSG_REQ_PAGES:
        case MIG_RP_MSG_REQ_PAGES_ID:
        {
            uint64_t offset;
            uint32_t size;
            char *id_str = NULL;

            offset = be64_to_cpu(*(uint64_t *) (h_buf + 0));
            size = be32_to_cpu(*(uint32_t *) (h_buf + 8));

            if (h_type == MIG_RP_MSG_REQ_PAGES_ID) {
                int h_parsed_len = rp_msg_args[MIG_RP_MSG_REQ_PAGES].len;

                if (h_len > h_parsed_len) {
                    int id_len;

                    /* RAM block id string */
                    id_len = h_buf[h_parsed_len];
                    id_str = (char *) &h_buf[h_parsed_len + 1];
                    id_str[id_len] = 0;

                    h_parsed_len += id_len + 1;
                }

                if (h_parsed_len != h_len) {
                    error_report("RP: received %s message len %d expected %d",
                                 rp_msg_args[MIG_RP_MSG_REQ_PAGES_ID].name,
                                 h_len, h_parsed_len);
                    res = -EINVAL;
                    break;
                }
            }

            res = queue_page_request(id_str, offset, size);
            break;
        }

        default:
            error_report("RP: received unexpected message type %d len %d",
                         h_type, h_len);
            res = -EINVAL;
        }
    }

    if (res >= 0) {
        res = qemu_file_get_error(f);
    }
    if (res) {
        error_report("RP: listen thread exit, error %d", res);
    }

    return NULL;
}

static void send_command(QEMUFile *f, int cmd, uint16_t len, uint8_t *data)
{
    qemu_put_byte(f, QEMU_VM_COMMAND);
    qemu_put_be16(f, (uint16_t) cmd);
    qemu_put_be16(f, len);

    qemu_put_buffer_async(f, data, len, false);
    qemu_fflush(f);
}

static void send_ram_block_discard(QEMUFile *f, RAMBlock *block)
{
    int id_len;
    int msg_len;
    uint8_t msg_buf[512];

    id_len = strlen(block->idstr);
    assert(id_len < 256);

    /* Version, always 0 */
    msg_buf[0] = 0;
    /* RAM block ID string length, not including terminating 0 */
    msg_buf[1] = id_len;
    /* RAM block ID string with terminating zero */
    memcpy(msg_buf + 2, block->idstr, (id_len + 1));
    msg_len = 2 + id_len + 1;
    /* Discard range offset */
    stq_be_p(msg_buf + msg_len, block->discard_offset);
    msg_len += 8;
    /* Discard range length */
    stq_be_p(msg_buf + msg_len, (block->length - block->discard_offset));
    msg_len += 8;

    send_command(f, MIG_CMD_POSTCOPY_RAM_DISCARD, msg_len, msg_buf);
}

static int send_each_ram_block_discard(QEMUFile *f)
{
    RAMBlock *block;
    int res = 0;

    QSIMPLEQ_FOREACH(block, &ram_ctx.ram_block_list, next) {
        send_ram_block_discard(f, block);

        res = qemu_file_get_error(f);
        if (res) {
            break;
        }
    }

    return res;
}

static int prepare_postcopy(StateLoadCtx *s)
{
    QEMUFile *f = s->f_fd;
    uint64_t tmp[2];
    int res;

    /* Number of pages to load in precopy before switching to postcopy */
    ram_ctx.precopy_pages = ram_ctx.normal_pages * s->postcopy_percent / 100;

    /* Send POSTCOPY_ADVISE */
    tmp[0] = cpu_to_be64(page_size);
    tmp[1] = cpu_to_be64(page_size);
    send_command(f, MIG_CMD_POSTCOPY_ADVISE, 16, (uint8_t *) tmp);

    /* Open return path on destination */
    send_command(f, MIG_CMD_OPEN_RETURN_PATH, 0, NULL);

    /*
     * Check for file errors after sending POSTCOPY_ADVISE command
     * since destination may already have closed input pipe in case
     * postcopy had not been enabled in advance.
     */
    res = qemu_file_get_error(f);
    if (!res) {
        qemu_thread_create(&s->rp_listen_thread, "rp_thread",
                           rp_listen_thread, s, QEMU_THREAD_JOINABLE);
        s->has_rp_listen_thread = true;
    }

    return res;
}

static int start_postcopy(StateLoadCtx *s)
{
    QIOChannelBuffer *bioc;
    QEMUFile *fb;
    int eof_pos;
    uint32_t length;
    int res = 0;

    /*
     * Send RAM discards for each block's unsent part. Without discards,
     * the userfault_fd code on destination will not trigger page requests
     * as expected. Also, the UFFDIO_COPY ioctl that is used to place incoming
     * page in postcopy would give an error if that page has not faulted
     * with MISSING reason.
     */
    res = send_each_ram_block_discard(s->f_fd);
    if (res) {
        return res;
    }

    /*
     * To perform a switch to postcopy on destination, we need to send
     * commands and the device state data in the following order:
     *   * MIG_CMD_POSTCOPY_LISTEN
     *   * Non-iterable device state sections
     *   * MIG_CMD_POSTCOPY_RUN
     *
     * All this has to be packaged into a single blob using MIG_CMD_PACKAGED
     * command. While loading the device state we may trigger page transfer
     * requests and the fd must be free to process those, thus the destination
     * must read the whole device state off the fd before it starts
     * processing it. To wrap it up in a package, QEMU buffer channel is used.
     */
    bioc = qio_channel_buffer_new(512 * 1024);
    qio_channel_set_name(QIO_CHANNEL(bioc), "migration-postcopy-buffer");
    fb = qemu_fopen_channel_output(QIO_CHANNEL(bioc));
    object_unref(OBJECT(bioc));

    /* MIG_CMD_POSTCOPY_LISTEN command */
    send_command(fb, MIG_CMD_POSTCOPY_LISTEN, 0, NULL);

    /* The rest of non-iterable device state with an optional vmdesc section */
    qemu_fsplice(fb, s->f_vmstate, s->eof_offset - s->device_offset);
    qemu_fflush(fb);

    /*
     * vmdesc section may optionally be present at the end of the stream
     * so we'll try to locate it and truncate the trailer.
     */
    eof_pos = bioc->usage - 1;

    for (int offset = (bioc->usage - 11); offset >= 0; offset--) {
        if (bioc->data[offset] == QEMU_VM_SECTION_FOOTER &&
                bioc->data[offset + 5] == QEMU_VM_EOF &&
                bioc->data[offset + 6] == QEMU_VM_VMDESCRIPTION) {
            uint32_t expected_length = bioc->usage - (offset + 11);
            uint32_t json_length;

            json_length = be32_to_cpu(*(uint32_t  *) &bioc->data[offset + 7]);
            if (json_length != expected_length) {
                error_report("Corrupted vmdesc trailer: length %" PRIu32
                             " expected %" PRIu32,
                             json_length, expected_length);
                res = -EINVAL;
                goto fail;
            }

            eof_pos = offset + 5;
            break;
        }
    }

    /*
     * When switching to postcopy we need to skip QEMU_VM_EOF token which
     * normally is placed after the last non-iterable device state section
     * (but before the vmdesc section).
     *
     * Skipping QEMU_VM_EOF is required to allow migration process to
     * continue in postcopy. Vmdesc section also has to be skipped here.
     */
    if (eof_pos >= 0 && bioc->data[eof_pos] == QEMU_VM_EOF) {
        bioc->usage = eof_pos;
        bioc->offset = eof_pos;
    }

    /* Finally is the MIG_CMD_POSTCOPY_RUN command */
    send_command(fb, MIG_CMD_POSTCOPY_RUN, 0, NULL);

    /* Now send that blob */
    length = cpu_to_be32(bioc->usage);
    send_command(s->f_fd, MIG_CMD_PACKAGED, sizeof(length), (uint8_t *) &length);
    qemu_put_buffer_async(s->f_fd, bioc->data, bioc->usage, false);
    qemu_fflush(s->f_fd);

    /*
     * Switch to lower setting of in-flight requests limit
     * to reduce page request latencies.
     */
    aio_ring_set_max_inflight(s->aio_ring, AIO_RING_INFLIGHT_LOWLAT);

    s->in_postcopy = true;

fail:
    qemu_fclose(fb);
    load_state_check_errors(s, &res);

    return res;
}

static bool is_postcopy_switchover(StateLoadCtx *s)
{
    return ram_ctx.loaded_pages > ram_ctx.precopy_pages;
}

static void send_section_header_part_end(QEMUFile *f, SectionHandlersEntry *se,
        uint8_t section_type)
{
    assert(section_type == QEMU_VM_SECTION_PART ||
           section_type == QEMU_VM_SECTION_END);

    qemu_put_byte(f, section_type);
    qemu_put_be32(f, se->real_section_id);
}

static void send_section_footer(QEMUFile *f, SectionHandlersEntry *se)
{
    qemu_put_byte(f, QEMU_VM_SECTION_FOOTER);
    qemu_put_be32(f, se->real_section_id);
}

static void send_page_header(QEMUFile *f, RAMBlock *block, int64_t offset)
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

static void send_zeropage(QEMUFile *f, RAMBlock *block, int64_t offset)
{
    send_page_header(f, block, offset | RAM_SAVE_FLAG_ZERO);
    qemu_put_byte(f, 0);
}

static bool find_next_page(RAMPage *page)
{
    RAMCtx *ram = &ram_ctx;
    RAMBlock *block = ram->last_page.block;
    int64_t slice = ram->last_page.offset >> slice_bits;
    bool full_round = false;
    bool found = false;

    if (!block) {
restart:
        block = QSIMPLEQ_FIRST(&ram->ram_block_list);
        slice = 0;
        full_round = true;
    }

    while (!found && block) {
        slice = find_next_bit(block->bitmap, block->nr_slices, slice);
        /* Can't find unsent slice in block? */
        if (slice >= block->nr_slices) {
            /* Try next block */
            block = QSIMPLEQ_NEXT(block, next);
            slice = 0;

            continue;
        }

        found = true;
    }

    /*
     * Re-start from the beginning if couldn't find unsent slice,
     * but do it only once.
     */
    if (!found && !full_round) {
        goto restart;
    }

    if (found) {
        page->block = block;
        page->offset = slice << slice_bits;
    }

    return found;
}

static inline
void get_page_range(RAMPage *page, unsigned *length, unsigned max_length)
{
    int64_t start_slice;
    int64_t end_slice;
    int64_t tmp;

    assert(QEMU_IS_ALIGNED(page->offset, slice_size));
    assert(max_length >= slice_size);

    start_slice = page->offset >> slice_bits;
    end_slice = find_next_zero_bit(page->block->bitmap, page->block->nr_slices,
                                   page->offset >> slice_bits);

    tmp = (end_slice - start_slice) << slice_bits;
    tmp = MIN(page->block->length - page->offset, tmp);

    /*
     * Length is always aligned to slice_size with the exception of case
     * when it is the last slice in RAM block.
     */
    *length = MIN(max_length, tmp);
}

static inline
void clear_page_range(RAMPage *page, unsigned length)
{
    assert(QEMU_IS_ALIGNED(page->offset, slice_size));
    assert(length);

    /*
     * Page offsets are aligned to the slice boundary so we only need
     * to round up length for the case when we load last slice in the block.
     */
    bitmap_clear(page->block->bitmap, page->offset >> slice_bits,
                 ((length - 1) >> slice_bits) + 1);
}

ssize_t coroutine_fn ram_load_aio_co(AioRingRequest *req)
{
    return blk_pread((BlockBackend *) req->opaque, req->offset,
            req->data, req->size);
}

static void coroutine_fn ram_load_submit_aio(StateLoadCtx *s)
{
    RAMCtx *ram = &ram_ctx;
    AioRingRequest *req;

    while ((req = aio_ring_get_request(s->aio_ring))) {
        RAMPage page;
        unsigned max_length = s->in_postcopy ? AIO_TRANSFER_SIZE_LOWLAT :
                AIO_TRANSFER_SIZE;
        unsigned length;
        bool urgent;

        urgent = get_queued_page(&page);
        if (!urgent && !find_next_page(&page)) {
            break;
        }
        
        /* Get range of contiguous pages that were not transferred yet */
        get_page_range(&page, &length, max_length);
        /* Clear range of pages to be queued for I/O */
        clear_page_range(&page, length);

        /* Used by find_next_page() */
        ram->last_page.block = page.block;
        ram->last_page.offset = page.offset + length;

        /* Used by send_ram_block_discard() */
        page.block->discard_offset = ram->last_page.offset;

        /* Setup I/O request */
        req->opaque = s->blk;
        req->data = qemu_blockalign(blk_bs(s->blk), length);
        req->offset = ram_bdrv_from_block_offset(page.block, page.offset);
        req->size = length;

        aio_ring_submit(s->aio_ring);
    }
}

static int ram_load_complete_aio(StateLoadCtx *s, AioRingEvent *ev)
{
    QEMUFile *f = s->f_fd;
    RAMCtx *ram = &ram_ctx;
    RAMBlock *block = ram->last_sent_block;
    void *bdrv_data = ev->origin->data;
    int64_t bdrv_offset = ev->origin->offset;
    ssize_t bdrv_count = ev->status;
    int64_t offset;
    int64_t flags = RAM_SAVE_FLAG_CONTINUE;
    int pages = 0;

    /* Need to switch to the another RAM block? */
    if (!ram_bdrv_offset_in_block(block, bdrv_offset)) {
        /*
         * Lookup RAM block by BDRV offset cause in postcopy we
         * can issue AIO loads from arbitrary blocks.
         */
        block = ram_block_by_bdrv_offset(bdrv_offset);
        ram->last_sent_block = block;

        /* Reset RAM_SAVE_FLAG_CONTINUE */
        flags = 0;
    }
    offset = ram_block_offset_from_bdrv(block, bdrv_offset);

    for (ssize_t count = 0; count < bdrv_count; count += page_size) {
        if (buffer_is_zero(bdrv_data, page_size)) {
            send_zeropage(f, block, (offset | flags));
        } else {
            send_page_header(f, block, (offset | RAM_SAVE_FLAG_PAGE | flags));
            qemu_put_buffer_async(f, bdrv_data, page_size, false);

            /* Update normal page count */
            ram->loaded_pages++;
        }

        /*
         * BDRV request shall never cross RAM block boundary so we can
         * set RAM_SAVE_FLAG_CONTINUE here unconditionally.
         */
        flags = RAM_SAVE_FLAG_CONTINUE;

        bdrv_data += page_size;
        offset += page_size;
        pages++;
    }

    /* Need to flush here cause we use qemu_put_buffer_async() */
    qemu_fflush(f);

    return pages;
}

static int coroutine_fn ram_load_pages(StateLoadCtx *s)
{
    AioRingEvent *event;
    int res = 0;

    /* Fill blockdev AIO queue */
    ram_load_submit_aio(s);

    /* Check for AIO completion event */
    event = aio_ring_wait_event(s->aio_ring);
    if (event) {
        /* Check completion status */
        res = event->status;
        if (res > 0) {
            res = ram_load_complete_aio(s, event);
        }

        qemu_vfree(event->origin->data);
        aio_ring_complete(s->aio_ring);
    }

    return res;
}

static int coroutine_fn ram_load_pages_flush(StateLoadCtx *s)
{
    AioRingEvent *event;

    while ((event = aio_ring_wait_event(s->aio_ring))) {
        /* Check completion status */
        if (event->status > 0) {
            ram_load_complete_aio(s, event);
        }

        qemu_vfree(event->origin->data);
        aio_ring_complete(s->aio_ring);
    }

    return 0;
}

static int ram_load(QEMUFile *f, void *opaque, int version_id)
{
    int compat_flags = RAM_SAVE_FLAG_MEM_SIZE | RAM_SAVE_FLAG_EOS;
    int flags = 0;
    int res = 0;

    if (version_id != 4) {
        error_report("Unsupported version %d for 'ram' handler v4", version_id);
        return -EINVAL;
    }

    while (!res && !(flags & RAM_SAVE_FLAG_EOS)) {
        int64_t offset;

        offset = qemu_get_be64(f);
        flags = offset & ~page_mask;
        offset &= page_mask;

        if (flags & ~compat_flags) {
            error_report("Incompatible RAM page flags 0x%x", flags);
            res = -EINVAL;
            break;
        }

        switch (flags) {
            case RAM_SAVE_FLAG_MEM_SIZE:
                /* Fill RAM block list */
                ram_block_list_from_stream(f, offset);
                break;

            case RAM_SAVE_FLAG_EOS:
                /* Normal exit */
                break;

            default:
                error_report("Unknown combination of RAM page flags 0x%x", flags);
                res = -EINVAL;
        }

        /* Check for file errors even if everything looks good */
        if (!res) {
            res = qemu_file_get_error(f);
        }
    }

    return res;
}

#define YIELD_AFTER_MS  500 /* ms */

static int ram_load_iterate(QEMUFile *f, void *opaque, int version_id)
{
    StateLoadCtx *s = (StateLoadCtx *) opaque;
    int64_t t_start;
    int tmp_res;
    int res = 1;

    t_start = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);

    for (int iter = 0; res > 0; iter++) {
        res = ram_load_pages(s);

        if (!(iter & 7)) {
            int64_t t_cur = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);

            if ((t_cur - t_start) > YIELD_AFTER_MS) {
                break;
            }
        }
    }

    /* Zero retcode means that there're no more pages to load */
    if (res >= 0) {
        res = res ? 0 : 1;
    }

    /* Process pending AIO ring events */
    tmp_res = ram_load_pages_flush(s);
    res = tmp_res ? tmp_res : res;

    /* Send EOS flag before section footer */
    qemu_put_be64(s->f_fd, RAM_SAVE_FLAG_EOS);
    qemu_fflush(s->f_fd);

    return res;
}

static int ram_load_memory(StateLoadCtx *s)
{
    SectionHandlersEntry *se;
    int res;

    se = find_se("ram", 0);
    assert(se && se->ops->load_state_iterate);

    /* Send section header with QEMU_VM_SECTION_PART type */
    send_section_header_part_end(s->f_fd, se, QEMU_VM_SECTION_PART);
    res = se->ops->load_state_iterate(s->f_fd, s, se->real_version_id);
    send_section_footer(s->f_fd, se);

    return res;
}

static int default_load(QEMUFile *f, void *opaque, int version_id)
{
    error_report("Unexpected (non-iterable device state) section");
    return -EINVAL;
}

static int load_state_header(StateLoadCtx *s)
{
    QEMUFile *f = s->f_vmstate;
    int v;

    /* Validate magic */
    v = qemu_get_be32(f);
    if (v != VMSTATE_HEADER_MAGIC) {
        error_report("Not a valid snapshot");
        return -EINVAL;
    }

    v = qemu_get_be32(f);
    if (v != page_size) {
        error_report("Incompatible page size: got %d expected %d",
                     v, (int) page_size);
        return -EINVAL;
    }

    /* Number of non-zero pages in all RAM blocks */
    ram_ctx.normal_pages = qemu_get_be64(f);

    /* vmstate stream offsets, counted from QEMU_VM_FILE_MAGIC */
    s->ram_list_offset = qemu_get_be32(f);
    s->device_offset = qemu_get_be32(f);
    s->eof_offset = qemu_get_be32(f);

    /* Check that offsets are within the limits */
    if ((VMSTATE_HEADER_SIZE + s->device_offset) > INPLACE_READ_MAX ||
            s->device_offset <= s->ram_list_offset) {
        error_report("Corrupted snapshot header");
        return -EINVAL;
    }

    /* Skip up to RAM block list section */
    qemu_file_skip(f, s->ram_list_offset);

    return 0;
}

static int load_state_ramlist(StateLoadCtx *s)
{
    QEMUFile *f = s->f_vmstate;
    uint8_t section_type;
    int res;

    section_type = qemu_get_byte(f);

    if (section_type == QEMU_VM_EOF) {
        error_report("Unexpected EOF token");
        return -EINVAL;
    } else if (section_type != QEMU_VM_SECTION_FULL &&
               section_type != QEMU_VM_SECTION_START) {
        error_report("Unexpected section type %d", section_type);
        return -EINVAL;
    }

    res = load_section_start_full(s);
    if (!res) {
        ram_block_list_init_bitmaps();
    }

    return res;
}

static int load_state_complete(StateLoadCtx *s)
{
    if (!s->in_postcopy) {
        /* Forward non-iterable device state */
        qemu_fsplice(s->f_fd, s->f_vmstate, s->eof_offset - s->device_offset);
    } else {
        /* Send terminating QEMU_VM_EOF if in postcopy */
        qemu_put_byte(s->f_fd, QEMU_VM_EOF);
    }

    qemu_fflush(s->f_fd);

    return 1;
}

static int load_section_start_full(StateLoadCtx *s)
{
    QEMUFile *f = s->f_vmstate;
    int section_id;
    int instance_id;
    int version_id;
    char idstr[256];
    SectionHandlersEntry *se;
    int res;

    section_id = qemu_get_be32(f);

    if (!qemu_get_counted_string(f, idstr)) {
        error_report("Failed to get section name(%d)", section_id);
        return -EINVAL;
    }

    instance_id = qemu_get_be32(f);
    version_id = qemu_get_be32(f);

    /* Find section handler */
    se = find_se(idstr, instance_id);
    if (!se) {
        se = &section_handlers.default_;
    } else if (version_id > se->version_id) {
        /* Validate version */
        error_report("Unsupported version %d for '%s' v%d",
                     version_id, idstr, se->version_id);
        return -EINVAL;
    }

    se->real_section_id = section_id;
    se->real_version_id = version_id;

    res = se->ops->load_state(f, s, se->real_version_id);
    if (res) {
        return res;
    }

    if (!check_section_footer(f, se)) {
        return -EINVAL;
    }

    return 0;
}

static int send_state_leader(StateLoadCtx *s)
{
    qemu_put_buffer(s->f_fd, s->ioc_leader->data + VMSTATE_HEADER_SIZE,
                    s->device_offset);
    return qemu_file_get_error(s->f_fd);
}

int coroutine_fn load_state_main(StateLoadCtx *s)
{
    int res;

    res = load_state_header(s);
    if (res) {
        goto fail;
    }

    res = load_state_ramlist(s);
    if (res) {
        goto fail;
    }

    res = send_state_leader(s);
    if (res) {
        goto fail;
    }

    if (s->postcopy) {
        res = prepare_postcopy(s);
        if (res) {
            goto fail;
        }
    }

    do {
        res = ram_load_memory(s);
        /* Check for file errors */
        load_state_check_errors(s, &res);

        /* Switch to postcopy? */
        if (!res && s->postcopy && !s->in_postcopy && is_postcopy_switchover(s)) {
            res = start_postcopy(s);
        }
    } while (!res);

    if (res == 1) {
        res = load_state_complete(s);
    }

fail:
    load_state_check_errors(s, &res);

    /* Replace positive retcode with 0 */
    return MIN(res, 0);
}

/* Initialize snapshot RAM state */
void ram_init_state(void)
{
    RAMCtx *ram = &ram_ctx;

    memset(ram, 0, sizeof(ram_ctx));

    /* Initialize RAM block list head */
    QSIMPLEQ_INIT(&ram->ram_block_list);

    /* Initialize postcopy page request queue */
    qemu_mutex_init(&ram->page_req_mutex);
    QSIMPLEQ_INIT(&ram->page_req);
}

/* Destroy snapshot RAM state */
void ram_destroy_state(void)
{
    RAMBlock *block;
    RAMBlock *next_block;

    /* Free RAM blocks */
    QSIMPLEQ_FOREACH_SAFE(block, &ram_ctx.ram_block_list, next, next_block) {
        g_free(block->bitmap);
        g_free(block);
    }

    /* Destroy page request mutex */
    qemu_mutex_destroy(&ram_ctx.page_req_mutex);
}
