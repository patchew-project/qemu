/*
 *  Migration Threads info
 *
 *  Copyright (c) 2022 HUAWEI TECHNOLOGIES CO., LTD.
 *
 *  Authors:
 *  Jiang Jiacheng <jiangjiacheng@huawei.com>
 *
 *  This work is licensed under the terms of the GNU GPL, version 2 or later.
 *  See the COPYING file in the top-level directory.
 */

#include "threadinfo.h"

static QLIST_HEAD(, MigrationThread) migration_threads;

MigrationThread* MigrationThreadAdd(const char *name, int thread_id)
{
    MigrationThread *thread = NULL;

    thread = g_new0(MigrationThread, 1);
    thread->name = (char*)name;
    thread->thread_id = thread_id;

    QLIST_INSERT_HEAD(&migration_threads, thread, node);

    return thread;
}

void MigrationThreadDel(MigrationThread* thread)
{
    if (thread) {
        QLIST_REMOVE(thread, node);
	    g_free(thread);
    }
}

MigrationThread* MigrationThreadQuery(const char* name)
{
    MigrationThread *thread = NULL;

    QLIST_FOREACH(thread, &migration_threads, node) {
        if (!strcmp(thread->name, name)) {
            return thread;
        }
    }

    return NULL;
}

MigrationThreadInfo* qmp_query_migrationthreads(const char* name, Error **errp)
{
    MigrationThread *thread = NULL;
    MigrationThreadInfo *info = NULL;

    thread = MigrationThreadQuery(name);
    if (!thread) {
        goto err;
    }

    info = g_new0(MigrationThreadInfo, 1);
    info->name = g_strdup(thread->name);
    info->thread_id = thread->thread_id;

    return info;

err:
    error_setg(errp, "thread '%s' doesn't exist", name);
    return NULL;
}
