/*
 * QTest testcases for migration compression
 *
 * Copyright (c) 2016-2018 Red Hat, Inc. and/or its affiliates
 *   based on the vhost-user-test.c that is:
 *      Copyright (c) 2014 Virtual Open Systems Sarl.
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


static char *tmpfs;

#ifdef CONFIG_ZSTD
static void test_multifd_tcp_zstd(char *name, MigrateCommon *args)
{
    args->listen_uri = "tcp:127.0.0.1:0";

    args->start.incoming_defer = true;

    qdict_put_bool(args->start.config, "multifd", true);

    /* temporary */
    qdict_put_bool(args->start.config, "use-config", true);

    test_precopy_common(args);
}

static void test_multifd_postcopy_tcp_zstd(char *name, MigrateCommon *args)
{
    args->listen_uri = "tcp:127.0.0.1:0";

    args->start.incoming_defer = true;

    qdict_put_bool(args->start.config, "multifd", true);
    qdict_put_bool(args->start.config, "postcopy-ram", true);
    qdict_put_int(args->start.config, "multifd-zstd-level", 2);
    qdict_put_str(args->start.config, "multifd-compression", "zstd");

    /* temporary */
    qdict_put_bool(args->start.config, "use-config", true);

    test_precopy_common(args);
}
#endif /* CONFIG_ZSTD */

#ifdef CONFIG_QATZIP
static void test_multifd_tcp_qatzip(char *name, MigrateCommon *args)
{
    args->listen_uri = "tcp:127.0.0.1:0";

    args->start.incoming_defer = true;

    qdict_put_bool(args->start.config, "multifd", true);
    qdict_put_int(args->start.config, "multifd-qatzip-level", 2);
    qdict_put_str(args->start.config, "multifd-compression", "qatzip");

    /* temporary */
    qdict_put_bool(args->start.config, "use-config", true);

    test_precopy_common(args);
}
#endif

#ifdef CONFIG_QPL
static void test_multifd_tcp_qpl(char *name, MigrateCommon *args)
{
    args->listen_uri = "tcp:127.0.0.1:0";

    args->start.incoming_defer = true;

    qdict_put_bool(args->start.config, "multifd", true);
    qdict_put_str(args->start.config, "multifd-compression", "qpl");

    /* temporary */
    qdict_put_bool(args->start.config, "use-config", true);

    test_precopy_common(args);
}
#endif /* CONFIG_QPL */

#ifdef CONFIG_UADK
static void test_multifd_tcp_uadk(char *name, MigrateCommon *args)
{
    args->listen_uri = "tcp:127.0.0.1:0";

    args->start.incoming_defer = true;

    qdict_put_bool(args->start.config, "multifd", true);
    qdict_put_str(args->start.config, "multifd-compression", "uadk");

    /* temporary */
    qdict_put_bool(args->start.config, "use-config", true);

    test_precopy_common(args);
}
#endif /* CONFIG_UADK */

static void test_precopy_unix_xbzrle(char *name, MigrateCommon *args)
{
    g_autofree char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);

    args->connect_uri = uri;
    args->listen_uri = uri;
    args->iterations = 2;
    /*
     * XBZRLE needs pages to be modified when doing the 2nd+ round
     * iteration to have real data pushed to the stream.
     */
    args->live = true;

    qdict_put_bool(args->start.config, "xbzrle", true);
    qdict_put_int(args->start.config, "xbzrle-cache-size", 33554432);

    /* temporary */
    qdict_put_bool(args->start.config, "use-config", true);

    test_precopy_common(args);
}

static void test_multifd_tcp_zlib(char *name, MigrateCommon *args)
{
    args->listen_uri = "tcp:127.0.0.1:0";

    args->start.incoming_defer = true;
    args->start.caps[MIGRATION_CAPABILITY_MULTIFD] = true;

    /*
     * Overloading this test to also check that set_parameter does not error.
     * This is also done in the tests for the other compression methods.
     */
    qdict_put_int(args->start.config, "multifd-zlib-level", 2);
    qdict_put_str(args->start.config, "multifd-compression", "zlib");

    /* temporary */
    qdict_put_bool(args->start.config, "use-config", true);

    test_precopy_common(args);
}

static void migration_test_add_compression_smoke(MigrationTestEnv *env)
{
    migration_test_add("/migration/multifd/tcp/plain/zlib",
                       test_multifd_tcp_zlib);
}

void migration_test_add_compression(MigrationTestEnv *env)
{
    tmpfs = env->tmpfs;

    migration_test_add_compression_smoke(env);

    if (!env->full_set) {
        return;
    }

#ifdef CONFIG_ZSTD
    migration_test_add("/migration/multifd/tcp/plain/zstd",
                       test_multifd_tcp_zstd);
    if (env->has_uffd) {
        migration_test_add("/migration/multifd+postcopy/tcp/plain/zstd",
                           test_multifd_postcopy_tcp_zstd);
    }
#endif

#ifdef CONFIG_QATZIP
    migration_test_add("/migration/multifd/tcp/plain/qatzip",
                       test_multifd_tcp_qatzip);
#endif

#ifdef CONFIG_QPL
    migration_test_add("/migration/multifd/tcp/plain/qpl",
                       test_multifd_tcp_qpl);
#endif

#ifdef CONFIG_UADK
    migration_test_add("/migration/multifd/tcp/plain/uadk",
                       test_multifd_tcp_uadk);
#endif

    if (g_test_slow()) {
        migration_test_add("/migration/precopy/unix/xbzrle",
                           test_precopy_unix_xbzrle);
    }
}
