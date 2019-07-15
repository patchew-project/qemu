#include "qemu/osdep.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qnum.h"
#include "qapi/qmp/qstring.h"
#include "qapi/error.h"
#include "qapi/qobject-input-visitor.h"
#include "tests/test-qapi-types.h"
#include "tests/test-qapi-visit.h"
#include "test-qapi-commands.h"

static QmpCommandList qmp_commands;

#if defined(TEST_IF_STRUCT) && defined(TEST_IF_CMD)
UserDefThree *qmp_TestIfCmd(TestIfStruct *foo, Error **errp)
{
    return NULL;
}
#endif

UserDefThree *qmp_TestCmdReturnDefThree(Error **errp)
{
    return NULL;
}

void qmp_user_def_cmd(Error **errp)
{
}

void qmp_test_flags_command(Error **errp)
{
}

void qmp_cmd_success_response(Error **errp)
{
}

static gboolean cmd_async_idle(gpointer user_data)
{
    QmpReturn *qret = user_data;

    qmp_cmd_async_return(qret, g_new0(Empty2, 1));

    return G_SOURCE_REMOVE;
}

void qmp_cmd_async(const char *filename, QmpReturn *qret)
{
    g_idle_add(cmd_async_idle, qret);
}

void qmp_cmd_success_response_async(const char *filename, QmpReturn *qret)
{
    Error *err = NULL;

    error_setg(&err, "no response, but error ok");
    qmp_return_error(qret, err);
}

Empty2 *qmp_user_def_cmd0(Error **errp)
{
    return g_new0(Empty2, 1);
}

void qmp_user_def_cmd1(UserDefOne * ud1, Error **errp)
{
}

void qmp_test_features(FeatureStruct0 *fs0, FeatureStruct1 *fs1,
                       FeatureStruct2 *fs2, FeatureStruct3 *fs3,
                       FeatureStruct4 *fs4, CondFeatureStruct1 *cfs1,
                       CondFeatureStruct2 *cfs2, CondFeatureStruct3 *cfs3,
                       Error **errp)
{
}

UserDefTwo *qmp_user_def_cmd2(UserDefOne *ud1a,
                              bool has_udb1, UserDefOne *ud1b,
                              Error **errp)
{
    UserDefTwo *ret;
    UserDefOne *ud1c = g_malloc0(sizeof(UserDefOne));
    UserDefOne *ud1d = g_malloc0(sizeof(UserDefOne));

    ud1c->string = strdup(ud1a->string);
    ud1c->integer = ud1a->integer;
    ud1d->string = strdup(has_udb1 ? ud1b->string : "blah0");
    ud1d->integer = has_udb1 ? ud1b->integer : 0;

    ret = g_new0(UserDefTwo, 1);
    ret->string0 = strdup("blah1");
    ret->dict1 = g_new0(UserDefTwoDict, 1);
    ret->dict1->string1 = strdup("blah2");
    ret->dict1->dict2 = g_new0(UserDefTwoDictDict, 1);
    ret->dict1->dict2->userdef = ud1c;
    ret->dict1->dict2->string = strdup("blah3");
    ret->dict1->dict3 = g_new0(UserDefTwoDictDict, 1);
    ret->dict1->has_dict3 = true;
    ret->dict1->dict3->userdef = ud1d;
    ret->dict1->dict3->string = strdup("blah4");

    return ret;
}

int64_t qmp_guest_get_time(int64_t a, bool has_b, int64_t b, Error **errp)
{
    return a + (has_b ? b : 0);
}

QObject *qmp_guest_sync(QObject *arg, Error **errp)
{
    return arg;
}

void qmp_boxed_struct(UserDefZero *arg, Error **errp)
{
}

void qmp_boxed_union(UserDefListUnion *arg, Error **errp)
{
}

__org_qemu_x_Union1 *qmp___org_qemu_x_command(__org_qemu_x_EnumList *a,
                                              __org_qemu_x_StructList *b,
                                              __org_qemu_x_Union2 *c,
                                              __org_qemu_x_Alt *d,
                                              Error **errp)
{
    __org_qemu_x_Union1 *ret = g_new0(__org_qemu_x_Union1, 1);

    ret->type = ORG_QEMU_X_UNION1_KIND___ORG_QEMU_X_BRANCH;
    ret->u.__org_qemu_x_branch.data = strdup("blah1");

    /* Also test that 'wchar-t' was munged to 'q_wchar_t' */
    if (b && b->value && !b->value->has_q_wchar_t) {
        b->value->q_wchar_t = 1;
    }
    return ret;
}

static void dispatch_cmd_return(QmpSession *session, QDict *resp)
{
    assert(resp != NULL);
    assert(!qdict_haskey(resp, "error"));
}

/* test commands with no input and no return value */
static void test_dispatch_cmd(void)
{
    QmpSession session = { 0, };
    QDict *req = qdict_new();

    qmp_session_init(&session, &qmp_commands, NULL, dispatch_cmd_return);
    qdict_put_str(req, "execute", "user_def_cmd");

    qmp_dispatch(&session, QOBJECT(req), false);

    qobject_unref(req);
    qmp_session_destroy(&session);
}

static void test_dispatch_cmd_oob(void)
{
    QmpSession session = { 0, };
    QDict *req = qdict_new();

    qmp_session_init(&session, &qmp_commands, NULL, dispatch_cmd_return);
    qdict_put_str(req, "exec-oob", "test-flags-command");

    qmp_dispatch(&session, QOBJECT(req), true);

    qobject_unref(req);
    qmp_session_destroy(&session);
}

static void dispatch_cmd_failure_return(QmpSession *session, QDict *resp)
{
    assert(resp != NULL);
    assert(qdict_haskey(resp, "error"));
}

/* test commands that return an error due to invalid parameters */
static void test_dispatch_cmd_failure(void)
{
    QmpSession session = { 0, };
    QDict *req = qdict_new();
    QDict *args = qdict_new();

    qmp_session_init(&session, &qmp_commands, NULL,
                     dispatch_cmd_failure_return);
    qdict_put_str(req, "execute", "user_def_cmd2");

    qmp_dispatch(&session, QOBJECT(req), false);

    qobject_unref(req);

    /* check that with extra arguments it throws an error */
    req = qdict_new();
    qdict_put_int(args, "a", 66);
    qdict_put(req, "arguments", args);
    qdict_put_str(req, "execute", "user_def_cmd");

    qmp_dispatch(&session, QOBJECT(req), false);

    qobject_unref(req);
    qmp_session_destroy(&session);
}

static QObject *dispatch_ret;

static void test_dispatch_cmd_success_response(void)
{
    QmpSession session = { 0, };
    QDict *req = qdict_new();

    qmp_session_init(&session, &qmp_commands, NULL, (QmpDispatchReturn *)abort);
    qdict_put_str(req, "execute", "cmd-success-response");

    qmp_dispatch(&session, QOBJECT(req), false);

    qobject_unref(req);
    qmp_session_destroy(&session);
}

static void dispatch_return(QmpSession *session, QDict *resp)
{
    assert(resp && !qdict_haskey(resp, "error"));
    dispatch_ret = qdict_get(resp, "return");
    qobject_ref(dispatch_ret);
}

static QObject *test_qmp_dispatch(QDict *req)
{
    QmpSession session = { 0, };
    QObject *ret;

    qmp_session_init(&session, &qmp_commands, NULL, dispatch_return);
    qmp_dispatch(&session, QOBJECT(req), false);
    ret = dispatch_ret;
    dispatch_ret = NULL;
    qmp_session_destroy(&session);
    return ret;
}

/* test commands that involve both input parameters and return values */
static void test_dispatch_cmd_io(void)
{
    QDict *req = qdict_new();
    QDict *args = qdict_new();
    QDict *args3 = qdict_new();
    QDict *ud1a = qdict_new();
    QDict *ud1b = qdict_new();
    QDict *ret, *ret_dict, *ret_dict_dict, *ret_dict_dict_userdef;
    QDict *ret_dict_dict2, *ret_dict_dict2_userdef;
    QNum *ret3;
    int64_t val;

    qdict_put_int(ud1a, "integer", 42);
    qdict_put_str(ud1a, "string", "hello");
    qdict_put_int(ud1b, "integer", 422);
    qdict_put_str(ud1b, "string", "hello2");
    qdict_put(args, "ud1a", ud1a);
    qdict_put(args, "ud1b", ud1b);
    qdict_put(req, "arguments", args);
    qdict_put_str(req, "execute", "user_def_cmd2");

    ret = qobject_to(QDict, test_qmp_dispatch(req));

    assert(!strcmp(qdict_get_str(ret, "string0"), "blah1"));
    ret_dict = qdict_get_qdict(ret, "dict1");
    assert(!strcmp(qdict_get_str(ret_dict, "string1"), "blah2"));
    ret_dict_dict = qdict_get_qdict(ret_dict, "dict2");
    ret_dict_dict_userdef = qdict_get_qdict(ret_dict_dict, "userdef");
    assert(qdict_get_int(ret_dict_dict_userdef, "integer") == 42);
    assert(!strcmp(qdict_get_str(ret_dict_dict_userdef, "string"), "hello"));
    assert(!strcmp(qdict_get_str(ret_dict_dict, "string"), "blah3"));
    ret_dict_dict2 = qdict_get_qdict(ret_dict, "dict3");
    ret_dict_dict2_userdef = qdict_get_qdict(ret_dict_dict2, "userdef");
    assert(qdict_get_int(ret_dict_dict2_userdef, "integer") == 422);
    assert(!strcmp(qdict_get_str(ret_dict_dict2_userdef, "string"), "hello2"));
    assert(!strcmp(qdict_get_str(ret_dict_dict2, "string"), "blah4"));
    qobject_unref(ret);

    qdict_put_int(args3, "a", 66);
    qdict_put(req, "arguments", args3);
    qdict_put_str(req, "execute", "guest-get-time");

    ret3 = qobject_to(QNum, test_qmp_dispatch(req));
    g_assert(qnum_get_try_int(ret3, &val));
    g_assert_cmpint(val, ==, 66);
    qobject_unref(ret3);

    qobject_unref(req);
}

/* test generated dealloc functions for generated types */
static void test_dealloc_types(void)
{
    UserDefOne *ud1test, *ud1a, *ud1b;
    UserDefOneList *ud1list;

    ud1test = g_malloc0(sizeof(UserDefOne));
    ud1test->integer = 42;
    ud1test->string = g_strdup("hi there 42");

    qapi_free_UserDefOne(ud1test);

    ud1a = g_malloc0(sizeof(UserDefOne));
    ud1a->integer = 43;
    ud1a->string = g_strdup("hi there 43");

    ud1b = g_malloc0(sizeof(UserDefOne));
    ud1b->integer = 44;
    ud1b->string = g_strdup("hi there 44");

    ud1list = g_malloc0(sizeof(UserDefOneList));
    ud1list->value = ud1a;
    ud1list->next = g_malloc0(sizeof(UserDefOneList));
    ud1list->next->value = ud1b;

    qapi_free_UserDefOneList(ud1list);
}

/* test generated deallocation on an object whose construction was prematurely
 * terminated due to an error */
static void test_dealloc_partial(void)
{
    static const char text[] = "don't leak me";

    UserDefTwo *ud2 = NULL;
    Error *err = NULL;

    /* create partial object */
    {
        QDict *ud2_dict;
        Visitor *v;

        ud2_dict = qdict_new();
        qdict_put_str(ud2_dict, "string0", text);

        v = qobject_input_visitor_new(QOBJECT(ud2_dict));
        visit_type_UserDefTwo(v, NULL, &ud2, &err);
        visit_free(v);
        qobject_unref(ud2_dict);
    }

    /* verify that visit_type_XXX() cleans up properly on error */
    error_free_or_abort(&err);
    assert(!ud2);

    /* Manually create a partial object, leaving ud2->dict1 at NULL */
    ud2 = g_new0(UserDefTwo, 1);
    ud2->string0 = g_strdup(text);

    /* tear down partial object */
    qapi_free_UserDefTwo(ud2);
}

typedef struct QmpReturnOrderly {
    QmpSession session;
    int returns;
} QmpReturnOrderly;

static void dispatch_return_orderly(QmpSession *session, QDict *resp)
{
    QmpReturnOrderly *o = container_of(session, QmpReturnOrderly, session);

    o->returns++;
}

static void test_qmp_return_orderly(void)
{
    QDict *dict = qdict_new();
    QmpReturnOrderly o = { { 0 }, };
    QmpReturn *r1, *r2, *r3;

    qmp_session_init(&o.session, &qmp_commands, NULL, dispatch_return_orderly);
    r1 = qmp_return_new(&o.session, NULL);
    qdict_put_str(dict, "exec-oob", "test");
    r2 = qmp_return_new(&o.session, QOBJECT(dict));
    r3 = qmp_return_new(&o.session, NULL);
    qmp_return(r3, NULL);
    g_assert_cmpint(o.returns, ==, 0);
    qmp_return(r2, NULL);
    g_assert_cmpint(o.returns, ==, 1);
    qmp_return(r1, NULL);
    g_assert_cmpint(o.returns, ==, 3);
    qmp_session_destroy(&o.session);
    qobject_unref(dict);
}

typedef struct QmpReturnAsync {
    QmpSession session;
    GMainLoop *loop;
} QmpReturnAsync;

static void dispatch_return_async(QmpSession *session, QDict *resp)
{
    QmpReturnAsync *a = container_of(session, QmpReturnAsync, session);

    g_main_loop_quit(a->loop);
    g_main_loop_unref(a->loop);
    a->loop = NULL;
}

static void test_qmp_return_async(void)
{
    QmpReturnAsync a = { { 0, } , };
    QDict *args = qdict_new();
    QDict *req = qdict_new();

    a.loop = g_main_loop_new(NULL, TRUE);
    qmp_session_init(&a.session, &qmp_commands,
                    NULL, dispatch_return_async);

    qdict_put_str(args, "filename", "test-filename");
    qdict_put_str(req, "execute", "cmd-async");
    qdict_put(req, "arguments", args);
    qmp_dispatch(&a.session, QOBJECT(req), false);
    g_assert(a.loop);

    g_main_loop_run(a.loop);
    g_assert(!a.loop);

    qmp_session_destroy(&a.session);
    qobject_unref(req);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/qmp/dispatch_cmd", test_dispatch_cmd);
    g_test_add_func("/qmp/dispatch_cmd_oob", test_dispatch_cmd_oob);
    g_test_add_func("/qmp/dispatch_cmd_failure", test_dispatch_cmd_failure);
    g_test_add_func("/qmp/dispatch_cmd_io", test_dispatch_cmd_io);
    g_test_add_func("/qmp/dispatch_cmd_success_response",
                    test_dispatch_cmd_success_response);
    g_test_add_func("/qmp/dealloc_types", test_dealloc_types);
    g_test_add_func("/qmp/dealloc_partial", test_dealloc_partial);
    g_test_add_func("/qmp/return_orderly", test_qmp_return_orderly);
    g_test_add_func("/qmp/return_async", test_qmp_return_async);

    test_qmp_init_marshal(&qmp_commands);
    g_test_run();

    return 0;
}
