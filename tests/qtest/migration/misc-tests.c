/*
 * QTest testcases for migration
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
#include "qapi/error.h"
#include "qobject/qjson.h"
#include "libqtest.h"
#include "migration/framework.h"
#include "migration/migration-qmp.h"
#include "migration/migration-util.h"

#define ANALYZE_SCRIPT "scripts/analyze-migration.py"

static char *tmpfs;

#ifdef CONFIG_HMP
static int test_case_line;

#define TEST(i1, i2, e) { i1, i2, e , .line = __LINE__, }
#define SKIP(i1, i2, e) { i1, i2, e , .skip = true, }
#define BG_SNAP_MSG ("Error: Background-snapshot is not compatible with " \
                     "currently set capabilities")

typedef struct HMPTestData {
    const char *input1;
    const char *input2;
    const char *output1;
    bool skip;
    int line;
} HMPTestData;

/*
 * .input1: string to be used as parameter name
 * .input2: string to be used as parameter value
 * .output1: expected output of migrate_set_parameters
 * E.g:
 * (qemu) migrate_set_parameters .input1 .input2
 * .output1
 */
HMPTestData test_cases[] = {
    TEST("", "", "migrate_set_parameter: string expected"),
    TEST("foo", "", "migrate_set_parameter: string expected"),
    TEST("foo", "on", "Error: invalid parameter value: foo"),

    /* bool */
    TEST("cpu-throttle-tailslow", "on", "on"),
    TEST("direct-io", "on", "on"),

    /* uint64_t */
    TEST("announce-initial", "60", "60 ms"),
    TEST("announce-max", "600", "600 ms"),
    TEST("announce-rounds", "6", "6"),
    TEST("announce-step", "15", "15 ms"),
    TEST("downtime-limit", "400", "400 ms"),
    TEST("avail-switchover-bandwidth", "2097152", "2199023255552 bytes/second"),
    TEST("max-bandwidth", "9876543", "10356305952768 bytes/second"),
    TEST("max-postcopy-bandwidth", "1048576", "1048576 bytes/second"),
    TEST("vcpu-dirty-limit", "20", "20 MB/s"),
    TEST("x-rdma-chunk-size", "1048576", "1048576 bytes"),
    TEST("x-vcpu-dirty-limit-period", "750", "750 ms"),
    TEST("xbzrle-cache-size", "67108864", "67108864 bytes"),

    /* uint32_t */
    TEST("x-checkpoint-delay", "5000", "5000 ms"),

    /* uint8_t */
    TEST("cpu-throttle-increment", "15", "15"),
    TEST("cpu-throttle-initial", "25", "25"),
    TEST("max-cpu-throttle", "85", "85"),
    TEST("multifd-channels", "8", "8"),
    TEST("throttle-trigger-threshold", "65", "65"),

    /* complex types */
    TEST("mode", "cpr-exec", "cpr-exec"),
    TEST("multifd-compression", "zlib", "zlib"),
    TEST("zero-page-detection", "none", "none"),
    TEST("tls-authz", "my_authz", "'my_authz'"),
    TEST("tls-creds", "null", "'null'"),
    TEST("tls-hostname", "localhost", "'localhost'"),
    TEST("cpr-exec-command", "/bin/true foobar", "/bin/true foobar"),

    /* can be set but are currently missing in the query output */
    SKIP("multifd-qatzip-level", "5", "5"),
    SKIP("multifd-zlib-level", "4", "4"),
    SKIP("multifd-zstd-level", "6", "6"),

    /* cannot be set */
    TEST("block-bitmap-mapping", "[]",
         "Error: The block-bitmap-mapping parameter "
         "can only be set through QMP"),
};

/*
 * Find a contiguous run of tokens in @larger that match the sequence
 * of tokens in @smaller, ignoring mismatches due to sequences of
 * empty tokens.
 *
 * Returns whether a match was found. @last is set if at least one
 * token has matched.
 */
static bool token_list_is_substr(char **smaller, char **larger, int *last)
{
    int i, j, k = 0;
    bool match = false;

    for (i = 0; smaller[i]; i++) {
        for (j = k; larger[j]; j++) {
            if (!*larger[j]) {
                continue;
            }

            /* readline adds several escape sequences */
            if (*larger[j] == '\033') {
                continue;
            }

            if (g_str_equal(larger[j], smaller[i])) {
                match = true;
                *last = j;
                k = j + 1;
                break;
            }

            if (match) {
                return false;
            } else {
                match = false;
            }
        }
    }

    return match;
}

static void assert_hmp_match_line(const char *str, const char *text)
{
    g_auto(GStrv) tok_str = g_strsplit_set(str, " ", -1);
    g_auto(GStrv) lines = g_strsplit_set(text, " \r\n", -1);
    int i, idx = -1;

    /*
     * Note that the reason the 'str' above is split is to allow
     * token_list_is_substr() to first match on the parameter name so
     * matching can stop immediately after a mismatched value is
     * found. This provides a better output for failing test cases
     * than simply "str != line".
     */

    for (i = 0; lines[i]; i++) {
        if (token_list_is_substr(tok_str, (char **)&lines[i], &idx)) {
            return;
        }

        if (idx >= 0) {
            break;
        }
    }

    g_test_message("HMP output mismatch for entry at line %d:", test_case_line);
    g_test_message("expected vs. found:\n\n%s\n---\n%s %s", str, lines[idx],
                   lines[idx + 1]);
    g_assert_not_reached();
}

static void assert_hmp_success(const char *str)
{
    if (!g_str_equal(str, "")) {
        g_test_message("HMP command failed:\n\n%s", str);
        g_assert_not_reached();
    }
}

static void test_hmp_migration_parameters(char *name, MigrateCommon *args)
{
    QTestState *qts;

    /* force TCG so it can run in all targets */
    qts = qtest_init("-accel tcg -nodefaults -S");

    for (int i = 0; i < G_N_ELEMENTS(test_cases); i++) {
        g_autofree char *resp = NULL;
        g_autofree char *line = NULL;
        struct HMPTestData *t = &test_cases[i];

        if (t->skip) {
            continue;
        }

        test_case_line = t->line;

        resp = qtest_hmp(qts, "migrate_set_parameter %s %s", t->input1,
                         t->input2);

        if (g_str_has_prefix(t->output1, "Error:") ||
            g_str_has_prefix(resp, "migrate_set_parameter:")) {

            assert_hmp_match_line(t->output1, resp);
            continue;
        }
        assert_hmp_success(resp);
        g_free(resp);

        resp = qtest_hmp(qts, "info migrate_parameters");

        line = g_strconcat(t->input1, ": ", t->output1, NULL);
        assert_hmp_match_line(line, resp);
    }

    qtest_quit(qts);
}
#endif /* CONFIG_HMP */

static void test_baddest(char *name, MigrateCommon *args)
{
    QTestState *from, *to;

    args->start.hide_stderr = true;

    if (migrate_start(&from, &to, &args->start)) {
        return;
    }

    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");
    migrate_qmp(from, to, "tcp:127.0.0.1:0", NULL, "{}");
    wait_for_migration_fail(from, false);
    migrate_end(from, to, false);
}

#ifndef _WIN32
static void test_analyze_script(char *name, MigrateCommon *args)
{
    QTestState *from, *to;
    g_autofree char *uri = NULL;
    g_autofree char *file = NULL;
    int pid, wstatus;
    const char *python = g_getenv("PYTHON");

    args->start.opts_source = "-uuid 11111111-1111-1111-1111-111111111111";

    if (!python) {
        g_test_skip("PYTHON variable not set");
        return;
    }

    if (migrate_start(&from, &to, &args->start)) {
        return;
    }

    /*
     * Setting these two capabilities causes the "configuration"
     * vmstate to include subsections for them. The script needs to
     * parse those subsections properly.
     */
    migrate_set_capability(from, "validate-uuid", true);
    migrate_set_capability(from, "x-ignore-shared", true);

    file = g_strdup_printf("%s/migfile", tmpfs);
    uri = g_strdup_printf("exec:cat > %s", file);

    migrate_ensure_converge(from);
    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");
    migrate_qmp(from, to, uri, NULL, "{}");
    wait_for_migration_complete(from);

    pid = fork();
    if (!pid) {
        close(1);
        open("/dev/null", O_WRONLY);
        execl(python, python, ANALYZE_SCRIPT, "-f", file, NULL);
        g_assert_not_reached();
    }

    g_assert(waitpid(pid, &wstatus, 0) == pid);
    if (!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0) {
        g_test_message("Failed to analyze the migration stream");
        g_test_fail();
    }
    migrate_end(from, to, false);
    unlink(file);
}
#endif

static void ignore_shared_assert_skipped(QTestState *from, QTestState *to,
                                         void *data)
{
    /* Check whether shared RAM has been really skipped */
    g_assert_cmpint(
        read_ram_property_int(from, "transferred"), <, 4 * 1024 * 1024);
}

static void test_ignore_shared(char *name, MigrateCommon *args)
{
    args->live = true;
    args->start.mem_type = MEM_TYPE_SHMEM;
    args->start.caps[MIGRATION_CAPABILITY_X_IGNORE_SHARED] = true;
    args->end_hook = ignore_shared_assert_skipped;

    test_precopy_unix_common(args);
}

static void do_test_validate_uuid(MigrateStart *args, bool should_fail)
{
    g_autofree char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);
    QTestState *from, *to;

    if (migrate_start(&from, &to, args)) {
        return;
    }

    /*
     * UUID validation is at the begin of migration. So, the main process of
     * migration is not interesting for us here. Thus, set huge downtime for
     * very fast migration.
     */
    migrate_set_parameter_int(from, "downtime-limit", 1000000);
    migrate_set_capability(from, "validate-uuid", true);

    /* Wait for the first serial output from the source */
    wait_for_serial("src_serial");

    migrate_incoming_qmp(to, uri, NULL, "{}");
    migrate_qmp(from, to, uri, NULL, "{}");

    if (should_fail) {
        wait_for_migration_fail(from, true);
    } else {
        wait_for_migration_complete(from);
    }

    migrate_end(from, to, false);
}

static void test_validate_uuid(char *name, MigrateCommon *args)
{
    args->start.opts_source = "-uuid 11111111-1111-1111-1111-111111111111";
    args->start.opts_target = "-uuid 11111111-1111-1111-1111-111111111111";

    do_test_validate_uuid(&args->start, false);
}

static void test_validate_uuid_error(char *name, MigrateCommon *args)
{
    args->start.opts_source = "-uuid 11111111-1111-1111-1111-111111111111";
    args->start.opts_target = "-uuid 22222222-2222-2222-2222-222222222222";
    args->start.hide_stderr = true;

    do_test_validate_uuid(&args->start, true);
}

static void test_validate_uuid_src_not_set(char *name, MigrateCommon *args)
{
    args->start.opts_target = "-uuid 22222222-2222-2222-2222-222222222222";
    args->start.hide_stderr = true;

    do_test_validate_uuid(&args->start, false);
}

static void test_validate_uuid_dst_not_set(char *name, MigrateCommon *args)
{
    args->start.opts_source = "-uuid 11111111-1111-1111-1111-111111111111";
    args->start.hide_stderr = true;

    do_test_validate_uuid(&args->start, false);
}

static void do_test_validate_uri_channel(MigrateCommon *args)
{
    QTestState *from, *to;
    QObject *channels;

    if (migrate_start(&from, &to, &args->start)) {
        return;
    }

    /* Wait for the first serial output from the source */
    wait_for_serial("src_serial");

    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");

    /*
     * 'uri' and 'channels' validation is checked even before the migration
     * starts.
     */
    channels = args->connect_channels ?
               qobject_from_json(args->connect_channels, &error_abort) :
               NULL;
    migrate_qmp_fail(from, args->uri, channels, "{}");

    migrate_end(from, to, false);
}

static void test_validate_uri_channels_both_set(char *name, MigrateCommon *args)
{
    args->uri = "tcp:127.0.0.1:0",
    args->connect_channels = ("[ { ""'channel-type': 'main',"
                              "    'addr': { 'transport': 'socket',"
                              "              'type': 'inet',"
                              "              'host': '127.0.0.1',"
                              "              'port': '0' } } ]"),

    args->start.hide_stderr = true;

    do_test_validate_uri_channel(args);
}

static void test_validate_uri_channels_none_set(char *name, MigrateCommon *args)
{
    args->start.hide_stderr = true;

    do_test_validate_uri_channel(args);
}

static void migration_test_add_misc_smoke(MigrationTestEnv *env)
{
#ifndef _WIN32
    migration_test_add("/migration/analyze-script", test_analyze_script);
#endif
}

void migration_test_add_misc(MigrationTestEnv *env)
{
    tmpfs = env->tmpfs;

    migration_test_add_misc_smoke(env);

    if (!env->full_set) {
        return;
    }

    migration_test_add("/migration/bad_dest", test_baddest);

    /*
     * Our CI system has problems with shared memory.
     * Don't run this test until we find a workaround.
     */
    if (getenv("QEMU_TEST_FLAKY_TESTS")) {
        migration_test_add("/migration/ignore-shared", test_ignore_shared);
    }

    migration_test_add("/migration/validate_uuid", test_validate_uuid);
    migration_test_add("/migration/validate_uuid_error",
                       test_validate_uuid_error);
    migration_test_add("/migration/validate_uuid_src_not_set",
                       test_validate_uuid_src_not_set);
    migration_test_add("/migration/validate_uuid_dst_not_set",
                       test_validate_uuid_dst_not_set);
    migration_test_add("/migration/validate_uri/channels/both_set",
                       test_validate_uri_channels_both_set);
    migration_test_add("/migration/validate_uri/channels/none_set",
                       test_validate_uri_channels_none_set);
#ifdef CONFIG_HMP
    migration_test_add("/migration/hmp/parameters",
                       test_hmp_migration_parameters);
#endif
}
