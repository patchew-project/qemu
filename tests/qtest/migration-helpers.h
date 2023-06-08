/*
 * QTest migration helpers
 *
 * Copyright (c) 2016-2018 Red Hat, Inc. and/or its affiliates
 *   based on the vhost-user-test.c that is:
 *      Copyright (c) 2014 Virtual Open Systems Sarl.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef MIGRATION_HELPERS_H
#define MIGRATION_HELPERS_H

#include "libqtest.h"

extern char *tmpfs;

bool migrate_watch_for_stop(QTestState *who, const char *name,
                            QDict *event, void *opaque);
bool migrate_watch_for_resume(QTestState *who, const char *name,
                              QDict *event, void *opaque);

G_GNUC_PRINTF(3, 4)
void migrate_qmp(QTestState *who, const char *uri, const char *fmt, ...);

QDict *migrate_query(QTestState *who);
QDict *migrate_query_not_failed(QTestState *who);

void wait_for_migration_status(QTestState *who,
                               const char *goal, const char **ungoals);

void wait_for_migration_complete(QTestState *who);

void wait_for_migration_fail(QTestState *from, bool allow_active);

typedef struct {
    QTestState *qs;
    /* options for source and target */
    gchar *arch_opts;
    gchar *arch_source;
    gchar *arch_target;
    const gchar *extra_opts;
    const gchar *hide_stderr;
    gchar *kvm_opts;
    const gchar *memory_size;
    /*
     * name must *not* contain "target" if it is the target of a
     * migration.
     */
    const gchar *name;
    gchar *serial_path;
    gchar *shmem_opts;
    gchar *shmem_path;
    gchar *unix_socket;
    gchar *uri;
    unsigned start_address;
    unsigned end_address;
    bool got_event;
} GuestState;

GuestState *guest_create(const char *name);
void guest_destroy(GuestState *vm);
void guest_realize(GuestState *who);
void guest_use_dirty_ring(GuestState *vm);

void wait_for_serial(GuestState *vm);

void bootfile_create(char *dir);
void bootfile_delete(void);

bool kvm_dirty_ring_supported(void);

#endif /* MIGRATION_HELPERS_H */
