/*
 * QTest testcases for TLS migration
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
#include "crypto/tlscredspsk.h"
#include "libqtest.h"
#include "migration/framework.h"
#include "migration/migration-qmp.h"
#include "migration/migration-util.h"

#include "tests/unit/crypto-tls-psk-helpers.h"
#ifdef CONFIG_TASN1
# include "tests/unit/crypto-tls-x509-helpers.h"
#endif /* CONFIG_TASN1 */


typedef struct {
    char *workdir;
    char *workdiralt;
    char *pskfile;
    char *pskfilealt;
} TestMigrateTLSPSKData;

typedef struct {
    bool mismatch;
} TestMigrateTLSPSK;

static TestMigrateTLSPSK tls_psk_match = {
    .mismatch = false,
};

static TestMigrateTLSPSK tls_psk_mismatch = {
    .mismatch = true,
};

static char *tmpfs;

static void *migrate_hook_start_tls_psk_common(QTestState *from,
                                               QTestState *to,
                                               void *opaque)
{
    TestMigrateTLSPSK *args = opaque;
    g_autofree char *workdir = g_strdup_printf("%s/tlscredspsk0", tmpfs);
    g_autofree char *workdiralt = NULL;

    if (args->mismatch) {
        workdiralt = g_strdup_printf("%s/tlscredspskalt0", tmpfs);
    }

    qtest_qmp_assert_success(from,
                             "{ 'execute': 'object-add',"
                             "  'arguments': { 'qom-type': 'tls-creds-psk',"
                             "                 'id': 'tlscredspsk0',"
                             "                 'endpoint': 'client',"
                             "                 'dir': %s,"
                             "                 'username': 'qemu'} }",
                             workdir);

    qtest_qmp_assert_success(to,
                             "{ 'execute': 'object-add',"
                             "  'arguments': { 'qom-type': 'tls-creds-psk',"
                             "                 'id': 'tlscredspsk0',"
                             "                 'endpoint': 'server',"
                             "                 'dir': %s } }",
                             args->mismatch ? workdiralt : workdir);

    return NULL;
}

static void migrate_tls_psk_init(MigrateCommon *args,
                                 TestMigrateTLSPSK *test_args,
                                 TestMigrateTLSPSKData *data)
{
    data->workdir = g_strdup_printf("%s/tlscredspsk0", tmpfs);
    data->pskfile = g_strdup_printf("%s/%s", data->workdir,
                                    QCRYPTO_TLS_CREDS_PSKFILE);
    g_mkdir_with_parents(data->workdir, 0700);
    test_tls_psk_init(data->pskfile);

    if (test_args->mismatch) {
        data->workdiralt = g_strdup_printf("%s/tlscredspskalt0", tmpfs);
        data->pskfilealt = g_strdup_printf("%s/%s", data->workdiralt,
                                           QCRYPTO_TLS_CREDS_PSKFILE);
        g_mkdir_with_parents(data->workdiralt, 0700);
        test_tls_psk_init_alt(data->pskfilealt);
    }
}

static void migrate_tls_psk_cleanup(TestMigrateTLSPSKData *data)
{
    test_tls_psk_cleanup(data->pskfile);
    if (data->pskfilealt) {
        test_tls_psk_cleanup(data->pskfilealt);
    }
    rmdir(data->workdir);
    if (data->workdiralt) {
        rmdir(data->workdiralt);
    }

    g_free(data->workdiralt);
    g_free(data->pskfilealt);
    g_free(data->workdir);
    g_free(data->pskfile);
    g_free(data);
}

static void test_precopy_tls_psk_common(MigrateCommon *args,
                                        TestMigrateTLSPSK *test_args)
{
    TestMigrateTLSPSKData *data = g_new0(TestMigrateTLSPSKData, 1);

    /* temporary */
    qdict_put_bool(args->start.config, "use-config", true);

    qdict_put_str(args->start.config, "tls-creds", "tlscredspsk0");

    migrate_tls_psk_init(args, test_args, data);
    test_precopy_common(args);
    migrate_tls_psk_cleanup(data);
}

static void test_postcopy_tls_psk_common(MigrateCommon *args,
                                        TestMigrateTLSPSK *test_args)
{
    TestMigrateTLSPSKData *data = g_new0(TestMigrateTLSPSKData, 1);

    qdict_put_str(args->start.config, "tls-creds", "tlscredspsk0");

    migrate_tls_psk_init(args, test_args, data);
    test_postcopy_common(args);
    migrate_tls_psk_cleanup(data);
}

static void test_postcopy_recovery_tls_psk_common(MigrateCommon *args,
                                                  TestMigrateTLSPSK *test_args)
{
    TestMigrateTLSPSKData *data = g_new0(TestMigrateTLSPSKData, 1);

    qdict_put_str(args->start.config, "tls-creds", "tlscredspsk0");

    migrate_tls_psk_init(args, test_args, data);
    test_postcopy_recovery_common(args);
    migrate_tls_psk_cleanup(data);
}

#ifdef CONFIG_TASN1
typedef struct {
    char *workdir;
    char *keyfile;
    char *cacert;
    char *servercert;
    char *serverkey;
    char *clientcert;
    char *clientkey;
} TestMigrateTLSX509Data;

typedef struct {
    bool verifyclient;
    bool clientcert;
    bool hostileclient;
    bool authzclient;
    const char *certhostname;
    const char *certipaddr;
} TestMigrateTLSX509;

/*
 * The normal case: match server's cert hostname against
 * whatever host we were telling QEMU to connect to (if any)
 */
static TestMigrateTLSX509 tls_x509_default_host = {
    .verifyclient = true,
    .clientcert = true,
    .certipaddr = "127.0.0.1"
};

/*
 * The unusual case: the server's cert is different from
 * the address we're telling QEMU to connect to (if any),
 * so we must give QEMU an explicit hostname to validate
 */
static TestMigrateTLSX509 tls_x509_override_host = {
    .verifyclient = true,
    .clientcert = true,
    .certhostname = "qemu.org",
};

/*
 * The unusual case: the server's cert is different from
 * the address we're telling QEMU to connect to, and so we
 * expect the client to reject the server
 */
static TestMigrateTLSX509 tls_x509_mismatch_host = {
    .verifyclient = true,
    .clientcert = true,
    .certipaddr = "10.0.0.1",
};

static TestMigrateTLSX509 x509_friendly_client = {
    .verifyclient = true,
    .clientcert = true,
    .authzclient = true,
    .certipaddr = "127.0.0.1",
};

static TestMigrateTLSX509 tls_x509_hostile_client = {
    .verifyclient = true,
    .clientcert = true,
    .hostileclient = true,
    .authzclient = true,
    .certipaddr = "127.0.0.1",
};

/*
 * The case with no client certificate presented,
 * and no server verification
 */
static TestMigrateTLSX509 tls_x509_allow_anon_client = {
    .certipaddr = "127.0.0.1",
};

/*
 * The case with no client certificate presented,
 * and server verification rejecting
 */
static TestMigrateTLSX509 tls_x509_reject_anon_client = {
    .verifyclient = true,
    .certipaddr = "127.0.0.1",
};

static TestMigrateTLSX509 tls_x509_no_host = {
    .verifyclient = true,
    .clientcert = true,
    .authzclient = true,
};

static void *
migrate_hook_start_tls_x509_common(QTestState *from,
                                   QTestState *to,
                                   void *opaque)
{
    TestMigrateTLSX509 *args = opaque;
    const char *workdir = g_strdup_printf("%s/tlscredsx5090", tmpfs);

    qtest_qmp_assert_success(from,
                             "{ 'execute': 'object-add',"
                             "  'arguments': { 'qom-type': 'tls-creds-x509',"
                             "                 'id': 'tlscredsx509client0',"
                             "                 'endpoint': 'client',"
                             "                 'dir': %s,"
                             "                 'sanity-check': true,"
                             "                 'verify-peer': true} }",
                             workdir);

    qtest_qmp_assert_success(to,
                             "{ 'execute': 'object-add',"
                             "  'arguments': { 'qom-type': 'tls-creds-x509',"
                             "                 'id': 'tlscredsx509server0',"
                             "                 'endpoint': 'server',"
                             "                 'dir': %s,"
                             "                 'sanity-check': true,"
                             "                 'verify-peer': %i} }",
                             workdir, args->verifyclient);

    if (args->authzclient) {
        qtest_qmp_assert_success(to,
                                 "{ 'execute': 'object-add',"
                                 "  'arguments': { 'qom-type': 'authz-simple',"
                                 "                 'id': 'tlsauthz0',"
                                 "                 'identity': %s} }",
                                 "CN=" QCRYPTO_TLS_TEST_CLIENT_NAME);
    }

    return NULL;
}

static void migrate_tls_x509_init(MigrateCommon *args,
                                  TestMigrateTLSX509 *test_args,
                                  TestMigrateTLSX509Data *data)
{
    data->workdir = g_strdup_printf("%s/tlscredsx5090", tmpfs);
    data->keyfile = g_strdup_printf("%s/key.pem", data->workdir);

    data->cacert = g_strdup_printf("%s/ca-cert.pem", data->workdir);
    data->serverkey = g_strdup_printf("%s/server-key.pem", data->workdir);
    data->servercert = g_strdup_printf("%s/server-cert.pem", data->workdir);
    if (test_args->clientcert) {
        data->clientkey = g_strdup_printf("%s/client-key.pem", data->workdir);
        data->clientcert = g_strdup_printf("%s/client-cert.pem", data->workdir);
    }

    g_mkdir_with_parents(data->workdir, 0700);

    test_tls_init(data->keyfile);
#ifndef _WIN32
    g_assert(link(data->keyfile, data->serverkey) == 0);
#else
    g_assert(CreateHardLink(data->serverkey, data->keyfile, NULL) != 0);
#endif
    if (test_args->clientcert) {
#ifndef _WIN32
        g_assert(link(data->keyfile, data->clientkey) == 0);
#else
        g_assert(CreateHardLink(data->clientkey, data->keyfile, NULL) != 0);
#endif
    }

    TLS_ROOT_REQ_SIMPLE(cacertreq, data->cacert);
    if (test_args->clientcert) {
        TLS_CERT_REQ_SIMPLE_CLIENT(servercertreq, cacertreq,
                                   test_args->hostileclient ?
                                   QCRYPTO_TLS_TEST_CLIENT_HOSTILE_NAME :
                                   QCRYPTO_TLS_TEST_CLIENT_NAME,
                                   data->clientcert);
        test_tls_deinit_cert(&servercertreq);
    }

    TLS_CERT_REQ_SIMPLE_SERVER(clientcertreq, cacertreq,
                               data->servercert,
                               test_args->certhostname,
                               test_args->certipaddr);
    test_tls_deinit_cert(&clientcertreq);
    test_tls_deinit_cert(&cacertreq);

    /*
     * These keys are not actual parameters, we need them for the test
     * code because it uses the same config object for both QEMUs.
     */
    qdict_put_str(args->start.config, "tmp-tls-client", "tlscredsx509client0");
    qdict_put_str(args->start.config, "tmp-tls-server", "tlscredsx509server0");

    if (test_args->certhostname) {
        qdict_put_str(args->start.config, "tls-hostname",
                      test_args->certhostname);
    } else {
        qdict_put_null(args->start.config, "tls-hostname");
    }

    if (test_args->authzclient) {
        qdict_put_str(args->start.config, "tls-authz", "tlsauthz0");
    }

    if (qdict_get_try_bool(args->start.config, "multifd", false)) {
        qdict_put_str(args->start.config, "multifd-compression", "none");
    }
}

static void migrate_tls_x509_cleanup(TestMigrateTLSX509Data *data)
{
    test_tls_cleanup(data->keyfile);
    g_free(data->keyfile);

    unlink(data->cacert);
    g_free(data->cacert);
    unlink(data->servercert);
    g_free(data->servercert);
    unlink(data->serverkey);
    g_free(data->serverkey);

    if (data->clientcert) {
        unlink(data->clientcert);
        g_free(data->clientcert);
    }
    if (data->clientkey) {
        unlink(data->clientkey);
        g_free(data->clientkey);
    }

    rmdir(data->workdir);
    g_free(data->workdir);

    g_free(data);
}

static void test_precopy_tls_x509_common(MigrateCommon *args,
                                         TestMigrateTLSX509 *test_args)
{
    TestMigrateTLSX509Data *data = g_new0(TestMigrateTLSX509Data, 1);

    /* temporary */
    qdict_put_bool(args->start.config, "use-config", true);

    migrate_tls_x509_init(args, test_args, data);
    test_precopy_common(args);
    migrate_tls_x509_cleanup(data);
}
#endif /* CONFIG_TASN1 */

static void test_postcopy_tls_psk(char *name, MigrateCommon *args)
{
    args->start_hook_full = migrate_hook_start_tls_psk_common;
    args->start_hook_data = &tls_psk_match;

    test_postcopy_tls_psk_common(args, &tls_psk_match);
}

static void test_postcopy_preempt_tls_psk(char *name, MigrateCommon *args)
{
    args->start_hook_full = migrate_hook_start_tls_psk_common;
    args->start_hook_data = &tls_psk_match;

    qdict_put_bool(args->start.config, "postcopy-preempt", true);

    test_postcopy_tls_psk_common(args, &tls_psk_match);
}

static void test_postcopy_recovery_tls_psk(char *name, MigrateCommon *args)
{
    args->start_hook_full = migrate_hook_start_tls_psk_common;
    args->start_hook_data = &tls_psk_match;

    test_postcopy_recovery_tls_psk_common(args, &tls_psk_match);
}

static void test_multifd_postcopy_recovery_tls_psk(char *name,
                                                   MigrateCommon *args)
{
    args->start_hook_full = migrate_hook_start_tls_psk_common;
    args->start_hook_data = &tls_psk_match;

    qdict_put_bool(args->start.config, "multifd", true);

    test_postcopy_recovery_tls_psk_common(args, &tls_psk_match);
}

/* This contains preempt+recovery+tls test altogether */
static void test_postcopy_preempt_all(char *name, MigrateCommon *args)
{
    args->start_hook_full = migrate_hook_start_tls_psk_common;
    args->start_hook_data = &tls_psk_match;

    qdict_put_bool(args->start.config, "postcopy-preempt", true);

    test_postcopy_recovery_tls_psk_common(args, &tls_psk_match);
}

static void test_multifd_postcopy_preempt_recovery_tls_psk(char *name,
                                                           MigrateCommon *args)
{
    args->start_hook_full = migrate_hook_start_tls_psk_common;
    args->start_hook_data = &tls_psk_match;

    qdict_put_bool(args->start.config, "multifd", true);
    qdict_put_bool(args->start.config, "postcopy-preempt", true);

    test_postcopy_recovery_tls_psk_common(args, &tls_psk_match);
}

static void test_precopy_unix_tls_psk(char *name, MigrateCommon *args)
{
    g_autofree char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);

    args->connect_uri = uri;
    args->listen_uri = uri;
    args->start_hook_full = migrate_hook_start_tls_psk_common;
    args->start_hook_data = &tls_psk_match;

    test_precopy_tls_psk_common(args, &tls_psk_match);
}

#ifdef CONFIG_TASN1
static void test_precopy_unix_tls_x509_default_host(char *name,
                                                    MigrateCommon *args)
{
    g_autofree char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);

    args->connect_uri = uri;
    args->listen_uri = uri;
    args->start_hook_full = migrate_hook_start_tls_x509_common;
    args->start_hook_data = &tls_x509_default_host;
    args->result = MIG_TEST_FAIL_DEST_QUIT_ERR;

    args->start.hide_stderr = true;

    test_precopy_tls_x509_common(args, &tls_x509_default_host);
}

static void test_precopy_unix_tls_x509_override_host(char *name,
                                                     MigrateCommon *args)
{
    g_autofree char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);

    args->connect_uri = uri;
    args->listen_uri = uri;
    args->start_hook_full = migrate_hook_start_tls_x509_common;
    args->start_hook_data = &tls_x509_override_host;

    test_precopy_tls_x509_common(args, &tls_x509_override_host);
}
#endif /* CONFIG_TASN1 */

static void test_precopy_tcp_tls_psk_match(char *name, MigrateCommon *args)
{
    args->listen_uri = "tcp:127.0.0.1:0";
    args->start_hook_full = migrate_hook_start_tls_psk_common;
    args->start_hook_data = &tls_psk_match;

    test_precopy_tls_psk_common(args, &tls_psk_match);
}

static void test_precopy_tcp_tls_psk_mismatch(char *name, MigrateCommon *args)
{
    args->listen_uri = "tcp:127.0.0.1:0";
    args->start_hook_full = migrate_hook_start_tls_psk_common;
    args->start_hook_data = &tls_psk_mismatch;
    args->result = MIG_TEST_FAIL;

    args->start.hide_stderr = true;

    test_precopy_tls_psk_common(args, &tls_psk_mismatch);
}

static void test_precopy_tcp_no_tls(char *name, MigrateCommon *args)
{
    args->listen_uri = "tcp:127.0.0.1:0";

    qdict_put_null(args->start.config, "tls-creds");

    test_precopy_common(args);
}

static void test_precopy_tcp_tls_no_hostname(char *name, MigrateCommon *args)
{
    args->listen_uri = "tcp:127.0.0.1:0";
    args->start_hook_full = migrate_hook_start_tls_x509_common;
    args->start_hook_data = &tls_x509_no_host;
    args->result = MIG_TEST_FAIL_DEST_QUIT_ERR;

    args->start.hide_stderr = true;

    test_precopy_tls_x509_common(args, &tls_x509_no_host);
}

#ifdef CONFIG_TASN1
static void test_precopy_tcp_tls_x509_default_host(char *name,
                                                   MigrateCommon *args)
{
    args->listen_uri = "tcp:127.0.0.1:0";
    args->start_hook_full = migrate_hook_start_tls_x509_common;
    args->start_hook_data = &tls_x509_default_host;

    test_precopy_tls_x509_common(args, &tls_x509_default_host);
}

static void test_precopy_tcp_tls_x509_override_host(char *name,
                                                    MigrateCommon *args)
{
    args->listen_uri = "tcp:127.0.0.1:0";
    args->start_hook_full = migrate_hook_start_tls_x509_common;
    args->start_hook_data = &tls_x509_override_host;

    test_precopy_tls_x509_common(args, &tls_x509_override_host);
}

static void test_precopy_tcp_tls_x509_mismatch_host(char *name,
                                                    MigrateCommon *args)
{
    args->listen_uri = "tcp:127.0.0.1:0";
    args->start_hook_full = migrate_hook_start_tls_x509_common;
    args->start_hook_data = &tls_x509_mismatch_host;
    args->result = MIG_TEST_FAIL_DEST_QUIT_ERR;

    args->start.hide_stderr = true;

    test_precopy_tls_x509_common(args, &tls_x509_mismatch_host);
}

static void test_precopy_tcp_tls_x509_friendly_client(char *name,
                                                      MigrateCommon *args)
{
    args->listen_uri = "tcp:127.0.0.1:0";
    args->start_hook_full = migrate_hook_start_tls_x509_common;
    args->start_hook_data = &x509_friendly_client;

    test_precopy_tls_x509_common(args, &x509_friendly_client);
}

static void test_precopy_tcp_tls_x509_hostile_client(char *name,
                                                     MigrateCommon *args)
{
    args->listen_uri = "tcp:127.0.0.1:0";
    args->start_hook_full = migrate_hook_start_tls_x509_common;
    args->start_hook_data = &tls_x509_hostile_client;
    args->result = MIG_TEST_FAIL;

    args->start.hide_stderr = true;

    test_precopy_tls_x509_common(args, &tls_x509_hostile_client);
}

static void test_precopy_tcp_tls_x509_allow_anon_client(char *name,
                                                        MigrateCommon *args)
{
    args->listen_uri = "tcp:127.0.0.1:0";
    args->start_hook_full = migrate_hook_start_tls_x509_common;
    args->start_hook_data = &tls_x509_allow_anon_client;

    test_precopy_tls_x509_common(args, &tls_x509_allow_anon_client);
}

static void test_precopy_tcp_tls_x509_reject_anon_client(char *name,
                                                         MigrateCommon *args)
{
    args->listen_uri = "tcp:127.0.0.1:0";
    args->start_hook_full = migrate_hook_start_tls_x509_common;
    args->start_hook_data = &tls_x509_reject_anon_client;
    args->result = MIG_TEST_FAIL;

    args->start.hide_stderr = true;

    test_precopy_tls_x509_common(args, &tls_x509_reject_anon_client);
}
#endif /* CONFIG_TASN1 */

static void test_multifd_tcp_tls_psk_match(char *name, MigrateCommon *args)
{
    args->start.incoming_defer = true;

    qdict_put_str(args->start.config, "multifd-compression", "none");
    qdict_put_bool(args->start.config, "multifd", true);

    test_precopy_tcp_tls_psk_match(name, args);
}

static void test_multifd_tcp_tls_psk_mismatch(char *name, MigrateCommon *args)
{
    args->start.incoming_defer = true;

    qdict_put_str(args->start.config, "multifd-compression", "none");
    qdict_put_bool(args->start.config, "multifd", true);

    test_precopy_tcp_tls_psk_mismatch(name, args);
}

static void test_multifd_postcopy_tcp_tls_psk_match(char *name,
                                                    MigrateCommon *args)
{
    args->start.incoming_defer = true;

    qdict_put_str(args->start.config, "multifd-compression", "none");
    qdict_put_bool(args->start.config, "multifd", true);
    qdict_put_bool(args->start.config, "postcopy-ram", true);

    test_precopy_tcp_tls_psk_match(name, args);
}

#ifdef CONFIG_TASN1
static void test_multifd_tcp_tls_x509_default_host(char *name,
                                                   MigrateCommon *args)
{
    args->start.incoming_defer = true;
    qdict_put_bool(args->start.config, "multifd", true);
    test_precopy_tcp_tls_x509_default_host(name, args);
}

static void test_multifd_tcp_tls_x509_override_host(char *name,
                                                    MigrateCommon *args)
{
    args->start.incoming_defer = true;
    qdict_put_bool(args->start.config, "multifd", true);
    test_precopy_tcp_tls_x509_override_host(name, args);
}

static void test_multifd_tcp_tls_x509_mismatch_host(char *name,
                                                    MigrateCommon *args)
{
    /*
     * This has different behaviour to the non-multifd case.
     *
     * In non-multifd case when client aborts due to mismatched
     * cert host, the server has already started trying to load
     * migration state, and so it exits with I/O failure.
     *
     * In multifd case when client aborts due to mismatched
     * cert host, the server is still waiting for the other
     * multifd connections to arrive so hasn't started trying
     * to load migration state, and thus just aborts the migration
     * without exiting.
     */
    args->start_hook_full = migrate_hook_start_tls_x509_common;
    args->start_hook_data = &tls_x509_mismatch_host;
    args->result = MIG_TEST_FAIL;
    args->listen_uri = "tcp:127.0.0.1:0";

    args->start.incoming_defer = true;
    args->start.hide_stderr = true;

    qdict_put_bool(args->start.config, "multifd", true);

    test_precopy_tls_x509_common(args, &tls_x509_mismatch_host);
}

static void test_multifd_tcp_tls_x509_allow_anon_client(char *name,
                                                        MigrateCommon *args)
{
    args->start.incoming_defer = true;
    qdict_put_bool(args->start.config, "multifd", true);
    test_precopy_tcp_tls_x509_allow_anon_client(name, args);
}

static void test_multifd_tcp_tls_x509_reject_anon_client(char *name,
                                                         MigrateCommon *args)
{
    args->start.incoming_defer = true;
    qdict_put_bool(args->start.config, "multifd", true);
    test_precopy_tcp_tls_x509_reject_anon_client(name, args);
}
#endif /* CONFIG_TASN1 */

static void migration_test_add_tls_smoke(MigrationTestEnv *env)
{
    migration_test_add("/migration/precopy/tcp/tls/psk/match",
                       test_precopy_tcp_tls_psk_match);
}

void migration_test_add_tls(MigrationTestEnv *env)
{
    tmpfs = env->tmpfs;

    migration_test_add_tls_smoke(env);

    if (!env->full_set) {
        return;
    }

    migration_test_add("/migration/precopy/tcp/no-tls",
                       test_precopy_tcp_no_tls);
    migration_test_add("/migration/precopy/tcp/tls/no-hostname",
                       test_precopy_tcp_tls_no_hostname);

    migration_test_add("/migration/precopy/unix/tls/psk",
                       test_precopy_unix_tls_psk);

    if (env->has_uffd) {
        /*
         * NOTE: psk test is enough for postcopy, as other types of TLS
         * channels are tested under precopy.  Here what we want to test is the
         * general postcopy path that has TLS channel enabled.
         */
        migration_test_add("/migration/postcopy/tls/psk",
                           test_postcopy_tls_psk);
        migration_test_add("/migration/postcopy/recovery/tls/psk",
                           test_postcopy_recovery_tls_psk);
        migration_test_add("/migration/postcopy/preempt/tls/psk",
                           test_postcopy_preempt_tls_psk);
        migration_test_add("/migration/postcopy/preempt/recovery/tls/psk",
                           test_postcopy_preempt_all);
        migration_test_add("/migration/multifd+postcopy/recovery/tls/psk",
                           test_multifd_postcopy_recovery_tls_psk);
        migration_test_add(
            "/migration/multifd+postcopy/preempt/recovery/tls/psk",
            test_multifd_postcopy_preempt_recovery_tls_psk);
    }
#ifdef CONFIG_TASN1
    migration_test_add("/migration/precopy/unix/tls/x509/default-host",
                       test_precopy_unix_tls_x509_default_host);
    migration_test_add("/migration/precopy/unix/tls/x509/override-host",
                       test_precopy_unix_tls_x509_override_host);
#endif /* CONFIG_TASN1 */

    migration_test_add("/migration/precopy/tcp/tls/psk/mismatch",
                       test_precopy_tcp_tls_psk_mismatch);
#ifdef CONFIG_TASN1
    migration_test_add("/migration/precopy/tcp/tls/x509/default-host",
                       test_precopy_tcp_tls_x509_default_host);
    migration_test_add("/migration/precopy/tcp/tls/x509/override-host",
                       test_precopy_tcp_tls_x509_override_host);
    migration_test_add("/migration/precopy/tcp/tls/x509/mismatch-host",
                       test_precopy_tcp_tls_x509_mismatch_host);
    migration_test_add("/migration/precopy/tcp/tls/x509/friendly-client",
                       test_precopy_tcp_tls_x509_friendly_client);
    migration_test_add("/migration/precopy/tcp/tls/x509/hostile-client",
                       test_precopy_tcp_tls_x509_hostile_client);
    migration_test_add("/migration/precopy/tcp/tls/x509/allow-anon-client",
                       test_precopy_tcp_tls_x509_allow_anon_client);
    migration_test_add("/migration/precopy/tcp/tls/x509/reject-anon-client",
                       test_precopy_tcp_tls_x509_reject_anon_client);
#endif /* CONFIG_TASN1 */

    migration_test_add("/migration/multifd/tcp/tls/psk/match",
                       test_multifd_tcp_tls_psk_match);
    migration_test_add("/migration/multifd/tcp/tls/psk/mismatch",
                       test_multifd_tcp_tls_psk_mismatch);
    if (env->has_uffd) {
        migration_test_add("/migration/multifd+postcopy/tcp/tls/psk/match",
                           test_multifd_postcopy_tcp_tls_psk_match);
    }
#ifdef CONFIG_TASN1
    migration_test_add("/migration/multifd/tcp/tls/x509/default-host",
                       test_multifd_tcp_tls_x509_default_host);
    migration_test_add("/migration/multifd/tcp/tls/x509/override-host",
                       test_multifd_tcp_tls_x509_override_host);
    migration_test_add("/migration/multifd/tcp/tls/x509/mismatch-host",
                       test_multifd_tcp_tls_x509_mismatch_host);
    migration_test_add("/migration/multifd/tcp/tls/x509/allow-anon-client",
                       test_multifd_tcp_tls_x509_allow_anon_client);
    migration_test_add("/migration/multifd/tcp/tls/x509/reject-anon-client",
                       test_multifd_tcp_tls_x509_reject_anon_client);
#endif /* CONFIG_TASN1 */
}
