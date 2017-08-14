/*
 * Unit tests for QAPI utility functions
 *
 * Copyright (C) 2017 Red Hat Inc.
 *
 * Authors:
 *  Eduardo Habkost <ehabkost@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/util.h"
#include "qapi/qmp/qnum.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qmp/qlist.h"
#include "qapi/qmp/qjson.h"
#include "hw/qdev-slotinfo.h"

#define JS(json) qobject_from_json((json), &error_abort)

static bool json_valuelist_contains(const char *jvalues, const char *jvalue)
{
    QObject *values = JS(jvalues);
    QObject *value = JS(jvalue);
    bool r = valuelist_contains(values, value);

    qobject_decref(values);
    qobject_decref(value);
    return r;
}

static void test_valuelist_contains(void)
{
    g_assert_true(json_valuelist_contains("100", "100"));
    g_assert_false(json_valuelist_contains("100", "200"));

    g_assert_false(json_valuelist_contains("[]", "100"));
    g_assert_true(json_valuelist_contains("[100, 200, 300]", "200"));
    g_assert_false(json_valuelist_contains("[100, 200, 300]", "150"));

    g_assert_true(json_valuelist_contains("\"abc\"", "\"abc\""));
    g_assert_false(json_valuelist_contains("\"abc\"", "\"xyz\""));
    g_assert_true(json_valuelist_contains("[\"abc\"]", "\"abc\""));
    g_assert_false(json_valuelist_contains("[\"abc\", \"cde\"]", "\"xyz\""));

#define TEST_RANGE "[[1,10], [18,20], [\"aaaa2\", \"jyz3\"], [-100, 5]," \
                    "\"kkk\", 14, -50, [51], [[30, 31]] ]"

    /* [-100, 5] */
    g_assert_false(json_valuelist_contains(TEST_RANGE, "-101"));
    g_assert_true( json_valuelist_contains(TEST_RANGE, "-100"));
    g_assert_true( json_valuelist_contains(TEST_RANGE,  "-99"));

    /* -50 */
    g_assert_true( json_valuelist_contains(TEST_RANGE,  "-51"));
    g_assert_true( json_valuelist_contains(TEST_RANGE,  "-50"));
    g_assert_true( json_valuelist_contains(TEST_RANGE,  "-49"));

    /* [-100, 5], [1, 10] */
    g_assert_true( json_valuelist_contains(TEST_RANGE,   "-1"));
    g_assert_true( json_valuelist_contains(TEST_RANGE,    "0"));
    g_assert_true( json_valuelist_contains(TEST_RANGE,    "1"));
    g_assert_true( json_valuelist_contains(TEST_RANGE,    "2"));

    /* [-100, 5] */
    g_assert_true( json_valuelist_contains(TEST_RANGE,    "4"));
    g_assert_true( json_valuelist_contains(TEST_RANGE,    "5"));
    g_assert_true( json_valuelist_contains(TEST_RANGE,    "6"));

    /* [1, 10] */
    g_assert_true( json_valuelist_contains(TEST_RANGE,    "9"));
    g_assert_true( json_valuelist_contains(TEST_RANGE,   "10"));
    g_assert_false(json_valuelist_contains(TEST_RANGE,   "11"));

    /* 14 */
    g_assert_false(json_valuelist_contains(TEST_RANGE,   "13"));
    g_assert_true( json_valuelist_contains(TEST_RANGE,   "14"));
    g_assert_false(json_valuelist_contains(TEST_RANGE,   "15"));

    /* [18, 20] */
    g_assert_false(json_valuelist_contains(TEST_RANGE,   "17"));
    g_assert_true( json_valuelist_contains(TEST_RANGE,   "18"));
    g_assert_true( json_valuelist_contains(TEST_RANGE,   "19"));
    g_assert_true( json_valuelist_contains(TEST_RANGE,   "20"));
    g_assert_false(json_valuelist_contains(TEST_RANGE,   "21"));

    /* [51] */
    g_assert_false(json_valuelist_contains(TEST_RANGE,   "50"));
    g_assert_true( json_valuelist_contains(TEST_RANGE,   "51"));
    g_assert_false(json_valuelist_contains(TEST_RANGE,   "52"));

    /* [ "aaa2" , "jyz3" ] */
    g_assert_false(json_valuelist_contains(TEST_RANGE,   "\"aaaa\""));
    g_assert_false(json_valuelist_contains(TEST_RANGE,   "\"aaaa1\""));
    g_assert_true( json_valuelist_contains(TEST_RANGE,   "\"aaaa2\""));
    g_assert_true(json_valuelist_contains(TEST_RANGE,   "\"aaaa3\""));

    /* [ "aaa2" , "jyz3" ] */
    g_assert_true(json_valuelist_contains(TEST_RANGE,   "\"bcde\""));

    /* [ "aaa2" , "jyz3" ] */
    g_assert_true( json_valuelist_contains(TEST_RANGE,   "\"jyz\""));
    g_assert_true( json_valuelist_contains(TEST_RANGE,   "\"jyz2\""));
    g_assert_true( json_valuelist_contains(TEST_RANGE,   "\"jyz3\""));
    g_assert_false(json_valuelist_contains(TEST_RANGE,   "\"jyz4\""));

    /* "kkk" */
    g_assert_false(json_valuelist_contains(TEST_RANGE,   "\"kk\""));
    g_assert_true( json_valuelist_contains(TEST_RANGE,   "\"kkk\""));
    g_assert_false(json_valuelist_contains(TEST_RANGE,   "\"kkkk\""));

    /* [[30, 31]] */
    g_assert_false(json_valuelist_contains(TEST_RANGE,   "30"));
    g_assert_false(json_valuelist_contains(TEST_RANGE,   "[30]"));
    g_assert_true( json_valuelist_contains(TEST_RANGE,   "[30, 31]"));

    /* empty set doesn't contain an empty list: */
    g_assert_false(json_valuelist_contains("[]", "[]"));

    /* [] is an invalid element on a value list: */
    g_assert_false(json_valuelist_contains("[[]]", "[]"));

    /* [[]] indicates [] is a valid value */
    g_assert_true(json_valuelist_contains("[[[]]]", "[]"));
}

#define assert_valuelist_extend(before, extend, after)           \
    do {                                                         \
        QObject *set = JS(before);                               \
        QObject *expected = JS(after);                           \
                                                                 \
        valuelist_extend(&set, JS(extend));;                     \
        g_assert_cmpint(qobject_compare(set, expected), ==, 0);  \
                                                                 \
        qobject_decref(set);                                     \
        qobject_decref(expected);                                \
    } while (0)

static void test_valuelist_extend(void)
{
    assert_valuelist_extend("[]",
                            "1",
                            "1");

    assert_valuelist_extend("1",
                            "1",
                            "1");

    assert_valuelist_extend("1",
                            "3",
                            "[1, 3]");

    assert_valuelist_extend("[1, 3]",
                            "6",
                            "[1, 3, 6]");

    /*TODO: implement and test range merging */

    /* single-element becomes range: */
    assert_valuelist_extend("[1, 3, 6]",
                            "4",
                            "[1, [3, 4], 6]");
    assert_valuelist_extend("[1, 4, 6]",
                            "3",
                            "[1, [3, 4], 6]");


    /* single-element merges two elements: */
    assert_valuelist_extend("[1, 3, 6]",
                            "2",
                            "[[1, 3], 6]");

    /* [] -> empty set */
    assert_valuelist_extend("[1, 3, 6]",
                            "[]",
                            "[1, 3, 6]");

    /* [3, 100] -> two elements: 3 and 100 (not a range) */
    assert_valuelist_extend("[[1, 4], 6]",
                            "[3, 100]",
                            "[[1, 4], 6, 100]");

    /* tests for appending new ranges: */

    /* add two ranges: 7-30, 40-50 */
    assert_valuelist_extend("[[1, 4], 6, 100]",
                            "[[7, 30], [40, 50]]",
                            "[[1, 4], [6, 30], 100, [40, 50]]");

    /* multiple ways of appending to a range: */
    assert_valuelist_extend("[[1, 4], [6, 30], [40, 50], [53, 60]]",
                            "30",
                            "[[1, 4], [6, 30], [40, 50], [53, 60]]");
    assert_valuelist_extend("[[1, 4], [6, 30], [40, 50], [53, 60]]",
                            "31",
                            "[[1, 4], [6, 31], [40, 50], [53, 60]]");
    assert_valuelist_extend("[[1, 4], [6, 30], [40, 50], [53, 60]]",
                            "[[25, 35]]",
                            "[[1, 4], [6, 35], [40, 50], [53, 60]]");
    assert_valuelist_extend("[[1, 4], [6, 30], [40, 50], [53, 60]]",
                            "[[30, 35]]",
                            "[[1, 4], [6, 35], [40, 50], [53, 60]]");
    assert_valuelist_extend("[[1, 4], [6, 30], [40, 50], [53, 60]]",
                            "[[31, 35]]",
                            "[[1, 4], [6, 35], [40, 50], [53, 60]]");
    //TODO: make this work:
    assert_valuelist_extend("[[1, 4], [6, 30], [40, 50], [53, 60]]",
                            "[[38, 51]]",
                            "[[1, 4], [6, 30], [38, 51], [53, 60]]");
    assert_valuelist_extend("[[1, 4], [6, 30], [40, 50], [53, 60]]",
                            "[[38, 52]]",
                            "[[1, 4], [6, 30], [38, 60]]");
    /* off-by-one check: */
    assert_valuelist_extend("[[1, 4], [6, 30], [40, 50], [53, 60]]",
                            "51",
                            "[[1, 4], [6, 30], [40, 51], [53, 60]]");
    /* _not_ appending to a range: */
    assert_valuelist_extend("[[1, 4], [6, 30], [40, 50], [53, 60]]",
                            "32",
                            "[[1, 4], [6, 30], [40, 50], [53, 60], 32]");
    assert_valuelist_extend("[[1, 4], [6, 30], [40, 50], [53, 60]]",
                            "[[32, 35]]",
                            "[[1, 4], [6, 30], [40, 50], [53, 60], [32, 35]]");

    /* multiple ways of prepending to a range: */
    assert_valuelist_extend("[[1, 4], [6, 30], [40, 50], [53, 60]]",
                            "40",
                            "[[1, 4], [6, 30], [40, 50], [53, 60]]");
    assert_valuelist_extend("[[1, 4], [6, 30], [40, 50], [53, 60]]",
                            "39",
                            "[[1, 4], [6, 30], [39, 50], [53, 60]]");
    assert_valuelist_extend("[[1, 4], [6, 30], [40, 50], [53, 60]]",
                            "[[35, 45]]",
                            "[[1, 4], [6, 30], [35, 50], [53, 60]]");
    assert_valuelist_extend("[[1, 4], [6, 30], [40, 50], [53, 60]]",
                            "[[35, 40]]",
                            "[[1, 4], [6, 30], [35, 50], [53, 60]]");
    assert_valuelist_extend("[[1, 4], [6, 30], [40, 50], [53, 60]]",
                            "[[35, 39]]",
                            "[[1, 4], [6, 30], [35, 50], [53, 60]]");
    /* off-by-one check: */
    assert_valuelist_extend("[[1, 4], [6, 30], [33, 50], [53, 60]]",
                            "32",
                            "[[1, 4], [6, 30], [32, 50], [53, 60]]");
    /* _not_ prepending to a range: */
    assert_valuelist_extend("[[1, 4], [6, 30], [40, 50], [53, 60]]",
                            "38",
                            "[[1, 4], [6, 30], [40, 50], [53, 60], 38]");
    assert_valuelist_extend("[[1, 4], [6, 30], [40, 50], [53, 60]]",
                            "[[35, 38]]",
                            "[[1, 4], [6, 30], [40, 50], [53, 60], [35, 38]]");

    /* multiple ways of combining two ranges: */
    assert_valuelist_extend("[[1, 4], [6, 30], [40, 50], [53, 60]]",
                            "5",
                            "[[1, 30], [40, 50], [53, 60]]");
    assert_valuelist_extend("[[1, 4], [6, 30], [40, 50], [53, 60]]",
                            "[[25, 45]]",
                            "[[1, 4], [6, 50], [53, 60]]");
}

static void test_slots_can_combine(void)
{
    DeviceSlotInfo *a = g_new0(DeviceSlotInfo, 1);
    DeviceSlotInfo *b = g_new0(DeviceSlotInfo, 1);
    const char *opt_name = NULL;

    g_assert_true(slots_can_be_combined(a, b, &opt_name));
    g_assert_null(opt_name);

    slot_add_opt(a, "bus", JS("\"mybus.0\""));
    g_assert_false(slots_can_be_combined(a, b, &opt_name));
    slot_add_opt(b, "bus", JS("\"mybus.0\""));

    g_assert_true(slots_can_be_combined(a, b, &opt_name));
    g_assert_null(opt_name);

    slot_add_opt(a, "addr", JS("[ 1, 3 ]"));
    g_assert_false(slots_can_be_combined(a, b, &opt_name));
    slot_add_opt(b, "addr", JS("5"));

    g_assert_true(slots_can_be_combined(a, b, &opt_name));
    g_assert_cmpstr(opt_name, ==, "addr");

    slot_add_opt(a, "unit", JS("1"));
    g_assert_false(slots_can_be_combined(a, b, &opt_name));
    slot_add_opt(b, "unit", JS("1"));

    g_assert_true(slots_can_be_combined(a, b, &opt_name));
    g_assert_cmpstr(opt_name, ==, "addr");

    a->hotpluggable = true;
    g_assert_false(slots_can_be_combined(a, b, &opt_name));
    a->hotpluggable = false;

    a->has_device = true;
    a->device = g_strdup("/machine/somedevice");
    g_assert_false(slots_can_be_combined(a, b, &opt_name));
    g_free(a->device);
    a->has_device = false;
    a->device = NULL;

    slot_add_opt(a, "port", JS("10"));
    g_assert_false(slots_can_be_combined(a, b, &opt_name));
    slot_add_opt(b, "port", JS("20"));

    g_assert_false(slots_can_be_combined(a, b, &opt_name));
}

static void test_slots_combine(void)
{
    DeviceSlotInfo *a = g_new0(DeviceSlotInfo, 1);
    DeviceSlotInfo *b = g_new0(DeviceSlotInfo, 1);
    SlotOption *o;

    slot_add_opt(a, "bus", JS("\"mybus.0\""));
    slot_add_opt(b, "bus", JS("\"mybus.0\""));

    slot_add_opt(a, "addr", JS("[ 1, 3 ]"));
    slot_add_opt(b, "addr", JS("5"));

    slot_add_opt(a, "unit", JS("1"));
    slot_add_opt(b, "unit", JS("1"));

    g_assert_true(slots_try_combine(a, b));

    o = a->opts->value;
    g_assert_cmpstr(o->option, ==, "unit");
    g_assert_cmpint(qobject_compare(o->values, JS("1")), ==, 0);

    o = a->opts->next->value;
    g_assert_cmpstr(o->option, ==, "addr");
    g_assert_cmpint(qobject_compare(o->values, JS("[1, 3, 5]")), ==, 0);

    o = a->opts->next->next->value;
    g_assert_cmpstr(o->option, ==, "bus");
    g_assert_cmpint(qobject_compare(o->values, JS("\"mybus.0\"")), ==, 0);
}

static void test_slot_list_collapse(void)
{
    DeviceSlotInfoList *l = NULL;
    SlotOption *o;
    int node, socket, core, thread;

    for (node = 0; node < 4; node++) {
        for (socket = 0; socket < 8; socket++) {
            for (core = 0; core < 4; core++) {
                for (thread = 0; thread < 2; thread++) {
                    DeviceSlotInfo *s = g_new0(DeviceSlotInfo, 1);
                    slot_add_opt_int(s, "node", node);
                    slot_add_opt_int(s, "socket", socket);
                    slot_add_opt_int(s, "core", core);
                    slot_add_opt_int(s, "thread", thread);
                    slot_list_add_slot(&l, s);
                }
            }
        }
    }

    /*
     * All the entries above should be merged in a single entry:
     * node = [0, 3]
     * socket = [0, 7]
     * core = [0, 3]
     * thread = [0, 1]
     */
    l = slot_list_collapse(l);
    g_assert_nonnull(l);
    g_assert_null(l->next);

    o = slot_find_opt(l->value, "node");
    g_assert_cmpint(qobject_compare(o->values, JS("[ [0, 3] ]")), ==, 0);

    o = slot_find_opt(l->value, "socket");
    g_assert_cmpint(qobject_compare(o->values, JS("[ [0, 7] ]")), ==, 0);

    o = slot_find_opt(l->value, "core");
    g_assert_cmpint(qobject_compare(o->values, JS("[ [0, 3] ]")), ==, 0);

    o = slot_find_opt(l->value, "thread");
    g_assert_cmpint(qobject_compare(o->values, JS("[ [0, 1] ]")), ==, 0);
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/qapi/util/valuelist_contains", test_valuelist_contains);
    g_test_add_func("/qapi/util/valuelist_extend", test_valuelist_extend);
    g_test_add_func("/qapi/util/slots_can_combine", test_slots_can_combine);
    g_test_add_func("/qapi/util/slots_combine", test_slots_combine);
    g_test_add_func("/qapi/util/slot_list_collapse", test_slot_list_collapse);
    g_test_run();
    return 0;
}
