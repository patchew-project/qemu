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

#if __SIZEOF_LONG__ == 8
#define BIT(n) (1UL << (n))
#else
#define BIT(n) (1ULL << (n))
#endif

/*
 * We expect the SVE max-vq to be 16. Also it must be <= 64
 * for our test code, otherwise 'vls' can't just be a uint64_t.
 */
#define SVE_MAX_VQ 16

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

static void resp_get_sve_vls(QDict *resp, uint64_t *vls, uint32_t *max_vq)
{
    const QDictEntry *e;
    QDict *qdict;
    int n = 0;

    *vls = 0;

    qdict = resp_get_props(resp);

    for (e = qdict_first(qdict); e; e = qdict_next(qdict, e)) {
        if (strlen(e->key) > 3 && !strncmp(e->key, "sve", 3) &&
            g_ascii_isdigit(e->key[3])) {
            char *endptr;
            int bits;

            bits = g_ascii_strtoll(&e->key[3], &endptr, 10);
            if (!bits || *endptr != '\0') {
                continue;
            }

            if (qdict_get_bool(qdict, e->key)) {
                *vls |= BIT((bits / 128) - 1);
            }
            ++n;
        }
    }

    g_assert(n == SVE_MAX_VQ);

    *max_vq = !*vls ? 0 : 64 - __builtin_clzll(*vls);
}

static uint64_t sve_get_vls(QTestState *qts, const char *cpu_type,
                            const char *fmt, ...)
{
    QDict *resp;
    uint64_t vls;
    uint32_t max_vq;

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
    resp_get_sve_vls(resp, &vls, &max_vq);
    qobject_unref(resp);

    return vls;
}

#define assert_sve_vls(qts, cpu_type, expected_vls, fmt, ...) \
    g_assert(sve_get_vls(qts, cpu_type, fmt, ##__VA_ARGS__) == expected_vls)

static void sve_tests_default(QTestState *qts, const char *cpu_type)
{
    /*
     * With no sve-max-vq or sve<vl-bits> properties on the command line
     * the default is to have all vector lengths enabled.
     */
    assert_sve_vls(qts, cpu_type, BIT(SVE_MAX_VQ) - 1, NULL);

    /*
     * -------------------------------------------------------------------
     *               power-of-2(vq)   all-power-            can      can
     *                                of-2(< vq)          enable   disable
     * -------------------------------------------------------------------
     * vq < max_vq      no            MUST*                yes      yes
     * vq < max_vq      yes           MUST*                yes      no
     * -------------------------------------------------------------------
     * vq == max_vq     n/a           MUST*                yes**    yes**
     * -------------------------------------------------------------------
     * vq > max_vq      n/a           no                   no       yes
     * vq > max_vq      n/a           yes                  yes      yes
     * -------------------------------------------------------------------
     *
     * [*] "MUST" means this requirement must already be satisfied,
     *     otherwise 'max_vq' couldn't itself be enabled.
     *
     * [**] Not testable with the QMP interface, only with the command line.
     */

    /* max_vq := 8 */
    assert_sve_vls(qts, cpu_type, 0x8b, "{ 'sve1024': true }");

    /* max_vq := 8, vq < max_vq, !power-of-2(vq) */
    assert_sve_vls(qts, cpu_type, 0x8f,
                   "{ 'sve1024': true, 'sve384': true }");
    assert_sve_vls(qts, cpu_type, 0x8b,
                   "{ 'sve1024': true, 'sve384': false }");

    /* max_vq := 8, vq < max_vq, power-of-2(vq) */
    assert_sve_vls(qts, cpu_type, 0x8b,
                   "{ 'sve1024': true, 'sve256': true }");
    assert_error(qts, cpu_type, "cannot disable sve256",
                 "{ 'sve1024': true, 'sve256': false }");

    /*
     * max_vq := 3, vq > max_vq, !all-power-of-2(< vq)
     *
     * If given sve384=on,sve512=off,sve640=on the command line error would be
     * "cannot enable sve640", but QMP visits the vector lengths in reverse
     * order, so we get "cannot disable sve512" instead. The command line would
     * also give that error if given sve384=on,sve640=on,sve512=off, so this is
     * all fine. The important thing is that we get an error.
     */
    assert_error(qts, cpu_type, "cannot disable sve512",
                 "{ 'sve384': true, 'sve512': false, 'sve640': true }");

    /*
     * We can disable power-of-2 vector lengths when all larger lengths
     * are also disabled. The shorter, sve384=on,sve512=off,sve640=off
     * works on the command line, but QMP doesn't know that all the
     * vector lengths larger than 384-bits will be disabled until it
     * sees the enabling of sve384, which comes near the end since it
     * visits the lengths in reverse order. So we just have to explicitly
     * disable them all.
     */
    assert_sve_vls(qts, cpu_type, 0x7,
                   "{ 'sve384': true, 'sve512': false, 'sve640': false, "
                   "  'sve768': false, 'sve896': false, 'sve1024': false, "
                   "  'sve1152': false, 'sve1280': false, 'sve1408': false, "
                   "  'sve1536': false, 'sve1664': false, 'sve1792': false, "
                   "  'sve1920': false, 'sve2048': false }");

    /* max_vq := 3, vq > max_vq, all-power-of-2(< vq) */
    assert_sve_vls(qts, cpu_type, 0x1f,
                   "{ 'sve384': true, 'sve512': true, 'sve640': true }");
    assert_sve_vls(qts, cpu_type, 0xf,
                   "{ 'sve384': true, 'sve512': true, 'sve640': false }");
}

static void sve_tests_sve_max_vq_8(const void *data)
{
    QTestState *qts;

    qts = qtest_init(MACHINE "-cpu max,sve-max-vq=8");

    assert_sve_vls(qts, "max", BIT(8) - 1, NULL);

    /*
     * Disabling the max-vq set by sve-max-vq is not allowed, but
     * of course enabling it is OK.
     */
    assert_error(qts, "max", "cannot disable sve1024", "{ 'sve1024': false }");
    assert_sve_vls(qts, "max", 0xff, "{ 'sve1024': true }");

    /*
     * Enabling anything larger than max-vq set by sve-max-vq is not
     * allowed, but of course disabling everything larger is OK.
     */
    assert_error(qts, "max", "cannot enable sve1152", "{ 'sve1152': true }");
    assert_sve_vls(qts, "max", 0xff, "{ 'sve1152': false }");

    /*
     * We can disable non power-of-2 lengths smaller than the max-vq
     * set by sve-max-vq, but not power-of-2 lengths.
     */
    assert_sve_vls(qts, "max", 0xfb, "{ 'sve384': false }");
    assert_error(qts, "max", "cannot disable sve256", "{ 'sve256': false }");

    qtest_quit(qts);
}

static void sve_tests_off(QTestState *qts, const char *cpu_type)
{
    /*
     * SVE is off, so the map should be empty.
     */
    assert_sve_vls(qts, cpu_type, 0, NULL);

    /*
     * We can't turn anything on, but off is OK.
     */
    assert_error(qts, cpu_type, "cannot enable sve128", "{ 'sve128': true }");
    assert_sve_vls(qts, cpu_type, 0, "{ 'sve128': false }");
}

static void sve_tests_sve_off(const void *data)
{
    QTestState *qts;

    qts = qtest_init(MACHINE "-cpu max,sve=off");
    sve_tests_off(qts, "max");
    qtest_quit(qts);
}

static void sve_tests_sve_off_kvm(const void *data)
{
    QTestState *qts;

    qts = qtest_init(MACHINE "-accel kvm -cpu max,sve=off");
    sve_tests_off(qts, "max");
    qtest_quit(qts);
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
        assert_has_feature(qts, "max", "sve");
        assert_has_feature(qts, "max", "sve128");
        assert_has_feature(qts, "cortex-a57", "pmu");
        assert_has_feature(qts, "cortex-a57", "aarch64");

        sve_tests_default(qts, "max");

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
        bool kvm_supports_sve;
        uint32_t max_vq, vq;
        uint64_t vls;
        char name[8];
        QDict *resp;
        char *error;

        assert_has_feature(qts, "host", "aarch64");

        assert_error(qts, "cortex-a15",
            "The CPU definition 'cortex-a15' cannot "
            "be used with KVM on this host", NULL);

        assert_has_feature(qts, "max", "sve");
        resp = do_query_no_props(qts, "max");
        g_assert(resp);
        kvm_supports_sve = qdict_get_bool(resp_get_props(resp), "sve");
        qobject_unref(resp);

        if (kvm_supports_sve) {
            resp = do_query_no_props(qts, "max");
            resp_get_sve_vls(resp, &vls, &max_vq);
            g_assert(max_vq != 0);
            qobject_unref(resp);

            /* Enabling a supported length is of course fine. */
            sprintf(name, "sve%d", max_vq * 128);
            assert_sve_vls(qts, "max", vls, "{ %s: true }", name);

            /* Also disabling the largest lengths is fine. */
            assert_sve_vls(qts, "max", (vls & ~BIT(max_vq - 1)),
                           "{ %s: false }", name);

            for (vq = 1; vq <= max_vq; ++vq) {
                if (!(vls & BIT(vq - 1))) {
                    /* vq is unsupported */
                    break;
                }
            }
            if (vq <= SVE_MAX_VQ) {
                sprintf(name, "sve%d", vq * 128);
                error = g_strdup_printf("cannot enable %s", name);
                assert_error(qts, "max", error, "{ %s: true }", name);
                g_free(error);
            }

            if (max_vq > 1) {
                /* The next smaller, supported vq is required */
                vq = 64 - __builtin_clzll(vls & ~BIT(max_vq - 1));
                sprintf(name, "sve%d", vq * 128);
                error = g_strdup_printf("cannot disable %s", name);
                assert_error(qts, "max", error, "{ %s: false }", name);
                g_free(error);
            }
        } else {
            resp = do_query_no_props(qts, "max");
            resp_get_sve_vls(resp, &vls, &max_vq);
            g_assert(max_vq == 0);
            qobject_unref(resp);
        }
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

    if (g_str_equal(qtest_get_arch(), "aarch64")) {
        qtest_add_data_func("/arm/max/query-cpu-model-expansion/sve-max-vq-8",
                            NULL, sve_tests_sve_max_vq_8);
        qtest_add_data_func("/arm/max/query-cpu-model-expansion/sve-off",
                            NULL, sve_tests_sve_off);
    }

    if (kvm_available) {
        qtest_add_data_func("/arm/kvm/query-cpu-model-expansion",
                            NULL, test_query_cpu_model_expansion_kvm);
        qtest_add_data_func("/arm/kvm/query-cpu-model-expansion/sve-off",
                            NULL, sve_tests_sve_off_kvm);
    }

    return g_test_run();
}
