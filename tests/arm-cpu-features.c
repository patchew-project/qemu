/*
 * Arm CPU feature test cases
 *
 * Copyright (c) 2019 Red Hat Inc.
 * Authors:
 *  Andrew Jones <drjones@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "libqtest.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qjson.h"

#define MACHINE    "-machine virt,gic-version=max "
#define QUERY_HEAD "{ 'execute': 'query-cpu-model-expansion', " \
                     "'arguments': { 'type': 'full', "
#define QUERY_TAIL "}}"

static QDict *do_query_no_props(QTestState *qts, const char *cpu_type)
{
    return qtest_qmp(qts, QUERY_HEAD "'model': { 'name': %s }"
                          QUERY_TAIL, cpu_type);
}

static const char *resp_get_error(QDict *resp)
{
    QDict *qdict;

    g_assert(resp);
    qdict = qdict_get_qdict(resp, "error");
    if (qdict) {
        return qdict_get_str(qdict, "desc");
    }
    return NULL;
}

static char *get_error(QTestState *qts, const char *cpu_type,
                       const char *fmt, ...)
{
    QDict *resp;
    char *error;

    if (fmt) {
        QDict *args;
        va_list ap;

        va_start(ap, fmt);
        args = qdict_from_vjsonf_nofail(fmt, ap);
        va_end(ap);

        resp = qtest_qmp(qts, QUERY_HEAD "'model': { 'name': %s, "
                                                    "'props': %p }"
                              QUERY_TAIL, cpu_type, args);
    } else {
        resp = do_query_no_props(qts, cpu_type);
    }

    g_assert(resp);
    error = g_strdup(resp_get_error(resp));
    qobject_unref(resp);

    return error;
}

#define assert_error(qts, cpu_type, expected_error, fmt, ...)          \
({                                                                     \
    char *_error = get_error(qts, cpu_type, fmt, ##__VA_ARGS__);       \
    g_assert(_error);                                                  \
    g_assert(g_str_equal(_error, expected_error));                     \
    g_free(_error);                                                    \
})

static QDict *resp_get_props(QDict *resp)
{
    QDict *qdict;

    g_assert(resp);
    g_assert(qdict_haskey(resp, "return"));
    qdict = qdict_get_qdict(resp, "return");
    g_assert(qdict_haskey(qdict, "model"));
    qdict = qdict_get_qdict(qdict, "model");
    g_assert(qdict_haskey(qdict, "props"));
    qdict = qdict_get_qdict(qdict, "props");
    return qdict;
}

#define assert_has_feature(qts, cpu_type, feature)                     \
({                                                                     \
    QDict *_resp = do_query_no_props(qts, cpu_type);                   \
    g_assert(_resp);                                                   \
    g_assert(qdict_get(resp_get_props(_resp), feature));               \
    qobject_unref(_resp);                                              \
})

#define assert_has_not_feature(qts, cpu_type, feature)                 \
({                                                                     \
    QDict *_resp = do_query_no_props(qts, cpu_type);                   \
    g_assert(_resp);                                                   \
    g_assert(!qdict_get(resp_get_props(_resp), feature));              \
    qobject_unref(_resp);                                              \
})

static void assert_type_full(QTestState *qts, const char *cpu_type)
{
    const char *error;
    QDict *resp;

    resp = qtest_qmp(qts, "{ 'execute': 'query-cpu-model-expansion', "
                            "'arguments': { 'type': 'static', "
                                           "'model': { 'name': %s }}}",
                     cpu_type);
    g_assert(resp);
    error = resp_get_error(resp);
    g_assert(error);
    g_assert(g_str_equal(error,
                         "The requested expansion type is not supported."));
    qobject_unref(resp);
}

static void assert_bad_props(QTestState *qts, const char *cpu_type)
{
    const char *error;
    QDict *resp;

    resp = qtest_qmp(qts, "{ 'execute': 'query-cpu-model-expansion', "
                            "'arguments': { 'type': 'full', "
                                           "'model': { 'name': %s, "
                                                      "'props': false }}}",
                     cpu_type);
    g_assert(resp);
    error = resp_get_error(resp);
    g_assert(error);
    g_assert(g_str_equal(error,
                         "Invalid parameter type for 'props', expected: dict"));
    qobject_unref(resp);
}

static void test_query_cpu_model_expansion(const void *data)
{
    QTestState *qts;

    qts = qtest_init(MACHINE "-cpu max");

    /* Test common query-cpu-model-expansion input validation */
    assert_type_full(qts, "foo");
    assert_bad_props(qts, "max");
    assert_error(qts, "foo", "The CPU definition 'foo' is unknown.", NULL);
    assert_error(qts, "max", "Parameter 'not-a-prop' is unexpected",
                 "{ 'not-a-prop': false }");
    assert_error(qts, "host", "The CPU definition 'host' requires KVM", NULL);

    /* Test expected feature presence/absence for some cpu types */
    assert_has_feature(qts, "max", "pmu");
    assert_has_feature(qts, "cortex-a15", "pmu");
    assert_has_not_feature(qts, "cortex-a15", "aarch64");

    if (g_str_equal(qtest_get_arch(), "aarch64")) {
        assert_has_feature(qts, "max", "aarch64");
        assert_has_feature(qts, "cortex-a57", "pmu");
        assert_has_feature(qts, "cortex-a57", "aarch64");

        /* Test that features that depend on KVM generate errors without. */
        assert_error(qts, "max",
                     "'aarch64' feature cannot be disabled "
                     "unless KVM is enabled and 32-bit EL1 "
                     "is supported",
                     "{ 'aarch64': false }");
    }

    qtest_quit(qts);
}

static void test_query_cpu_model_expansion_kvm(const void *data)
{
    QTestState *qts;

    qts = qtest_init(MACHINE "-accel kvm -cpu host");

    assert_has_feature(qts, "host", "pmu");

    if (g_str_equal(qtest_get_arch(), "aarch64")) {
        assert_has_feature(qts, "host", "aarch64");

        assert_error(qts, "cortex-a15",
            "The CPU definition 'cortex-a15' cannot "
            "be used with KVM on this host", NULL);
    } else {
        assert_error(qts, "host",
                     "'pmu' feature not supported by KVM on this host",
                     "{ 'pmu': true }");
    }

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    bool kvm_available = false;

    if (!access("/dev/kvm",  R_OK | W_OK)) {
#if defined(HOST_AARCH64)
        kvm_available = g_str_equal(qtest_get_arch(), "aarch64");
#elif defined(HOST_ARM)
        kvm_available = g_str_equal(qtest_get_arch(), "arm");
#endif
    }

    g_test_init(&argc, &argv, NULL);

    qtest_add_data_func("/arm/query-cpu-model-expansion",
                        NULL, test_query_cpu_model_expansion);

    if (kvm_available) {
        qtest_add_data_func("/arm/kvm/query-cpu-model-expansion",
                            NULL, test_query_cpu_model_expansion_kvm);
    }

    return g_test_run();
}
