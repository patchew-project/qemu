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

#include "qemu/osdep.h"
#include "qemu/stats64.h"
#include "qemu/timer.h"
#include "qemu-file.h"
#include "migration-stats.h"

MigrationAtomicStats mig_stats;

void calculate_time_since(Stat64 *val, int64_t since)
{
    int64_t now = qemu_clock_get_ms(QEMU_CLOCK_HOST);
    stat64_set(val, now - since);
}

bool migration_rate_limit_exceeded(QEMUFile *f)
{
    if (qemu_file_get_error(f)) {
        return true;
    }

    uint64_t rate_limit_used = stat64_get(&mig_stats.rate_limit_used);
    uint64_t rate_limit_max = stat64_get(&mig_stats.rate_limit_max);
    /*
     *  rate_limit_max == 0 means no rate_limit enfoncement.
     */
    if (rate_limit_max > 0 && rate_limit_used > rate_limit_max) {
        return true;
    }
    return false;
}

uint64_t migration_rate_limit_get(void)
{
    return stat64_get(&mig_stats.rate_limit_max);
}

void migration_rate_limit_set(uint64_t limit)
{
    /*
     * 'limit' is per second.  But we check it each BUFER_DELAY miliseconds.
     */
    stat64_set(&mig_stats.rate_limit_max, limit);
}

void migration_rate_limit_reset(void)
{
    stat64_set(&mig_stats.rate_limit_used, 0);
}

void migration_rate_limit_account(uint64_t len)
{
    stat64_add(&mig_stats.rate_limit_used, len);
}
