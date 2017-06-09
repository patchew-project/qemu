/*
 * Test parallel port listen configuration with
 * dynamic port allocation
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu-common.h"
#include "qemu/thread.h"
#include "qemu/sockets.h"
#include "qapi/error.h"

#define NAME_LEN 1024
#define PORT_LEN 16

struct thr_info {
    QemuThread thread;
    int to_port;
    int got_port;
    int eno;
    int fd;
    const char *errstr;
};

static char hostname[NAME_LEN + 1];
static char port[PORT_LEN + 1];

static void *listener_thread(void *arg)
{
    struct thr_info *thr = (struct thr_info *)arg;
    SocketAddress addr = {
        .type = SOCKET_ADDRESS_TYPE_INET,
        .u = {
            .inet = {
                .host = hostname,
                .port = port,
                .ipv4 = true,
                .has_to = true,
                .to = thr->to_port,
            },
        },
    };
    Error *err = NULL;
    int fd;

    fd = socket_listen(&addr, &err);
    if (fd < 0) {
        thr->eno = errno;
        thr->errstr = error_get_pretty(err);
    } else {
        struct sockaddr_in a;
        socklen_t a_len = sizeof(a);
        g_assert_cmpint(getsockname(fd, (struct sockaddr *)&a, &a_len), ==, 0);
        thr->got_port = ntohs(a.sin_port);
        thr->fd = fd;
    }
    return arg;
}


static void listen_compete_nthr(bool threaded, int nthreads,
                                int start_port, int max_offset)
{
    int i;
    int failed_listens = 0;
    size_t alloc_sz = sizeof(struct thr_info) * nthreads;
    struct thr_info *thr = g_malloc(alloc_sz);
    int used[max_offset + 1];
    memset(used, 0, sizeof(used));
    g_assert_nonnull(thr);
    g_assert_cmpint(gethostname(hostname, NAME_LEN), == , 0);
    snprintf(port, PORT_LEN, "%d", start_port);
    memset(thr, 0, alloc_sz);

    for (i = 0; i < nthreads; i++) {
        thr[i].to_port = start_port + max_offset;
        if (threaded) {
            qemu_thread_create(&thr[i].thread, "listener",
                               listener_thread, &thr[i],
                               QEMU_THREAD_JOINABLE);
        } else {
            listener_thread(&thr[i]);
        }
    }

    if (threaded) {
        for (i = 0; i < nthreads; i++) {
            qemu_thread_join(&thr[i].thread);
        }
    }
    for (i = 0; i < nthreads; i++) {
        if (thr[i].got_port) {
            closesocket(thr[i].fd);
        }
    }

    for (i = 0; i < nthreads; i++) {
        if (thr[i].eno != 0) {
            printf("** Failed to assign a port to thread %d (errno = %d)\n", i, thr[i].eno);
            /* This is what we are interested in capturing -
             * catch if something unexpected happens:
             */
            g_assert(strstr(thr[i].errstr, "Failed to listen on socket") != NULL);
            failed_listens++;
        } else {
            int assigned_port = thr[i].got_port;
            g_assert_cmpint(assigned_port, <= , thr[i].to_port);
            g_assert_cmpint(used[assigned_port - start_port], == , 0);
        }
    }
    g_assert_cmpint(failed_listens, ==, 0);
    free(thr);
}


static void listen_compete(void)
{
    listen_compete_nthr(true, 200, 5920, 300);
}

static void listen_serial(void)
{
    listen_compete_nthr(false, 200, 6300, 300);
}


int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/socket/listen-serial", listen_serial);
    g_test_add_func("/socket/listen-compete", listen_compete);

    return g_test_run();
}
