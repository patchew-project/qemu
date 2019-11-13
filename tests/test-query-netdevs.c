/*
 * QTest testcase for the query-netdevs
 *
 * Copyright Yandex N.V., 2019
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"

#include "libqtest.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qlist.h"

/*
 * Events can get in the way of responses we are actually waiting for.
 */
GCC_FMT_ATTR(2, 3)
static QObject *wait_command(QTestState *who, const char *command, ...)
{
    va_list ap;
    QDict *response;
    QObject *result;

    va_start(ap, command);
    qtest_qmp_vsend(who, command, ap);
    va_end(ap);

    response = qtest_qmp_receive(who);

    result = qdict_get(response, "return");
    g_assert(result);
    qobject_ref(result);
    qobject_unref(response);

    return result;
}

static void qmp_query_netdevs_no_error(QTestState *qts,
                                       size_t netdevs_count)
{
    QObject *resp;
    QList *netdevs;

    resp = wait_command(qts, "{'execute': 'x-query-netdevs'}");

    netdevs = qobject_to(QList, resp);
    g_assert(netdevs);
    g_assert(qlist_size(netdevs) == netdevs_count);

    qobject_unref(resp);
}

static void test_query_netdevs(void)
{
    const char *arch = qtest_get_arch();
    size_t correction = 0;
    QObject *resp;
    QTestState *state;

    if (strcmp(arch, "arm") == 0 ||
        strcmp(arch, "aarch64") == 0 ||
        strcmp(arch, "tricore") == 0) {
        g_test_skip("Not supported without machine type");
        return;
    }

    /* Archs with default not unpluggable netdev */
    if (strcmp(arch, "cris") == 0 ||
        strcmp(arch, "microblaze") == 0 ||
        strcmp(arch, "microblazeel") == 0 ||
        strcmp(arch, "sparc") == 0) {
        correction = 1;
    }

    state = qtest_init(
        "-nodefaults "
        "-netdev user,id=slirp0");
    g_assert(state);

    qmp_query_netdevs_no_error(state, 1 + correction);

    resp = wait_command(state,
        "{'execute': 'netdev_add', 'arguments': {"
        " 'id': 'slirp1',"
        " 'type': 'user'}}");
    qobject_unref(resp);

    qmp_query_netdevs_no_error(state, 2 + correction);

    resp = wait_command(state,
        "{'execute': 'netdev_del', 'arguments': {"
        " 'id': 'slirp1'}}");
    qobject_unref(resp);

    qmp_query_netdevs_no_error(state, 1 + correction);

    qtest_quit(state);
}

int main(int argc, char **argv)
{
    int ret = 0;
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/net/qapi/query_netdevs",
        test_query_netdevs);

    ret = g_test_run();

    return ret;
}
