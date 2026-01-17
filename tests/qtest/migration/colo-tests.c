/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * QTest testcases for COLO migration
 *
 * Copyright (c) 2025 Lukas Straub <lukasstraub2@web.de>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "migration/framework.h"
#include "migration/migration-qmp.h"
#include "migration/migration-util.h"
#include "qemu/module.h"

static void test_colo_plain_common(MigrateCommon *args,
                                   bool failover_during_checkpoint,
                                   bool primary_failover)
{
    args->listen_uri = "tcp:127.0.0.1:0";
    test_colo_common(args, failover_during_checkpoint, primary_failover);
}

static void *hook_start_multifd(QTestState *from, QTestState *to)
{
    return migrate_hook_start_precopy_tcp_multifd_common(from, to, "none");
}

static void test_colo_multifd_common(MigrateCommon *args,
                                     bool failover_during_checkpoint,
                                     bool primary_failover)
{
    args->listen_uri = "defer";
    args->start_hook = hook_start_multifd;
    args->start.caps[MIGRATION_CAPABILITY_MULTIFD] = true;
    test_colo_common(args, failover_during_checkpoint, primary_failover);
}

static void test_colo_plain_primary_failover(char *name, MigrateCommon *args)
{
    test_colo_plain_common(args, false, true);
}

static void test_colo_plain_secondary_failover(char *name, MigrateCommon *args)
{
    test_colo_plain_common(args, false, false);
}

static void test_colo_multifd_primary_failover(char *name, MigrateCommon *args)
{
    test_colo_multifd_common(args, false, true);
}

static void test_colo_multifd_secondary_failover(char *name,
                                                 MigrateCommon *args)
{
    test_colo_multifd_common(args, false, false);
}

static void test_colo_plain_primary_failover_checkpoint(char *name,
                                                        MigrateCommon *args)
{
    test_colo_plain_common(args, true, true);
}

static void test_colo_plain_secondary_failover_checkpoint(char *name,
                                                          MigrateCommon *args)
{
    test_colo_plain_common(args, true, false);
}

static void test_colo_multifd_primary_failover_checkpoint(char *name,
                                                          MigrateCommon *args)
{
    test_colo_multifd_common(args, true, true);
}

static void test_colo_multifd_secondary_failover_checkpoint(char *name,
                                                            MigrateCommon *args)
{
    test_colo_multifd_common(args, true, false);
}

void migration_test_add_colo(MigrationTestEnv *env)
{
    if (!env->full_set) {
        return;
    }

    migration_test_add("/migration/colo/plain/primary_failover",
                       test_colo_plain_primary_failover);
    migration_test_add("/migration/colo/plain/secondary_failover",
                       test_colo_plain_secondary_failover);

    migration_test_add("/migration/colo/multifd/primary_failover",
                       test_colo_multifd_primary_failover);
    migration_test_add("/migration/colo/multifd/secondary_failover",
                       test_colo_multifd_secondary_failover);

    migration_test_add("/migration/colo/plain/primary_failover_checkpoint",
                       test_colo_plain_primary_failover_checkpoint);
    migration_test_add("/migration/colo/plain/secondary_failover_checkpoint",
                       test_colo_plain_secondary_failover_checkpoint);

    migration_test_add("/migration/colo/multifd/primary_failover_checkpoint",
                       test_colo_multifd_primary_failover_checkpoint);
    migration_test_add("/migration/colo/multifd/secondary_failover_checkpoint",
                       test_colo_multifd_secondary_failover_checkpoint);
}
