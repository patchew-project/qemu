/*
 * QMP protocol test cases
 *
 * Copyright (c) 2017 Red Hat Inc.
 *
 * Authors:
 *  Markus Armbruster <armbru@redhat.com>,
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qapi-visit.h"
#include "qapi/error.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/util.h"
#include "qapi/visitor.h"
#include "qapi/qmp/qstring.h"

const char common_args[] = "-nodefaults -machine none";

static const char *get_error_class(QDict *resp)
{
    QDict *error = qdict_get_qdict(resp, "error");
    const char *desc = qdict_get_try_str(error, "desc");

    g_assert(desc);
    return error ? qdict_get_try_str(error, "class") : NULL;
}

static void test_version(QObject *version)
{
    Visitor *v;
    VersionInfo *vinfo;

    g_assert(version);
    v = qobject_input_visitor_new(version);
    visit_type_VersionInfo(v, "version", &vinfo, &error_abort);
    qapi_free_VersionInfo(vinfo);
    visit_free(v);
}

static void test_malformed(void)
{
    QDict *resp;

    /* Not even a dictionary */
    resp = qmp("null");
    g_assert_cmpstr(get_error_class(resp), ==, "GenericError");
    QDECREF(resp);

    /* No "execute" key */
    resp = qmp("{}");
    g_assert_cmpstr(get_error_class(resp), ==, "GenericError");
    QDECREF(resp);

    /* "execute" isn't a string */
    resp = qmp("{ 'execute': true }");
    g_assert_cmpstr(get_error_class(resp), ==, "GenericError");
    QDECREF(resp);

    /* "arguments" isn't a dictionary */
    resp = qmp("{ 'execute': 'no-such-cmd', 'arguments': [] }");
    g_assert_cmpstr(get_error_class(resp), ==, "GenericError");
    QDECREF(resp);

    /* extra key */
    resp = qmp("{ 'execute': 'no-such-cmd', 'extra': true }");
    g_assert_cmpstr(get_error_class(resp), ==, "GenericError");
    QDECREF(resp);
}

static void test_qmp_protocol(void)
{
    QDict *resp, *q, *ret;
    QList *capabilities;
    const QListEntry *entry;
    QString *qstr;
    int i;

    global_qtest = qtest_init_without_qmp_handshake(common_args);

    /* Test greeting */
    resp = qmp_receive();
    q = qdict_get_qdict(resp, "QMP");
    g_assert(q);
    test_version(qdict_get(q, "version"));
    capabilities = qdict_get_qlist(q, "capabilities");
    g_assert(capabilities);
    entry = qlist_first(capabilities);
    g_assert(entry);
    qstr = qobject_to_qstring(entry->value);
    g_assert(qstr);
    g_assert_cmpstr(qstring_get_str(qstr), ==, "oob");
    QDECREF(resp);

    /* Test valid command before handshake */
    resp = qmp("{ 'execute': 'query-version' }");
    g_assert_cmpstr(get_error_class(resp), ==, "CommandNotFound");
    QDECREF(resp);

    /* Test malformed commands before handshake */
    test_malformed();

    /* Test handshake */
    resp = qmp("{ 'execute': 'qmp_capabilities' }");
    ret = qdict_get_qdict(resp, "return");
    g_assert(ret && !qdict_size(ret));
    QDECREF(resp);

    /* Test repeated handshake */
    resp = qmp("{ 'execute': 'qmp_capabilities' }");
    g_assert_cmpstr(get_error_class(resp), ==, "CommandNotFound");
    QDECREF(resp);

    /* Test valid command */
    resp = qmp("{ 'execute': 'query-version' }");
    test_version(qdict_get(resp, "return"));
    QDECREF(resp);

    /* Test malformed commands */
    test_malformed();

    /* Test 'id' */
    resp = qmp("{ 'execute': 'query-name', 'id': 'cookie#1' }");
    ret = qdict_get_qdict(resp, "return");
    g_assert(ret);
    g_assert_cmpstr(qdict_get_try_str(resp, "id"), ==, "cookie#1");
    QDECREF(resp);

    /* Test command failure with 'id' */
    resp = qmp("{ 'execute': 'human-monitor-command', 'id': 2 }");
    g_assert_cmpstr(get_error_class(resp), ==, "GenericError");
    g_assert_cmpint(qdict_get_int(resp, "id"), ==, 2);
    QDECREF(resp);

    /*
     * Test command batching.  In current test OOB is not enabled, we
     * should be able to run as many commands in batch as we like.
     * Using 16 (>8, which is OOB queue length) to make sure OOB
     * won't break existing clients.
     */
    for (i = 0; i < 16; i++) {
        qmp_async("{ 'execute': 'query-version' }");
    }
    /* Verify the replies to make sure no command is dropped. */
    for (i = 0; i < 16; i++) {
        resp = qmp_receive();
        /* It should never be dropped.  Each of them should be a reply. */
        g_assert(qdict_haskey(resp, "return"));
        g_assert(!qdict_haskey(resp, "event"));
        QDECREF(resp);
    }

    qtest_end();
}

/* Tests for Out-Of-Band support. */
static void test_qmp_oob(void)
{
    QDict *resp;
    int acks = 0;
    char *qtest_params;
    const char *cmd_id, *extra_params;
    const char *arch = qtest_get_arch();

    /*
     * Some archs need to specify cpu to make sure dump-guest-memory
     * can work.  I chose CPU type randomly.
     */
    if (g_strcmp0(arch, "aarch64") == 0) {
        extra_params = "-cpu cortex-a57";
    } else if (g_strcmp0(arch, "ppc64") == 0) {
        extra_params = "-cpu power8";
    } else {
        extra_params = "";
    }

    /*
     * Let's have some memory to make sure dump-guest-memory will be
     * time consuming.  That is required to test OOB functionaility.
     */
    qtest_params = g_strdup_printf("-nodefaults -machine none -m 1G %s",
                                   extra_params);
    global_qtest = qtest_init_without_qmp_handshake(qtest_params);
    g_free(qtest_params);

    /* Ignore the greeting message. */
    resp = qmp_receive();
    g_assert(qdict_get_qdict(resp, "QMP"));
    QDECREF(resp);

    /* Try a fake capability, it should fail. */
    resp = qmp("{ 'execute': 'qmp_capabilities', "
               "  'arguments': { 'enable': [ 'cap-does-not-exist' ] } }");
    g_assert(qdict_haskey(resp, "error"));

    /* Now, enable OOB in current QMP session, it should success. */
    resp = qmp("{ 'execute': 'qmp_capabilities', "
               "  'arguments': { 'enable': [ 'oob' ] } }");
    g_assert(qdict_haskey(resp, "return"));

    /*
     * Try any command that does not support OOB but with OOB flag. We
     * should get failure.
     */
    resp = qmp("{ 'execute': 'query-cpus',"
               "  'control': { 'run-oob': true } }");
    g_assert(qdict_haskey(resp, "error"));

    /*
     * Try a time-consuming command, following by a OOB command, make
     * sure we get OOB command before the time-consuming one (which is
     * run in the parser).
     *
     * When writting up this test script, the only command that
     * support OOB is migrate-incoming.  It's not the best command to
     * test OOB but we don't really have a choice here.  We will check
     * arriving order but not command errors, which does not really
     * matter to us.
     */
    qmp_async("{ 'execute': 'dump-guest-memory',"
              "  'arguments': { 'paging': true, "
              "                 'protocol': 'file:/dev/null' }, "
              "  'id': 'time-consuming-cmd'}");
    qmp_async("{ 'execute': 'migrate-incoming', "
              "  'control': { 'run-oob': true }, "
              "  'id': 'oob-cmd' }");

    /* Ignore all events.  Wait for 2 acks */
    while (acks < 2) {
        resp = qmp_receive();
        if (qdict_haskey(resp, "event")) {
            /* Skip possible events */
            continue;
        }
        cmd_id = qdict_get_str(resp, "id");
        if (acks == 0) {
            /* Need to receive OOB response first */
            g_assert_cmpstr(cmd_id, ==, "oob-cmd");
        } else if (acks == 1) {
            g_assert_cmpstr(cmd_id, ==, "time-consuming-cmd");
        }
        acks++;
    }

    qtest_end();
}

static int query_error_class(const char *cmd)
{
    static struct {
        const char *cmd;
        int err_class;
    } fails[] = {
        /* Success depends on build configuration: */
#ifndef CONFIG_SPICE
        { "query-spice", ERROR_CLASS_COMMAND_NOT_FOUND },
#endif
#ifndef CONFIG_VNC
        { "query-vnc", ERROR_CLASS_GENERIC_ERROR },
        { "query-vnc-servers", ERROR_CLASS_GENERIC_ERROR },
#endif
#ifndef CONFIG_REPLICATION
        { "query-xen-replication-status", ERROR_CLASS_COMMAND_NOT_FOUND },
#endif
        /* Likewise, and require special QEMU command-line arguments: */
        { "query-acpi-ospm-status", ERROR_CLASS_GENERIC_ERROR },
        { "query-balloon", ERROR_CLASS_DEVICE_NOT_ACTIVE },
        { "query-hotpluggable-cpus", ERROR_CLASS_GENERIC_ERROR },
        { "query-vm-generation-id", ERROR_CLASS_GENERIC_ERROR },
        { NULL, -1 }
    };
    int i;

    for (i = 0; fails[i].cmd; i++) {
        if (!strcmp(cmd, fails[i].cmd)) {
            return fails[i].err_class;
        }
    }
    return -1;
}

static void test_query(const void *data)
{
    const char *cmd = data;
    int expected_error_class = query_error_class(cmd);
    QDict *resp, *error;
    const char *error_class;

    qtest_start(common_args);

    resp = qmp("{ 'execute': %s }", cmd);
    error = qdict_get_qdict(resp, "error");
    error_class = error ? qdict_get_str(error, "class") : NULL;

    if (expected_error_class < 0) {
        g_assert(qdict_haskey(resp, "return"));
    } else {
        g_assert(error);
        g_assert_cmpint(qapi_enum_parse(&QapiErrorClass_lookup, error_class,
                                        -1, &error_abort),
                        ==, expected_error_class);
    }
    QDECREF(resp);

    qtest_end();
}

static bool query_is_blacklisted(const char *cmd)
{
    const char *blacklist[] = {
        /* Not actually queries: */
        "add-fd",
        /* Success depends on target arch: */
        "query-cpu-definitions",  /* arm, i386, ppc, s390x */
        "query-gic-capabilities", /* arm */
        /* Success depends on target-specific build configuration: */
        "query-pci",              /* CONFIG_PCI */
        NULL
    };
    int i;

    for (i = 0; blacklist[i]; i++) {
        if (!strcmp(cmd, blacklist[i])) {
            return true;
        }
    }
    return false;
}

typedef struct {
    SchemaInfoList *list;
    GHashTable *hash;
} QmpSchema;

static void qmp_schema_init(QmpSchema *schema)
{
    QDict *resp;
    Visitor *qiv;
    SchemaInfoList *tail;

    qtest_start(common_args);
    resp = qmp("{ 'execute': 'query-qmp-schema' }");

    qiv = qobject_input_visitor_new(qdict_get(resp, "return"));
    visit_type_SchemaInfoList(qiv, NULL, &schema->list, &error_abort);
    visit_free(qiv);

    QDECREF(resp);
    qtest_end();

    schema->hash = g_hash_table_new(g_str_hash, g_str_equal);

    /* Build @schema: hash table mapping entity name to SchemaInfo */
    for (tail = schema->list; tail; tail = tail->next) {
        g_hash_table_insert(schema->hash, tail->value->name, tail->value);
    }
}

static SchemaInfo *qmp_schema_lookup(QmpSchema *schema, const char *name)
{
    return g_hash_table_lookup(schema->hash, name);
}

static void qmp_schema_cleanup(QmpSchema *schema)
{
    qapi_free_SchemaInfoList(schema->list);
    g_hash_table_destroy(schema->hash);
}

static bool object_type_has_mandatory_members(SchemaInfo *type)
{
    SchemaInfoObjectMemberList *tail;

    g_assert(type->meta_type == SCHEMA_META_TYPE_OBJECT);

    for (tail = type->u.object.members; tail; tail = tail->next) {
        if (!tail->value->has_q_default) {
            return true;
        }
    }

    return false;
}

static void add_query_tests(QmpSchema *schema)
{
    SchemaInfoList *tail;
    SchemaInfo *si, *arg_type, *ret_type;
    const char *test_name;

    /* Test the query-like commands */
    for (tail = schema->list; tail; tail = tail->next) {
        si = tail->value;
        if (si->meta_type != SCHEMA_META_TYPE_COMMAND) {
            continue;
        }

        if (query_is_blacklisted(si->name)) {
            continue;
        }

        arg_type = qmp_schema_lookup(schema, si->u.command.arg_type);
        if (object_type_has_mandatory_members(arg_type)) {
            continue;
        }

        ret_type = qmp_schema_lookup(schema, si->u.command.ret_type);
        if (ret_type->meta_type == SCHEMA_META_TYPE_OBJECT
            && !ret_type->u.object.members) {
            continue;
        }

        test_name = g_strdup_printf("qmp/%s", si->name);
        qtest_add_data_func(test_name, si->name, test_query);
    }
}

int main(int argc, char *argv[])
{
    QmpSchema schema;
    int ret;

    g_test_init(&argc, &argv, NULL);

    qtest_add_func("qmp/protocol", test_qmp_protocol);
    qtest_add_func("qmp/oob", test_qmp_oob);
    qmp_schema_init(&schema);
    add_query_tests(&schema);

    ret = g_test_run();

    qmp_schema_cleanup(&schema);
    return ret;
}
