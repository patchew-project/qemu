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
#include "qemu/sockets.h"
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
    TEST("foo", "on", "Error: Parameter 'foo' is unexpected"),

    /* bool */
    TEST("cpu-throttle-tailslow", "on", "on"),
    TEST("direct-io", "on", "on"),

    /* uint64_t */
    TEST("announce-initial", "60", "60"),
    TEST("announce-max", "600", "600"),
    TEST("announce-rounds", "6", "6"),
    TEST("announce-step", "15", "15"),
    TEST("downtime-limit", "400", "400"),
    TEST("avail-switchover-bandwidth", "2097152", "2097152"),
    TEST("max-bandwidth", "9876543", "9876543"),
    TEST("max-postcopy-bandwidth", "1048576", "1048576"),
    TEST("vcpu-dirty-limit", "20", "20"),
    TEST("x-rdma-chunk-size", "1048576", "1048576"),
    TEST("x-vcpu-dirty-limit-period", "750", "750"),
    TEST("xbzrle-cache-size", "67108864", "67108864"),

    /* uint32_t */
    TEST("x-checkpoint-delay", "5000", "5000"),

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
    TEST("tls-authz", "my_authz", "my_authz"),
    TEST("tls-creds", "null", "null"),
    TEST("tls-hostname", "localhost", "localhost"),
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
 * .input1:  partial string with an ending TAB (as if pressed by
 *           the user).
 * .input2:  common root of the completions, i.e. what the partial
 *           part of .input1 string completes to.
 * .output1: full list of completion suggestions for the string
 *           in .input2.
 * E.g:
 * (qemu) .input1
 * <after TAB>
 * (qemu) .input2
 * .output1
 */
HMPTestData completion_cases[] = {
    TEST("migra\t",
         "migrate",
         "migrate migrate_cancel migrate_continue migrate_incoming "
         "migrate_pause migrate_recover migrate_set_capability "
         "migrate_set_parameter migrate_start_postcopy"),

    /*
     * Note QEMU doesn't keep 'info' when offering the completions
     * suggestions.
     */
    TEST("info migra\t",
         "migrate",
         "migrate migrate_capabilities migrate_parameters"),

    TEST("migrate_se\t",
         "migrate_set_",
         "migrate_set_capability migrate_set_parameter"),

    /*
     * parameters and capabilities are not listed here to avoid having
     * to enumerate them all, see test_hmp_completion().
     */
    TEST("migrate_set_parameter \t", "migrate_set_parameter ", "@params@"),
    TEST("migrate_set_capability \t", "migrate_set_capability ", "@caps@"),
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

static void assert_hmp_match_text(const char *str, const char *text)
{
    g_auto(GStrv) tok_str = g_strsplit_set(str, " ", -1);
    g_auto(GStrv) tok_txt = g_strsplit_set(text, " \r\n", -1);
    int idx;

    if (token_list_is_substr(tok_str, tok_txt, &idx)) {
        return;
    }

    g_test_message("HMP output mismatch for entry at line %d:", test_case_line);
    g_test_message("expected vs. found (whitespace ignored):\n\n%s\n---\n%s",
                   str, text);
    g_assert_not_reached();
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

static void hmp_sock_write(int fd, const char *buf)
{
    size_t sz = strlen(buf);

    assert(fd > 0);
    assert(write(fd, buf, sz) == sz);
}

static void hmp_sock_read(int fd, char *buf, size_t buf_sz)
{
    char *p = buf;
    size_t sz = buf_sz - 1;

    assert(fd >= 0);
    memset(buf, 0, buf_sz);

    while (sz > 0) {
        ssize_t r = read(fd, p, sz);
        char *prompt;

        if (!r) {
            break;
        } else if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            g_assert_not_reached();
        }

        p += r;
        sz -= r;

        prompt = strstr(buf, "(qemu) ");
        if (prompt) {
            *prompt = '\0';
            break;
        }
    }
}

static int comp(const void *a, const void *b)
{
    return strcmp(*(const char **) a, *(const char **) b);
}

static void get_migration_opts_sorted(GString *exp, const char * const *lookup, int n)
{
    g_autofree char **opts_array = g_new0(char *, n);
    uint8_t i;

    for (i = 0; i < n; i++) {
        opts_array[i] = g_strdup(lookup[i]);
    }

    qsort(opts_array, n, sizeof(char *), comp);

    for (i = 0; i < n; i++) {
        g_string_append(exp, opts_array[i]);
        if (i + 1 != n) {
            g_string_append(exp, " ");
        }
    }

    for (i = 0; i < n; i++) {
        g_free(opts_array[i]);
    }
}

static void hmp_completion_single(int fd, const struct HMPTestData *t)
{
    g_autoptr(GString) exp = g_string_new("");
    char buf[8192];
    char *output;

    test_case_line = t->line;

    if (g_str_equal(t->output1, "@caps@")) {
        g_string_append(exp, "migrate_set_capability ");
        get_migration_opts_sorted(exp, MigrationCapability_lookup.array,
                                  MIGRATION_CAPABILITY__MAX);
    } else if (g_str_equal(t->output1, "@params@")) {
        g_string_append(exp, "migrate_set_parameter ");
        get_migration_opts_sorted(exp, MigrationParameter_lookup.array,
                                  MIGRATION_PARAMETER__MAX);
    } else {
        g_string_append(exp, t->output1);
    }

    hmp_sock_write(fd, t->input1);
    hmp_sock_read(fd, buf, sizeof(buf));

    /*
     * readline first rewrites the input to the common root of the
     * completions, then outputs the completion suggestions:
     *
     * (qemu) info migr<TAB>
     * (qemu) migrate migrate_parameters
     * migrate_capabilities ...
     */
    output = strstr(buf, t->input2);
    assert_hmp_match_text(exp->str, output);

    /* ^U backward kill line */
    hmp_sock_write(fd, "\x15");
}

static void test_hmp_completion(char *name, MigrateCommon *args)
{
    g_autofree char *cmdline;
    char buf[1024];
    QTestState *qts;
    int sockfds[2];

    assert(!qemu_socketpair(AF_UNIX, SOCK_STREAM, 0, sockfds));
    qemu_clear_cloexec(sockfds[1]);

    cmdline = g_strdup_printf("-chardev socket,id=mon0,fd=%d "
                              "-mon chardev=mon0,mode=readline -S",
                              sockfds[1]);
    qts = qtest_init(cmdline);
    close(sockfds[1]);

    /* read HMP banner */
    hmp_sock_read(sockfds[0], buf, sizeof(buf));

    for (int i = 0; i < G_N_ELEMENTS(completion_cases); i++) {
        hmp_completion_single(sockfds[0], &completion_cases[i]);
    }

    close(sockfds[0]);
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
    migration_test_add("/migration/hmp/completion",
                       test_hmp_completion);
#endif
}
