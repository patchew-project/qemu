/*
 * Copyright (c) 2022 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#ifndef STATS_H
#define STATS_H

/*
 * Add qmp stats callbacks to the stats_callbacks list.
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
                         void (*schemas_fn)(StatsSchemaResult *, Error **));

/* Stats helpers routines */
StatsResultsEntry *add_vm_stats_entry(StatsResults *, StatsProvider);
StatsResultsEntry *add_vcpu_stats_entry(StatsResults *, StatsProvider, char *);
StatsSchemaProvider *add_vm_stats_schema(StatsSchemaResult *, StatsProvider);
StatsSchemaProvider *add_vcpu_stats_schema(StatsSchemaResult *, StatsProvider);

bool stat_name_filter(StatsFilter *, StatsTarget, char *);
bool stat_cpu_filter(StatsFilter *, char *);

#endif /* STATS_H */
