/*
 * QEMU migration miscellaneus exported functions
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef MIGRATION_MISC_H
#define MIGRATION_MISC_H

#include "exec/cpu-common.h"
#include "qemu/notify.h"
#include "qemu/thread.h"

/* migration/ram.c */

typedef enum RamSaveState {
    RAM_SAVE_ERR = 0,
    RAM_SAVE_RESET = 1,
    RAM_SAVE_BEFORE_SYNC_BITMAP = 2,
    RAM_SAVE_AFTER_SYNC_BITMAP = 3,
    RAM_SAVE_MAX = 4,
} RamSaveState;

/* State of RAM for migration */
typedef struct RAMState {
    /* QEMUFile used for this migration */
    QEMUFile *f;
    /* Last block that we have visited searching for dirty pages */
    RAMBlock *last_seen_block;
    /* Last block from where we have sent data */
    RAMBlock *last_sent_block;
    /* Last dirty target page we have sent */
    ram_addr_t last_page;
    /* last ram version we have seen */
    uint32_t last_version;
    /* We are in the first round */
    bool ram_bulk_stage;
    /* How many times we have dirty too many pages */
    int dirty_rate_high_cnt;
    /* ram save states used for notifiers */
    int ram_save_state;
    /* these variables are used for bitmap sync */
    /* last time we did a full bitmap_sync */
    int64_t time_last_bitmap_sync;
    /* bytes transferred at start_time */
    uint64_t bytes_xfer_prev;
    /* number of dirty pages since start_time */
    uint64_t num_dirty_pages_period;
    /* xbzrle misses since the beginning of the period */
    uint64_t xbzrle_cache_miss_prev;
    /* number of iterations at the beginning of period */
    uint64_t iterations_prev;
    /* Iterations since start */
    uint64_t iterations;
    /* number of dirty bits in the bitmap */
    uint64_t migration_dirty_pages;
    /* protects modification of the bitmap */
    QemuMutex bitmap_mutex;
    /* The RAMBlock used in the last src_page_requests */
    RAMBlock *last_req_rb;
    /* Queue of outstanding page requests from the destination */
    QemuMutex src_page_req_mutex;
    QSIMPLEQ_HEAD(src_page_requests, RAMSrcPageRequest) src_page_requests;
} RAMState;

void add_ram_save_state_change_notifier(Notifier *notify);
void ram_mig_init(void);
void qemu_guest_free_page_hint(void *addr, size_t len);

/* migration/block.c */

#ifdef CONFIG_LIVE_BLOCK_MIGRATION
void blk_mig_init(void);
#else
static inline void blk_mig_init(void) {}
#endif

#define SELF_ANNOUNCE_ROUNDS 5

static inline
int64_t self_announce_delay(int round)
{
    assert(round < SELF_ANNOUNCE_ROUNDS && round > 0);
    /* delay 50ms, 150ms, 250ms, ... */
    return 50 + (SELF_ANNOUNCE_ROUNDS - round - 1) * 100;
}

/* migration/savevm.c */

void dump_vmstate_json_to_file(FILE *out_fp);

/* migration/migration.c */
void migration_object_init(void);
void migration_object_finalize(void);
void qemu_start_incoming_migration(const char *uri, Error **errp);
bool migration_is_idle(void);
void add_migration_state_change_notifier(Notifier *notify);
void remove_migration_state_change_notifier(Notifier *notify);
bool migration_in_setup(MigrationState *);
bool migration_has_finished(MigrationState *);
bool migration_has_failed(MigrationState *);
/* ...and after the device transmission */
bool migration_in_postcopy_after_devices(MigrationState *);
void migration_global_dump(Monitor *mon);

/* migration/block-dirty-bitmap.c */
void dirty_bitmap_mig_init(void);

#endif
