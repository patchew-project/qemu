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

    char idstr[256];            /* RAM block id string */

    /* Link into ram_list */
    QSIMPLEQ_ENTRY(RAMBlockDesc) next;
} RAMBlockDesc;

/* State reflecting RAM part of snapshot */
typedef struct RAMState {
    int64_t page_size;          /* Page size */
    int64_t page_mask;          /* Page mask */
    int page_bits;              /* Page size bits */

    int64_t normal_pages;       /* Total number of normal (non-zero) pages */

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
static int ram_save(QEMUFile *f, void *opaque, int version_id);
static int save_state_complete(SnapSaveState *sn);

static RAMState ram_state;

static SectionHandlerOps default_handler_ops = {
    .save_section = default_save,
    .load_section = NULL,
};

static SectionHandlerOps ram_handler_ops = {
    .save_section = ram_save,
    .load_section = NULL,
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

/* Load snapshot data and send it with outgoing migration stream */
int coroutine_fn snap_load_state_main(SnapLoadState *sn)
{
    /* TODO: implement */
    return 0;
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
}
