/*
 * Copyright (c) 2022 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#ifndef STATS_H
#define STATS_H

#include "qapi/qapi-types-stats.h"

/*
 * Add QMP stats callbacks to the stats_callbacks list.
 *
 * @provider: stats provider
 *
 * @stats_fn: routine to query stats:
 *    void (*stats_fn)(StatsResults *results, StatsFilter *filter, Error **errp)
 *
 * @schema_fn: routine to query stat schemas:
 *    void (*schemas_fn)(StatsSchemaResult *results, Error **errp)
 */
void add_stats_callbacks(StatsProvider provider,
                         void (*stats_fn)(StatsResults *, StatsFilter *,
                                          Error **),
                         void (*schemas_fn)(StatsSchemaResults *, Error **));

/*
 * Helper routines for adding stats entries to the results lists.
 */
void add_vm_stats_entry(StatsList *, StatsResults *, StatsProvider);
void add_vcpu_stats_entry(StatsList *, StatsResults *, StatsProvider, char *);
void add_vm_stats_schema(StatsSchemaValueList *, StatsSchemaResults *,
                         StatsProvider);
void add_vcpu_stats_schema(StatsSchemaValueList *, StatsSchemaResults *,
                           StatsProvider);

/*
 * True if a stat name and provider match a filter or if no corresponding
 * filters are defined. False otherwise.
 */
bool stats_requested_name(const char *, StatsProvider, StatsFilter *);

/*
 * True if a vcpu qom path and provider match a filter or if no corresponding
 * filters are defined. False otherwise.
 */
bool stats_requested_vcpu(const char *, StatsProvider, StatsFilter *);

#endif /* STATS_H */
