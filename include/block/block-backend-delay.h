/*
 * Copyright (c) 2026 Hanna Czenczek <hreitz@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BLOCK_BACKEND_DELAY_H
#define BLOCK_BACKEND_DELAY_H

#include "qemu/thread.h"
#include "qemu/timer.h"
#include "qapi/qapi-types-block.h"

/* Enumeration of I/O delay threshold types */
enum BlockDelayThreshold {
    /* Latency threshold at which to report a delay */
    BLOCK_DELAY_REPORT,
    /* Latency threshold at which to auto-stop the VM */
    BLOCK_DELAY_STOP,

    /* Number of different thresholds */
    BLOCK_DELAY__MAX,
};

typedef struct BlockDelayTracker {
    /* Timer to regularly check requests for delay */
    QEMUTimer *timer;

    /* Which clock to use */
    QEMUClockType clock_type;

    /*
     * Protects access to all subsequent fields in this struct. Do not take any
     * other lock while this lock is taken.
     */
    QemuMutex lock;

    /* Thresholds for generating delay report events or stopping the VM */
    uint64_t threshold_ns[BLOCK_DELAY__MAX];

    /* List of active requests; new requests appended at the end */
    QTAILQ_HEAD(, BlockDelayTrackerRequest) requests;
} BlockDelayTracker;

typedef struct BlockDelayTrackerRequest {
    /* offset < 0 means no valid offset */
    int64_t offset;
    /* Request length, or 0 if no valid length */
    uint64_t bytes;
    /* When the request was started */
    uint64_t start_time_ns;
    /* Operation type */
    IoAccountingOperation op_type;
    /*
     * Whether any delay threshold has been exceeded.
     * If so, we need to emit BLOCK_IO_DONE when the request is done.
     */
    bool threshold_exceeded[BLOCK_DELAY__MAX];
    /*
     * Set the first time any threshold is exceeded, to cache the device name
     * for BLOCK_IO_DONE.
     */
    char *device_path;

    /* Entry in the list of all running requests for this BB */
    QTAILQ_ENTRY(BlockDelayTrackerRequest) list;
} BlockDelayTrackerRequest;

/**
 * Set up the request latency delay tracker for this BlockBackend.
 *
 * The caller must allocate blk->delay_tracker beforehand (because
 * block-backend-delay.c cannot access BlockBackend fields directly).
 */
void blk_delay_tracker_init(BlockBackend *blk);

/**
 * Stop and destroy the request latency delay tracker for this BlockBackend.
 */
void blk_delay_tracker_destroy(BlockDelayTracker *bdt);

/**
 * Return the latency delay tracker object for the given BlockBackend.
 *
 * (Implemented in block-backend.c, where the BlockBackend definition resides.)
 */
BlockDelayTracker *blk_delay_tracker(BlockBackend *blk);

/**
 * Begin tracking an I/O request on @blk of type @op_type over @bytes starting
 * from @offset.
 *
 * @request is initialized by this function.
 *
 * Use offset=-1 for operations that have no valid offset (e.g. flush).
 */
void blk_track_delay_begin(BlockBackend *blk,
                           BlockDelayTrackerRequest *request,
                           int64_t offset,
                           uint64_t bytes,
                           IoAccountingOperation op_type);
/**
 * Finalize I/O request tracking.
 *
 * @ret indicates the operation's status, with 0 for success and -errno for
 * errors.
 */
void blk_track_delay_end(BlockBackend *blk,
                         BlockDelayTrackerRequest *request,
                         int ret);

#endif
