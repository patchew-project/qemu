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
#include "migration-stats.h"

MigrationAtomicStats mig_stats;

void calculate_time_since(Stat64 *val, int64_t since)
{
    int64_t now = qemu_clock_get_ms(QEMU_CLOCK_HOST);
    stat64_set(val, now - since);
}
