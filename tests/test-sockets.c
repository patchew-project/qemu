/*
 * Test core sockets APIs
 *
 * Copyright 2015-2018 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */


#include "qemu/osdep.h"
#include "qemu/sockets.h"
#include "socket-helpers.h"
#include "qapi/error.h"
#include "monitor/monitor.h"


static int mon_fd = -1;
static const char *mon_fdname;

int monitor_get_fd(Monitor *mon, const char *fdname, Error **errp)
{
    g_assert(cur_mon);
    g_assert(mon == cur_mon);
    if (mon_fd == -1 || !g_str_equal(mon_fdname, fdname)) {
        error_setg(errp, "No fd named %s", fdname);
        return -1;
    }
    return dup(mon_fd);
}

/* Syms in libqemustub.a are discarded at .o file granularity.
 * To replace monitor_get_fd() we must ensure everything in
 * stubs/monitor.c is defined, to make sure monitor.o is discarded
 * otherwise we get duplicate syms at link time.
 */
Monitor *cur_mon;
void monitor_init(Chardev *chr, int flags) {}


static void test_socket_fd_pass_good(void)
{
    SocketAddress addr;
    int fd;

    cur_mon = g_malloc(1); /* Fake a monitor */
    mon_fdname = "myfd";
    mon_fd = qemu_socket(AF_INET, SOCK_STREAM, 0);
    g_assert_cmpint(mon_fd, >, STDERR_FILENO);

    addr.type = SOCKET_ADDRESS_TYPE_FD;
    addr.u.fd.str = g_strdup(mon_fdname);

    fd = socket_connect(&addr, &error_abort);
    g_assert_cmpint(fd, !=, -1);
    g_assert_cmpint(fd, !=, mon_fd);
    close(fd);

    fd = socket_listen(&addr, &error_abort);
    g_assert_cmpint(fd, !=, -1);
    g_assert_cmpint(fd, !=, mon_fd);
    close(fd);

    g_free(addr.u.fd.str);
    mon_fdname = NULL;
    close(mon_fd);
    mon_fd = -1;
    g_free(cur_mon);
    cur_mon = NULL;
}

static void test_socket_fd_pass_bad(void)
{
    SocketAddress addr;
    Error *err = NULL;
    int fd;

    cur_mon = g_malloc(1); /* Fake a monitor */
    mon_fdname = "myfd";
    mon_fd = dup(STDOUT_FILENO);
    g_assert_cmpint(mon_fd, >, STDERR_FILENO);

    addr.type = SOCKET_ADDRESS_TYPE_FD;
    addr.u.fd.str = g_strdup(mon_fdname);

    fd = socket_connect(&addr, &err);
    g_assert_cmpint(fd, ==, -1);
    error_free_or_abort(&err);

    fd = socket_listen(&addr, &err);
    g_assert_cmpint(fd, ==, -1);
    error_free_or_abort(&err);

    g_free(addr.u.fd.str);
    mon_fdname = NULL;
    close(mon_fd);
    mon_fd = -1;
    g_free(cur_mon);
    cur_mon = NULL;
}

int main(int argc, char **argv)
{
    bool has_ipv4, has_ipv6;

    module_call_init(MODULE_INIT_QOM);
    socket_init();

    g_test_init(&argc, &argv, NULL);

    /* We're creating actual IPv4/6 sockets, so we should
     * check if the host running tests actually supports
     * each protocol to avoid breaking tests on machines
     * with either IPv4 or IPv6 disabled.
     */
    if (socket_check_protocol_support(&has_ipv4, &has_ipv6) < 0) {
        return 1;
    }

    if (has_ipv4) {
        g_test_add_func("/socket/fd-pass/good",
                        test_socket_fd_pass_good);
        g_test_add_func("/socket/fd-pass/bad",
                        test_socket_fd_pass_bad);
    }

    return g_test_run();
}
