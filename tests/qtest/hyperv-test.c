/*
 * Hyper-V emulation CPU feature test cases
 *
 * Copyright (c) 2021 Red Hat Inc.
 * Authors:
 *  Vitaly Kuznetsov <vkuznets@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include <linux/kvm.h>
#include <sys/ioctl.h>

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "libqos/libqtest.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qjson.h"

#define MACHINE_KVM "-machine pc-q35-5.2 -accel kvm "
#define QUERY_HEAD  "{ 'execute': 'query-cpu-model-expansion', " \
                    "  'arguments': { 'type': 'full', "
#define QUERY_TAIL  "}}"

static bool kvm_enabled(QTestState *qts)
{
    QDict *resp, *qdict;
    bool enabled;

    resp = qtest_qmp(qts, "{ 'execute': 'query-kvm' }");
    g_assert(qdict_haskey(resp, "return"));
    qdict = qdict_get_qdict(resp, "return");
    g_assert(qdict_haskey(qdict, "enabled"));
    enabled = qdict_get_bool(qdict, "enabled");
    qobject_unref(resp);

    return enabled;
}

static bool kvm_has_cap(int cap)
{
    int fd = open("/dev/kvm", O_RDWR);
    int ret;

    if (fd < 0) {
        return false;
    }

    ret = ioctl(fd, KVM_CHECK_EXTENSION, cap);

    close(fd);

    return ret > 0;
}

static QDict *do_query_no_props(QTestState *qts, const char *cpu_type)
{
    return qtest_qmp(qts, QUERY_HEAD "'model': { 'name': %s }"
                          QUERY_TAIL, cpu_type);
}

static bool resp_has_props(QDict *resp)
{
    QDict *qdict;

    g_assert(resp);

    if (!qdict_haskey(resp, "return")) {
        return false;
    }
    qdict = qdict_get_qdict(resp, "return");

    if (!qdict_haskey(qdict, "model")) {
        return false;
    }
    qdict = qdict_get_qdict(qdict, "model");

    return qdict_haskey(qdict, "props");
}

static QDict *resp_get_props(QDict *resp)
{
    QDict *qdict;

    g_assert(resp);
    g_assert(resp_has_props(resp));

    qdict = qdict_get_qdict(resp, "return");
    qdict = qdict_get_qdict(qdict, "model");
    qdict = qdict_get_qdict(qdict, "props");

    return qdict;
}

static bool resp_get_feature(QDict *resp, const char *feature)
{
    QDict *props;

    g_assert(resp);
    g_assert(resp_has_props(resp));
    props = resp_get_props(resp);
    g_assert(qdict_get(props, feature));
    return qdict_get_bool(props, feature);
}

#define assert_has_feature(qts, cpu_type, feature)                     \
({                                                                     \
    QDict *_resp = do_query_no_props(qts, cpu_type);                   \
    g_assert(_resp);                                                   \
    g_assert(resp_has_props(_resp));                                   \
    g_assert(qdict_get(resp_get_props(_resp), feature));               \
    qobject_unref(_resp);                                              \
})

#define resp_assert_feature(resp, feature, expected_value)             \
({                                                                     \
    QDict *_props;                                                     \
                                                                       \
    g_assert(_resp);                                                   \
    g_assert(resp_has_props(_resp));                                   \
    _props = resp_get_props(_resp);                                    \
    g_assert(qdict_get(_props, feature));                              \
    g_assert(qdict_get_bool(_props, feature) == (expected_value));     \
})

#define assert_feature(qts, cpu_type, feature, expected_value)         \
({                                                                     \
    QDict *_resp;                                                      \
                                                                       \
    _resp = do_query_no_props(qts, cpu_type);                          \
    g_assert(_resp);                                                   \
    resp_assert_feature(_resp, feature, expected_value);               \
    qobject_unref(_resp);                                              \
})

#define assert_has_feature_enabled(qts, cpu_type, feature)             \
    assert_feature(qts, cpu_type, feature, true)

#define assert_has_feature_disabled(qts, cpu_type, feature)            \
    assert_feature(qts, cpu_type, feature, false)

static void test_assert_hyperv_all_but_evmcs(QTestState *qts)
{
    assert_has_feature_enabled(qts, "host", "hv-relaxed");
    assert_has_feature_enabled(qts, "host", "hv-vapic");
    assert_has_feature_enabled(qts, "host", "hv-vpindex");
    assert_has_feature_enabled(qts, "host", "hv-runtime");
    assert_has_feature_enabled(qts, "host", "hv-crash");
    assert_has_feature_enabled(qts, "host", "hv-time");
    assert_has_feature_enabled(qts, "host", "hv-synic");
    assert_has_feature_enabled(qts, "host", "hv-stimer");
    assert_has_feature_enabled(qts, "host", "hv-tlbflush");
    assert_has_feature_enabled(qts, "host", "hv-ipi");
    assert_has_feature_enabled(qts, "host", "hv-reset");
    assert_has_feature_enabled(qts, "host", "hv-frequencies");
    assert_has_feature_enabled(qts, "host", "hv-reenlightenment");
    assert_has_feature_enabled(qts, "host", "hv-stimer-direct");
}

static void test_query_cpu_hv_all_but_evmcs(const void *data)
{
    QTestState *qts;

    qts = qtest_init(MACHINE_KVM "-cpu host,hv-relaxed,hv-vapic,hv-vpindex,"
                     "hv-runtime,hv-crash,hv-time,hv-synic,hv-stimer,"
                     "hv-tlbflush,hv-ipi,hv-reset,hv-frequencies,"
                     "hv-reenlightenment,hv-stimer-direct");

    test_assert_hyperv_all_but_evmcs(qts);

    qtest_quit(qts);
}

static void test_query_cpu_hv_custom(const void *data)
{
    QTestState *qts;

    qts = qtest_init(MACHINE_KVM "-cpu host,hv-vpindex");

    assert_has_feature_enabled(qts, "host", "hv-vpindex");
    assert_has_feature_disabled(qts, "host", "hv-synic");

    qtest_quit(qts);
}

static void test_query_cpu_hv_passthrough(const void *data)
{
    QTestState *qts;
    QDict *resp;

    qts = qtest_init(MACHINE_KVM "-cpu host,hv-passthrough");
    if (!kvm_enabled(qts)) {
        qtest_quit(qts);
        return;
    }

    test_assert_hyperv_all_but_evmcs(qts);

    resp = do_query_no_props(qts, "host");
    if (resp_get_feature(resp, "vmx")) {
        assert_has_feature_enabled(qts, "host", "hv-evmcs");
    } else {
        assert_has_feature_disabled(qts, "host", "hv-evmcs");
    }

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    if (!kvm_has_cap(KVM_CAP_HYPERV_CPUID)) {
        g_test_message("Skipping test: KVM not available or too old");
        return g_test_run();
    }

    qtest_add_data_func("/hyperv/hv-all-but-evmcs",
                        NULL, test_query_cpu_hv_all_but_evmcs);
    qtest_add_data_func("/hyperv/hv-custom",
                        NULL, test_query_cpu_hv_custom);
    if (kvm_has_cap(KVM_CAP_SYS_HYPERV_CPUID)) {
        qtest_add_data_func("/hyperv/hv-passthrough",
                            NULL, test_query_cpu_hv_passthrough);
    }

    return g_test_run();
}
