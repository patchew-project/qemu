/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"

#include "net/net.h"
#include "clients.h"
#include "monitor/monitor.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/option.h"
#include "qemu/sockets.h"
#include "qemu/iov.h"
#include "qemu/main-loop.h"
#include "qemu/cutils.h"

typedef struct NetSocketNGState {
    NetClientState nc;
    int listen_fd;
    int fd;
    SocketReadState rs;
    unsigned int send_index;      /* number of bytes sent (only SOCK_STREAM) */
                     /* contains destination iff connectionless (SOCK_DGRAM) */
    struct sockaddr *dgram_dst;
    IOHandler *send_fn;           /* differs between SOCK_STREAM/SOCK_DGRAM */
    bool read_poll;               /* waiting to receive data? */
    bool write_poll;              /* waiting to transmit data? */
} NetSocketNGState;

static void net_socket_accept(void *opaque);
static void net_socket_writable(void *opaque);

static void net_socket_update_fd_handler(NetSocketNGState *s)
{
    qemu_set_fd_handler(s->fd,
                        s->read_poll ? s->send_fn : NULL,
                        s->write_poll ? net_socket_writable : NULL,
                        s);
}

static void net_socket_read_poll(NetSocketNGState *s, bool enable)
{
    s->read_poll = enable;
    net_socket_update_fd_handler(s);
}

static void net_socket_write_poll(NetSocketNGState *s, bool enable)
{
    s->write_poll = enable;
    net_socket_update_fd_handler(s);
}

static void net_socket_writable(void *opaque)
{
    NetSocketNGState *s = opaque;

    net_socket_write_poll(s, false);

    qemu_flush_queued_packets(&s->nc);
}

static ssize_t net_socket_receive(NetClientState *nc, const uint8_t *buf,
                                  size_t size)
{
    NetSocketNGState *s = DO_UPCAST(NetSocketNGState, nc, nc);
    uint32_t len = htonl(size);
    struct iovec iov[] = {
        {
            .iov_base = &len,
            .iov_len  = sizeof(len),
        }, {
            .iov_base = (void *)buf,
            .iov_len  = size,
        },
    };
    size_t remaining;
    ssize_t ret;

    remaining = iov_size(iov, 2) - s->send_index;
    ret = iov_send(s->fd, iov, 2, s->send_index, remaining);

    if (ret == -1 && errno == EAGAIN) {
        ret = 0; /* handled further down */
    }
    if (ret == -1) {
        s->send_index = 0;
        return -errno;
    }
    if (ret < (ssize_t)remaining) {
        s->send_index += ret;
        net_socket_write_poll(s, true);
        return 0;
    }
    s->send_index = 0;
    return size;
}

static ssize_t net_socket_receive_dgram(NetClientState *nc,
                                        const uint8_t *buf, size_t size)
{
    NetSocketNGState *s = DO_UPCAST(NetSocketNGState, nc, nc);
    ssize_t ret;

    do {
        if (s->dgram_dst) {
            ret = sendto(s->fd, buf, size, 0, s->dgram_dst,
                         sizeof(struct sockaddr_in));
        } else {
            ret = send(s->fd, buf, size, 0);
        }
    } while (ret == -1 && errno == EINTR);

    if (ret == -1 && errno == EAGAIN) {
        net_socket_write_poll(s, true);
        return 0;
    }
    return ret;
}

static void net_socket_send_completed(NetClientState *nc, ssize_t len)
{
    NetSocketNGState *s = DO_UPCAST(NetSocketNGState, nc, nc);

    if (!s->read_poll) {
        net_socket_read_poll(s, true);
    }
}

static void net_socket_rs_finalize(SocketReadState *rs)
{
    NetSocketNGState *s = container_of(rs, NetSocketNGState, rs);

    if (qemu_send_packet_async(&s->nc, rs->buf,
                               rs->packet_len,
                               net_socket_send_completed) == 0) {
        net_socket_read_poll(s, false);
    }
}

static void net_socket_send(void *opaque)
{
    NetSocketNGState *s = opaque;
    int size;
    int ret;
    uint8_t buf1[NET_BUFSIZE];
    const uint8_t *buf;

    size = recv(s->fd, buf1, sizeof(buf1), 0);
    if (size < 0) {
        if (errno != EWOULDBLOCK) {
            goto eoc;
        }
    } else if (size == 0) {
        /* end of connection */
    eoc:
        net_socket_read_poll(s, false);
        net_socket_write_poll(s, false);
        if (s->listen_fd != -1) {
            qemu_set_fd_handler(s->listen_fd, net_socket_accept, NULL, s);
        }
        closesocket(s->fd);

        s->fd = -1;
        net_socket_rs_init(&s->rs, net_socket_rs_finalize, false);
        s->nc.link_down = true;
        memset(s->nc.info_str, 0, sizeof(s->nc.info_str));

        return;
    }
    buf = buf1;

    ret = net_fill_rstate(&s->rs, buf, size);

    if (ret == -1) {
        goto eoc;
    }
}

static void net_socket_send_dgram(void *opaque)
{
    NetSocketNGState *s = opaque;
    int size;

    size = recv(s->fd, s->rs.buf, sizeof(s->rs.buf), 0);
    if (size < 0) {
        return;
    }
    if (size == 0) {
        /* end of connection */
        net_socket_read_poll(s, false);
        net_socket_write_poll(s, false);
        return;
    }
    if (qemu_send_packet_async(&s->nc, s->rs.buf, size,
                               net_socket_send_completed) == 0) {
        net_socket_read_poll(s, false);
    }
}

static int net_socket_mcast_create(struct sockaddr_in *mcastaddr,
                                   struct in_addr *localaddr,
                                   Error **errp)
{
    struct ip_mreq imr;
    int fd;
    int val, ret;
#ifdef __OpenBSD__
    unsigned char loop;
#else
    int loop;
#endif

    if (!IN_MULTICAST(ntohl(mcastaddr->sin_addr.s_addr))) {
        error_setg(errp, "specified mcastaddr %s (0x%08x) "
                   "does not contain a multicast address",
                   inet_ntoa(mcastaddr->sin_addr),
                   (int)ntohl(mcastaddr->sin_addr.s_addr));
        return -1;
    }

    fd = qemu_socket(PF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        error_setg_errno(errp, errno, "can't create datagram socket");
        return -1;
    }

    /*
     * Allow multiple sockets to bind the same multicast ip and port by setting
     * SO_REUSEADDR. This is the only situation where SO_REUSEADDR should be set
     * on windows. Use socket_set_fast_reuse otherwise as it sets SO_REUSEADDR
     * only on posix systems.
     */
    val = 1;
    ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
    if (ret < 0) {
        error_setg_errno(errp, errno,
                         "can't set socket option SO_REUSEADDR");
        goto fail;
    }

    ret = bind(fd, (struct sockaddr *)mcastaddr, sizeof(*mcastaddr));
    if (ret < 0) {
        error_setg_errno(errp, errno, "can't bind ip=%s to socket",
                         inet_ntoa(mcastaddr->sin_addr));
        goto fail;
    }

    /* Add host to multicast group */
    imr.imr_multiaddr = mcastaddr->sin_addr;
    if (localaddr) {
        imr.imr_interface = *localaddr;
    } else {
        imr.imr_interface.s_addr = htonl(INADDR_ANY);
    }

    ret = setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                     &imr, sizeof(struct ip_mreq));
    if (ret < 0) {
        error_setg_errno(errp, errno,
                         "can't add socket to multicast group %s",
                         inet_ntoa(imr.imr_multiaddr));
        goto fail;
    }

    /* Force mcast msgs to loopback (eg. several QEMUs in same host */
    loop = 1;
    ret = setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP,
                     &loop, sizeof(loop));
    if (ret < 0) {
        error_setg_errno(errp, errno,
                         "can't force multicast message to loopback");
        goto fail;
    }

    /* If a bind address is given, only send packets from that address */
    if (localaddr != NULL) {
        ret = setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF,
                         localaddr, sizeof(*localaddr));
        if (ret < 0) {
            error_setg_errno(errp, errno,
                             "can't set the default network send interface");
            goto fail;
        }
    }

    qemu_socket_set_nonblock(fd);
    return fd;
fail:
    if (fd >= 0) {
        closesocket(fd);
    }
    return -1;
}

static void net_socket_cleanup(NetClientState *nc)
{
    NetSocketNGState *s = DO_UPCAST(NetSocketNGState, nc, nc);
    if (s->fd != -1) {
        net_socket_read_poll(s, false);
        net_socket_write_poll(s, false);
        close(s->fd);
        s->fd = -1;
    }
    if (s->listen_fd != -1) {
        qemu_set_fd_handler(s->listen_fd, NULL, NULL, NULL);
        closesocket(s->listen_fd);
        s->listen_fd = -1;
    }
    g_free(s->dgram_dst);
    s->dgram_dst = NULL;
}

static NetClientInfo net_dgram_socket_info = {
    .type = NET_CLIENT_DRIVER_SOCKET_NG,
    .size = sizeof(NetSocketNGState),
    .receive = net_socket_receive_dgram,
    .cleanup = net_socket_cleanup,
};

static NetSocketNGState *net_socket_fd_init_dgram(NetClientState *peer,
                                                const char *model,
                                                const char *name,
                                                int fd, int is_fd,
                                                SocketAddress *mcast,
                                                Error **errp)
{
    struct sockaddr_in *saddr = NULL;
    int newfd;
    NetClientState *nc;
    NetSocketNGState *s;
    SocketAddress *sa;
    SocketAddressType sa_type;

    sa = socket_local_address(fd, errp);
    if (!sa) {
        return NULL;
    }
    sa_type = sa->type;
    qapi_free_SocketAddress(sa);

    /*
     * fd passed: multicast: "learn" dgram_dst address from bound address and
     * save it. Because this may be "shared" socket from a "master" process,
     * datagrams would be recv() by ONLY ONE process: we must "clone" this
     * dgram socket --jjo
     */

    if (is_fd && mcast != NULL) {
            saddr = g_new(struct sockaddr_in, 1);

            if (convert_host_port(saddr, mcast->u.inet.host, mcast->u.inet.port,
                                  errp) < 0) {
                goto err;
            }
            /* must be bound */
            if (saddr->sin_addr.s_addr == 0) {
                error_setg(errp, "can't setup multicast destination address");
                goto err;
            }
            /* clone dgram socket */
            newfd = net_socket_mcast_create(saddr, NULL, errp);
            if (newfd < 0) {
                goto err;
            }
            /* clone newfd to fd, close newfd */
            dup2(newfd, fd);
            close(newfd);
    }

    nc = qemu_new_net_client(&net_dgram_socket_info, peer, model, name);

    s = DO_UPCAST(NetSocketNGState, nc, nc);

    s->fd = fd;
    s->listen_fd = -1;
    s->send_fn = net_socket_send_dgram;
    net_socket_rs_init(&s->rs, net_socket_rs_finalize, false);
    net_socket_read_poll(s, true);

    /* mcast: save bound address as dst */
    if (saddr) {
        g_assert(s->dgram_dst == NULL);
        s->dgram_dst = (struct sockaddr *)saddr;
        snprintf(nc->info_str, sizeof(nc->info_str),
                 "socket-ng: fd=%d (cloned mcast=%s:%d)",
                 fd, inet_ntoa(saddr->sin_addr), ntohs(saddr->sin_port));
    } else {
        snprintf(nc->info_str, sizeof(nc->info_str),
                 "socket-ng: fd=%d %s", fd, SocketAddressType_str(sa_type));
    }

    return s;

err:
    g_free(saddr);
    closesocket(fd);
    return NULL;
}

static void net_socket_connect(void *opaque)
{
    NetSocketNGState *s = opaque;
    s->send_fn = net_socket_send;
    net_socket_read_poll(s, true);
}

static NetClientInfo net_socket_info = {
    .type = NET_CLIENT_DRIVER_SOCKET_NG,
    .size = sizeof(NetSocketNGState),
    .receive = net_socket_receive,
    .cleanup = net_socket_cleanup,
};

static NetSocketNGState *net_socket_fd_init_stream(NetClientState *peer,
                                                 const char *model,
                                                 const char *name,
                                                 int fd, int is_connected)
{
    NetClientState *nc;
    NetSocketNGState *s;

    nc = qemu_new_net_client(&net_socket_info, peer, model, name);

    snprintf(nc->info_str, sizeof(nc->info_str), "socket-ng: fd=%d", fd);

    s = DO_UPCAST(NetSocketNGState, nc, nc);

    s->fd = fd;
    s->listen_fd = -1;
    net_socket_rs_init(&s->rs, net_socket_rs_finalize, false);

    /* Disable Nagle algorithm on TCP sockets to reduce latency */
    socket_set_nodelay(fd);

    if (is_connected) {
        net_socket_connect(s);
    } else {
        qemu_set_fd_handler(s->fd, NULL, net_socket_connect, s);
    }
    return s;
}

static void net_socket_accept(void *opaque)
{
    NetSocketNGState *s = opaque;
    struct sockaddr_storage saddr;
    socklen_t len;
    int fd;

    for (;;) {
        len = sizeof(saddr);
        fd = qemu_accept(s->listen_fd, (struct sockaddr *)&saddr, &len);
        if (fd < 0 && errno != EINTR) {
            return;
        } else if (fd >= 0) {
            qemu_set_fd_handler(s->listen_fd, NULL, NULL, NULL);
            break;
        }
    }

    s->fd = fd;
    s->nc.link_down = false;
    net_socket_connect(s);
    switch (saddr.ss_family) {
    case AF_INET: {
        struct sockaddr_in *saddr_in = (struct sockaddr_in *)&saddr;

        snprintf(s->nc.info_str, sizeof(s->nc.info_str),
                 "socket-ng: connection from %s:%d",
                 inet_ntoa(saddr_in->sin_addr), ntohs(saddr_in->sin_port));
        break;
    }
    case AF_UNIX: {
        struct sockaddr_un saddr_un;

        len = sizeof(saddr_un);
        getsockname(s->listen_fd, (struct sockaddr *)&saddr_un, &len);
        snprintf(s->nc.info_str, sizeof(s->nc.info_str),
                 "socket-ng: connect from %s", saddr_un.sun_path);
        break;
    }
    default:
        g_assert_not_reached();
    }
}

static int net_socketng_listen_init(NetClientState *peer,
                                    const char *model,
                                    const char *name,
                                    SocketAddress *addr,
                                    Error **errp)
{
    NetClientState *nc;
    NetSocketNGState *s;
    int fd, ret;

    switch (addr->type) {
    case SOCKET_ADDRESS_TYPE_INET: {
        struct sockaddr_in saddr_in;

        if (convert_host_port(&saddr_in, addr->u.inet.host, addr->u.inet.port,
                              errp) < 0) {
            return -1;
        }

        fd = qemu_socket(PF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            error_setg_errno(errp, errno, "can't create stream socket");
            return -1;
        }
        qemu_socket_set_nonblock(fd);

        socket_set_fast_reuse(fd);

        ret = bind(fd, (struct sockaddr *)&saddr_in, sizeof(saddr_in));
        if (ret < 0) {
            error_setg_errno(errp, errno, "can't bind ip=%s to socket",
                             inet_ntoa(saddr_in.sin_addr));
            closesocket(fd);
            return -1;
        }
        break;
    }
    case SOCKET_ADDRESS_TYPE_UNIX: {
        struct sockaddr_un saddr_un;

        ret = unlink(addr->u.q_unix.path);
        if (ret < 0 && errno != ENOENT) {
            error_setg_errno(errp, errno, "failed to unlink socket %s",
                             addr->u.q_unix.path);
            return -1;
        }

        saddr_un.sun_family = PF_UNIX;
        ret = snprintf(saddr_un.sun_path, sizeof(saddr_un.sun_path), "%s",
                       addr->u.q_unix.path);
        if (ret < 0 || ret >= sizeof(saddr_un.sun_path)) {
            error_setg(errp, "UNIX socket path '%s' is too long",
                       addr->u.q_unix.path);
            error_append_hint(errp, "Path must be less than %zu bytes\n",
                              sizeof(saddr_un.sun_path));
        }

        fd = qemu_socket(PF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            error_setg_errno(errp, errno, "can't create stream socket");
            return -1;
        }
        qemu_socket_set_nonblock(fd);

        ret = bind(fd, (struct sockaddr *)&saddr_un, sizeof(saddr_un));
        if (ret < 0) {
            error_setg_errno(errp, errno, "can't create socket with path: %s",
                             saddr_un.sun_path);
            closesocket(fd);
            return -1;
        }
        break;
    }
    case SOCKET_ADDRESS_TYPE_FD:
        fd = monitor_fd_param(monitor_cur(), addr->u.fd.str, errp);
        if (fd == -1) {
            return -1;
        }
        ret = qemu_socket_try_set_nonblock(fd);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "%s: Can't use file descriptor %d",
                             name, fd);
            return -1;
        }
        break;
    default:
        g_assert_not_reached();
    }

    ret = listen(fd, 0);
    if (ret < 0) {
        error_setg_errno(errp, errno, "can't listen on socket");
        closesocket(fd);
        return -1;
    }

    nc = qemu_new_net_client(&net_socket_info, peer, model, name);
    s = DO_UPCAST(NetSocketNGState, nc, nc);
    s->fd = -1;
    s->listen_fd = fd;
    s->nc.link_down = true;
    net_socket_rs_init(&s->rs, net_socket_rs_finalize, false);

    qemu_set_fd_handler(s->listen_fd, net_socket_accept, NULL, s);
    return 0;
}

static int net_socketng_connect_init(NetClientState *peer,
                                   const char *model,
                                   const char *name,
                                   SocketAddress *addr,
                                   Error **errp)
{
    NetSocketNGState *s;
    int fd, connected, ret;
    gchar *info_str;

    switch (addr->type) {
    case SOCKET_ADDRESS_TYPE_INET: {
        struct sockaddr_in saddr_in;

        if (convert_host_port(&saddr_in, addr->u.inet.host, addr->u.inet.port,
                              errp) < 0) {
            return -1;
        }

        fd = qemu_socket(PF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            error_setg_errno(errp, errno, "can't create stream socket");
            return -1;
        }
        qemu_socket_set_nonblock(fd);

        connected = 0;
        for (;;) {
            ret = connect(fd, (struct sockaddr *)&saddr_in, sizeof(saddr_in));
            if (ret < 0) {
                if (errno == EINTR || errno == EWOULDBLOCK) {
                    /* continue */
                } else if (errno == EINPROGRESS ||
                           errno == EALREADY ||
                           errno == EINVAL) {
                    break;
                } else {
                    error_setg_errno(errp, errno, "can't connect socket");
                    closesocket(fd);
                    return -1;
                }
            } else {
                connected = 1;
                break;
            }
        }
        info_str = g_strdup_printf("socket-ng: connect to %s:%d",
                                   inet_ntoa(saddr_in.sin_addr),
                                   ntohs(saddr_in.sin_port));
        break;
    }
    case SOCKET_ADDRESS_TYPE_UNIX: {
        struct sockaddr_un saddr_un;

        saddr_un.sun_family = PF_UNIX;
        ret = snprintf(saddr_un.sun_path, sizeof(saddr_un.sun_path), "%s",
                       addr->u.q_unix.path);
        if (ret < 0 || ret >= sizeof(saddr_un.sun_path)) {
            error_setg(errp, "UNIX socket path '%s' is too long",
                       addr->u.q_unix.path);
            error_append_hint(errp, "Path must be less than %zu bytes\n",
                              sizeof(saddr_un.sun_path));
        }

        fd = qemu_socket(PF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            error_setg_errno(errp, errno, "can't create stream socket");
            return -1;
        }
        qemu_socket_set_nonblock(fd);

        connected = 0;
        for (;;) {
            ret = connect(fd, (struct sockaddr *)&saddr_un, sizeof(saddr_un));
            if (ret < 0) {
                if (errno == EINTR || errno == EWOULDBLOCK) {
                    /* continue */
                } else if (errno == EAGAIN ||
                           errno == EALREADY ||
                           errno == EINVAL) {
                    break;
                } else {
                    error_setg_errno(errp, errno, "can't connect socket");
                    closesocket(fd);
                    return -1;
                }
            } else {
                connected = 1;
                break;
            }
        }
        info_str = g_strdup_printf("socket-ng: connect to %s",
                                   saddr_un.sun_path);
        break;
    }
    case SOCKET_ADDRESS_TYPE_FD:
        fd = monitor_fd_param(monitor_cur(), addr->u.fd.str, errp);
        if (fd == -1) {
            return -1;
        }
        ret = qemu_socket_try_set_nonblock(fd);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "%s: Can't use file descriptor %d",
                             name, fd);
            return -1;
        }
        connected = 1;
        info_str = g_strdup_printf("socket-ng: connect to fd %d", fd);
        break;
    default:
        error_setg(errp, "only support inet, unix or fd type");
        return -1;
    }

    s = net_socket_fd_init_stream(peer, model, name, fd, connected);

    pstrcpy(s->nc.info_str, sizeof(s->nc.info_str), info_str);
    g_free(info_str);

    return 0;
}

static int net_socketng_mcast_init(NetClientState *peer,
                                 const char *model,
                                 const char *name,
                                 SocketAddress *remote,
                                 SocketAddress *local,
                                 Error **errp)
{
    NetSocketNGState *s;
    int fd, ret;
    struct sockaddr_in *saddr;

    if (remote->type != SOCKET_ADDRESS_TYPE_INET) {
        error_setg(errp, "multicast only support inet type");
        return -1;
    }

    saddr = g_new(struct sockaddr_in, 1);
    if (convert_host_port(saddr, remote->u.inet.host, remote->u.inet.port,
                          errp) < 0) {
        g_free(saddr);
        return -1;
    }

    if (!local) {
        fd = net_socket_mcast_create(saddr, NULL, errp);
        if (fd < 0) {
            g_free(saddr);
            return -1;
        }
    } else {
        switch (local->type) {
        case SOCKET_ADDRESS_TYPE_INET: {
            struct in_addr localaddr;

            if (inet_aton(local->u.inet.host, &localaddr) == 0) {
                g_free(saddr);
                error_setg(errp, "localaddr '%s' is not a valid IPv4 address",
                           local->u.inet.host);
                return -1;
            }

            fd = net_socket_mcast_create(saddr, &localaddr, errp);
            if (fd < 0) {
                g_free(saddr);
                return -1;
            }
            break;
        }
        case SOCKET_ADDRESS_TYPE_FD:
            fd = monitor_fd_param(monitor_cur(), local->u.fd.str, errp);
            if (fd == -1) {
                g_free(saddr);
                return -1;
            }
            ret = qemu_socket_try_set_nonblock(fd);
            if (ret < 0) {
                g_free(saddr);
                error_setg_errno(errp, -ret, "%s: Can't use file descriptor %d",
                                 name, fd);
                return -1;
            }
            break;
        default:
            g_free(saddr);
            error_setg(errp, "only support inet or fd type for local");
            return -1;
        }
    }

    s = net_socket_fd_init_dgram(peer, model, name, fd,
                                 local->type == SOCKET_ADDRESS_TYPE_FD,
                                 remote, errp);
    if (!s) {
        g_free(saddr);
        return -1;
    }

    g_assert(s->dgram_dst == NULL);
    s->dgram_dst = (struct sockaddr *)saddr;

    snprintf(s->nc.info_str, sizeof(s->nc.info_str),
             "socket-ng: mcast=%s:%d",
             inet_ntoa(saddr->sin_addr), ntohs(saddr->sin_port));

    return 0;

}

static int net_socketng_udp_init(NetClientState *peer,
                                 const char *model,
                                 const char *name,
                                 SocketAddress *remote,
                                 SocketAddress *local,
                                 Error **errp)
{
    NetSocketNGState *s;
    int fd, ret;
    gchar *info_str;
    struct sockaddr *dgram_dst;

    if (remote) {
        if (local->type == SOCKET_ADDRESS_TYPE_FD) {
            error_setg(errp, "don't set remote with local.fd");
            return -1;
        }
        if (remote->type != local->type) {
            error_setg(errp, "remote and local types must be the same");
            return -1;
        }
    } else {
        if (local->type != SOCKET_ADDRESS_TYPE_FD) {
            error_setg(errp, "type=inet and mode=unicast require "
                             "remote parameter");
            return -1;
        }
    }

    switch (local->type) {
    case SOCKET_ADDRESS_TYPE_INET: {
        struct sockaddr_in laddr_in, raddr_in;

        if (convert_host_port(&laddr_in, local->u.inet.host, local->u.inet.port,
                              errp) < 0) {
            return -1;
        }

        if (convert_host_port(&raddr_in, remote->u.inet.host,
                              remote->u.inet.port, errp) < 0) {
            return -1;
        }

        fd = qemu_socket(PF_INET, SOCK_DGRAM, 0);
        if (fd < 0) {
            error_setg_errno(errp, errno, "can't create datagram socket");
            return -1;
        }

        ret = socket_set_fast_reuse(fd);
        if (ret < 0) {
            error_setg_errno(errp, errno,
                             "can't set socket option SO_REUSEADDR");
            closesocket(fd);
            return -1;
        }
        ret = bind(fd, (struct sockaddr *)&laddr_in, sizeof(laddr_in));
        if (ret < 0) {
            error_setg_errno(errp, errno, "can't bind ip=%s to socket",
                             inet_ntoa(laddr_in.sin_addr));
            closesocket(fd);
            return -1;
        }
        qemu_socket_set_nonblock(fd);

        dgram_dst = g_malloc(sizeof(raddr_in));
        memcpy(dgram_dst, &raddr_in, sizeof(raddr_in));

        info_str = g_strdup_printf("socket-ng: udp=%s:%d/%s:%d",
                        inet_ntoa(laddr_in.sin_addr), ntohs(laddr_in.sin_port),
                        inet_ntoa(raddr_in.sin_addr), ntohs(raddr_in.sin_port));

        break;
    }
    case SOCKET_ADDRESS_TYPE_FD:
        fd = monitor_fd_param(monitor_cur(), local->u.fd.str, errp);
        if (fd == -1) {
            return -1;
        }
        ret = qemu_socket_try_set_nonblock(fd);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "%s: Can't use file descriptor %d",
                             name, fd);
            return -1;
        }
        break;
    default:
        error_setg(errp, "only support inet or fd type for local");
        return -1;
    }

    s = net_socket_fd_init_dgram(peer, model, name, fd, 0, NULL, errp);
    if (!s) {
        return -1;
    }

    if (remote) {
        g_assert(s->dgram_dst == NULL);
        s->dgram_dst = dgram_dst;

        pstrcpy(s->nc.info_str, sizeof(s->nc.info_str), info_str);
        g_free(info_str);
    }
    return 0;
};

static int net_socketng_dgram_init(NetClientState *peer,
                                 const char *model,
                                 const char *name,
                                 SocketAddress *remote,
                                 SocketAddress *local,
                                 Error **errp)
{
    /* detect multicast address */
    if (remote && remote->type == SOCKET_ADDRESS_TYPE_INET) {
        struct sockaddr_in mcastaddr;

        if (convert_host_port(&mcastaddr, remote->u.inet.host,
                              remote->u.inet.port, errp) < 0) {
            return -1;
        }

        if (IN_MULTICAST(ntohl(mcastaddr.sin_addr.s_addr))) {
            return net_socketng_mcast_init(peer, model, name, remote, local,
                                           errp);
        }
    }
    /* unicast address */
    if (!local) {
        error_setg(errp, "mode=dgram requires local= parameter");
        return -1;
    }
    return net_socketng_udp_init(peer, model, name, remote, local, errp);
}

int net_init_socket_ng(const Netdev *netdev, const char *name,
                    NetClientState *peer, Error **errp)
{
    const NetdevSocketNGOptions *sock;

    assert(netdev->type == NET_CLIENT_DRIVER_SOCKET_NG);
    sock = &netdev->u.socket_ng;

    switch (sock->mode) {
    case NETDEV_SOCKETNG_MODE_DGRAM:
        return net_socketng_dgram_init(peer, "socket", name, sock->remote,
                                       sock->local, errp);
    case NETDEV_SOCKETNG_MODE_SERVER:
        if (!sock->has_addr) {
            error_setg(errp, "mode=server requires addr parameter");
            return -1;
        }
        if (sock->has_remote || sock->has_local) {
            error_setg(errp,
                 "local and remote parameters cannot be used with mode=server");
            return -1;
        }
        return net_socketng_listen_init(peer, "socket", name, sock->addr, errp);
    case NETDEV_SOCKETNG_MODE_CLIENT:
        if (!sock->has_addr) {
            error_setg(errp, "mode=client requires addr parameter");
            return -1;
        }
        if (sock->has_remote || sock->has_local) {
            error_setg(errp,
                 "local and remote parameters cannot be used with mode=client");
            return -1;
        }
        return net_socketng_connect_init(peer, "socket", name, sock->addr,
                                         errp);
    default:
        g_assert_not_reached();
    }

    g_assert_not_reached();
}
