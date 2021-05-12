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

    unsigned long *bitmap;      /* Bitmap of RAM slices */

    /* Link into ram_list */
    QSIMPLEQ_ENTRY(RAMBlock) next;

    char idstr[256];            /* RAM block id string */
} RAMBlock;

/* RAM block page */
typedef struct RAMPage {
    RAMBlock *block;            /* RAM block containing the page */
    int64_t offset;             /* Page offset in RAM block */
} RAMPage;

/* RAM transfer context */
typedef struct RAMCtx {
    int64_t normal_pages;       /* Total number of normal pages */

    /* RAM block list head */
    QSIMPLEQ_HEAD(, RAMBlock) ram_block_list;
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
static int ram_save(QEMUFile *f, void *opaque, int version_id);
static int save_state_complete(StateSaveCtx *s);

static RAMCtx ram_ctx;

static SectionHandlerOps default_handler_ops = {
    .save_state = default_save,
};

static SectionHandlerOps ram_handler_ops = {
    .save_state = ram_save,
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

int coroutine_fn load_state_main(StateLoadCtx *s)
{
    /* TODO: implement */
    return 0;
}

/* Initialize snapshot RAM state */
void ram_init_state(void)
{
    RAMCtx *ram = &ram_ctx;

    memset(ram, 0, sizeof(ram_ctx));
}

/* Destroy snapshot RAM state */
void ram_destroy_state(void)
{
    /* TODO: implement */
}
