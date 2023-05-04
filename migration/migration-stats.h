/*
 * Migration stats
 *
 * Copyright (c) 2012-2023 Red Hat Inc
 *
 * Authors:
 *  Juan Quintela <quintela@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_MIGRATION_STATS_H
#define QEMU_MIGRATION_STATS_H

#include "qemu/stats64.h"

/*
 * These are the ram migration statistic counters.  It is loosely
 * based on MigrationStats.  We change to Stat64 any counter that
 * needs to be updated using atomic ops (can be accessed by more than
 * one thread).
 */
typedef struct {
    /*
     * number of bytes that were dirty last time that we sync with the
     * guest memory.  We use that to calculate the downtime.  As the
     * remaining dirty amounts to what we know that is still dirty
     * since last iteration, not counting what the guest has dirtied
     * sync we synchronize bitmaps.
     */
    Stat64 dirty_bytes_last_sync;
    /*
     * number of pages dirtied by second.
     */
    Stat64 dirty_pages_rate;
    /*
     * number of times we have synchronize guest bitmaps.
     */
    Stat64 dirty_sync_count;
    /*
     * number of times zero copy failed to send any page using zero
     * copy.
     */
    Stat64 dirty_sync_missed_zero_copy;
    /*
     * number of bytes sent at migration completion stage while the
     * guest is stopped.
     */
    Stat64 downtime_bytes;
    /*
     * number of bytes sent through multifd channels.
     */
    Stat64 multifd_bytes;
    /*
     * number of pages transferred that were not full of zeros.
     */
    Stat64 normal_pages;
    /*
     * number of bytes sent during postcopy.
     */
    Stat64 postcopy_bytes;
    /*
     * number of postcopy page faults that we have handled during
     * postocpy stage.
     */
    Stat64 postcopy_requests;
    /*
     *  number of bytes sent during precopy stage.
     */
    Stat64 precopy_bytes;
    /*
     * total number of bytes transferred.
     */
    Stat64 transferred;
    /*
     * number of pages transferred that were full of zeros.
     */
    Stat64 zero_pages;
} MigrationAtomicStats;

extern MigrationAtomicStats mig_stats;

#endif
