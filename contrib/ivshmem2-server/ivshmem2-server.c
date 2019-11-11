/*
 * Copyright 6WIND S.A., 2014
 * Copyright (c) Siemens AG, 2019
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "qemu/sockets.h"
#include "qemu/atomic.h"

#include <sys/socket.h>
#include <sys/un.h>

#include "ivshmem2-server.h"

/* log a message on stdout if verbose=1 */
#define IVSHMEM_SERVER_DEBUG(server, fmt, ...) do { \
        if ((server)->args.verbose) {         \
            printf(fmt, ## __VA_ARGS__); \
        }                                \
    } while (0)

/** maximum size of a huge page, used by ivshmem_server_ftruncate() */
#define IVSHMEM_SERVER_MAX_HUGEPAGE_SIZE (1024 * 1024 * 1024)

/** default listen backlog (number of sockets not accepted) */
#define IVSHMEM_SERVER_LISTEN_BACKLOG 10

/* send message to a client unix socket */
static int ivshmem_server_send_msg(int sock_fd, void *payload, int len, int fd)
{
    int ret;
    struct msghdr msg;
    struct iovec iov[1];
    union {
        struct cmsghdr cmsg;
        char control[CMSG_SPACE(sizeof(int))];
    } msg_control;
    struct cmsghdr *cmsg;

    iov[0].iov_base = payload;
    iov[0].iov_len = len;

    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    /* if fd is specified, add it in a cmsg */
    if (fd >= 0) {
        memset(&msg_control, 0, sizeof(msg_control));
        msg.msg_control = &msg_control;
        msg.msg_controllen = sizeof(msg_control);
        cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(cmsg), &fd, sizeof(fd));
    }

    ret = sendmsg(sock_fd, &msg, 0);
    if (ret <= 0) {
        return -1;
    }

    return 0;
}

static int ivshmem_server_send_event_fd(int sock_fd, int peer_id, int vector,
                                        int fd)
{
    IvshmemEventFd msg = {
        .header = {
            .type = GUINT32_TO_LE(IVSHMEM_MSG_EVENT_FD),
            .len = GUINT32_TO_LE(sizeof(msg)),
        },
        .id = GUINT32_TO_LE(peer_id),
        .vector = GUINT32_TO_LE(vector),
    };

    return ivshmem_server_send_msg(sock_fd, &msg, sizeof(msg), fd);
}

/* free a peer when the server advertises a disconnection or when the
 * server is freed */
static void
ivshmem_server_free_peer(IvshmemServer *server, IvshmemServerPeer *peer)
{
    unsigned vector;
    IvshmemServerPeer *other_peer;
    IvshmemPeerGone msg = {
        .header = {
            .type = GUINT32_TO_LE(IVSHMEM_MSG_PEER_GONE),
            .len = GUINT32_TO_LE(sizeof(msg)),
        },
        .id = GUINT32_TO_LE(peer->id),
    };

    IVSHMEM_SERVER_DEBUG(server, "free peer %" PRId64 "\n", peer->id);
    close(peer->sock_fd);
    QTAILQ_REMOVE(&server->peer_list, peer, next);

    server->state_table[peer->id] = 0;
    smp_mb();

    /* advertise the deletion to other peers */
    QTAILQ_FOREACH(other_peer, &server->peer_list, next) {
        ivshmem_server_send_msg(other_peer->sock_fd, &msg, sizeof(msg), -1);
    }

    for (vector = 0; vector < peer->vectors_count; vector++) {
        event_notifier_cleanup(&peer->vectors[vector]);
    }

    g_free(peer);
}

/* send the peer id and the shm_fd just after a new client connection */
static int
ivshmem_server_send_initial_info(IvshmemServer *server, IvshmemServerPeer *peer)
{
    IvshmemInitialInfo msg = {
        .header = {
            .type = GUINT32_TO_LE(IVSHMEM_MSG_INIT),
            .len = GUINT32_TO_LE(sizeof(msg)),
        },
        .version = GUINT32_TO_LE(IVSHMEM_PROTOCOL_VERSION),
        .compatible_version = GUINT32_TO_LE(IVSHMEM_PROTOCOL_VERSION),
        .id = GUINT32_TO_LE(peer->id),
        .max_peers = GUINT32_TO_LE(server->args.max_peers),
        .vectors = GUINT32_TO_LE(server->args.vectors),
        .protocol = GUINT32_TO_LE(server->args.protocol),
        .output_section_size = GUINT64_TO_LE(server->args.output_section_size),
    };
    unsigned virtio_protocol;
    int ret;

    if (server->args.protocol >= 0x8000) {
        virtio_protocol = server->args.protocol & ~0x4000;
        msg.protocol &= ~0x4000;
        if (peer->id == 0) {
            virtio_protocol |= 0x4000;
        }
        msg.protocol = GUINT32_TO_LE(virtio_protocol);
    }

    ret = ivshmem_server_send_msg(peer->sock_fd, &msg, sizeof(msg),
                                  server->shm_fd);
    if (ret < 0) {
        IVSHMEM_SERVER_DEBUG(server, "cannot send initial info: %s\n",
                             strerror(errno));
        return -1;
    }

    return 0;
}

/* handle message on listening unix socket (new client connection) */
static int
ivshmem_server_handle_new_conn(IvshmemServer *server)
{
    IvshmemServerPeer *peer, *other_peer;
    struct sockaddr_un unaddr;
    socklen_t unaddr_len;
    int newfd;
    unsigned i;

    /* accept the incoming connection */
    unaddr_len = sizeof(unaddr);
    newfd = qemu_accept(server->sock_fd,
                        (struct sockaddr *)&unaddr, &unaddr_len);

    if (newfd < 0) {
        IVSHMEM_SERVER_DEBUG(server, "cannot accept() %s\n", strerror(errno));
        return -1;
    }

    qemu_set_nonblock(newfd);
    IVSHMEM_SERVER_DEBUG(server, "accept()=%d\n", newfd);

    /* allocate new structure for this peer */
    peer = g_malloc0(sizeof(*peer));
    peer->sock_fd = newfd;

    /* get an unused peer id */
    /* XXX: this could use id allocation such as Linux IDA, or simply
     * a free-list */
    for (i = 0; i < G_MAXUINT16; i++) {
        if (ivshmem_server_search_peer(server, i) == NULL) {
            break;
        }
    }
    if (i >= server->args.max_peers) {
        IVSHMEM_SERVER_DEBUG(server, "cannot allocate new client id\n");
        close(newfd);
        g_free(peer);
        return -1;
    }
    peer->id = i;

    /* create eventfd, one per vector */
    peer->vectors_count = server->args.vectors;
    for (i = 0; i < peer->vectors_count; i++) {
        if (event_notifier_init(&peer->vectors[i], FALSE) < 0) {
            IVSHMEM_SERVER_DEBUG(server, "cannot create eventfd\n");
            goto fail;
        }
    }

    /* send peer id and shm fd */
    if (ivshmem_server_send_initial_info(server, peer) < 0) {
        IVSHMEM_SERVER_DEBUG(server, "cannot send initial info\n");
        goto fail;
    }

    /* advertise the new peer to others */
    QTAILQ_FOREACH(other_peer, &server->peer_list, next) {
        for (i = 0; i < peer->vectors_count; i++) {
            ivshmem_server_send_event_fd(other_peer->sock_fd, peer->id, i,
                                         peer->vectors[i].wfd);
        }
    }

    /* advertise the other peers to the new one */
    QTAILQ_FOREACH(other_peer, &server->peer_list, next) {
        for (i = 0; i < peer->vectors_count; i++) {
            ivshmem_server_send_event_fd(peer->sock_fd, other_peer->id, i,
                                         other_peer->vectors[i].wfd);
        }
    }

    /* advertise the new peer to itself */
    for (i = 0; i < peer->vectors_count; i++) {
        ivshmem_server_send_event_fd(peer->sock_fd, peer->id, i,
                                     event_notifier_get_fd(&peer->vectors[i]));
    }

    QTAILQ_INSERT_TAIL(&server->peer_list, peer, next);
    IVSHMEM_SERVER_DEBUG(server, "new peer id = %" PRId64 "\n",
                         peer->id);
    return 0;

fail:
    while (i--) {
        event_notifier_cleanup(&peer->vectors[i]);
    }
    close(newfd);
    g_free(peer);
    return -1;
}

/* Try to ftruncate a file to next power of 2 of shmsize.
 * If it fails; all power of 2 above shmsize are tested until
 * we reach the maximum huge page size. This is useful
 * if the shm file is in a hugetlbfs that cannot be truncated to the
 * shm_size value. */
static int
ivshmem_server_ftruncate(int fd, unsigned shmsize)
{
    int ret;
    struct stat mapstat;

    /* align shmsize to next power of 2 */
    shmsize = pow2ceil(shmsize);

    if (fstat(fd, &mapstat) != -1 && mapstat.st_size == shmsize) {
        return 0;
    }

    while (shmsize <= IVSHMEM_SERVER_MAX_HUGEPAGE_SIZE) {
        ret = ftruncate(fd, shmsize);
        if (ret == 0) {
            return ret;
        }
        shmsize *= 2;
    }

    return -1;
}

/* Init a new ivshmem server */
void ivshmem_server_init(IvshmemServer *server)
{
    server->sock_fd = -1;
    server->shm_fd = -1;
    server->state_table = NULL;
    QTAILQ_INIT(&server->peer_list);
}

/* open shm, create and bind to the unix socket */
int
ivshmem_server_start(IvshmemServer *server)
{
    struct sockaddr_un sun;
    int shm_fd, sock_fd, ret;
    void *state_table;

    /* open shm file */
    if (server->args.use_shm_open) {
        IVSHMEM_SERVER_DEBUG(server, "Using POSIX shared memory: %s\n",
                             server->args.shm_path);
        shm_fd = shm_open(server->args.shm_path, O_CREAT | O_RDWR, S_IRWXU);
    } else {
        gchar *filename = g_strdup_printf("%s/ivshmem.XXXXXX",
                                          server->args.shm_path);
        IVSHMEM_SERVER_DEBUG(server, "Using file-backed shared memory: %s\n",
                             server->args.shm_path);
        shm_fd = mkstemp(filename);
        unlink(filename);
        g_free(filename);
    }

    if (shm_fd < 0) {
        fprintf(stderr, "cannot open shm file %s: %s\n", server->args.shm_path,
                strerror(errno));
        return -1;
    }
    if (ivshmem_server_ftruncate(shm_fd, server->args.shm_size) < 0) {
        fprintf(stderr, "ftruncate(%s) failed: %s\n", server->args.shm_path,
                strerror(errno));
        goto err_close_shm;
    }
    state_table = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED,
                       shm_fd, 0);
    if (state_table == MAP_FAILED) {
        fprintf(stderr, "mmap failed: %s\n", strerror(errno));
        goto err_close_shm;
    }

    IVSHMEM_SERVER_DEBUG(server, "create & bind socket %s\n",
                         server->args.unix_socket_path);

    /* create the unix listening socket */
    sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        IVSHMEM_SERVER_DEBUG(server, "cannot create socket: %s\n",
                             strerror(errno));
        goto err_unmap;
    }

    sun.sun_family = AF_UNIX;
    ret = snprintf(sun.sun_path, sizeof(sun.sun_path), "%s",
                   server->args.unix_socket_path);
    if (ret < 0 || ret >= sizeof(sun.sun_path)) {
        IVSHMEM_SERVER_DEBUG(server, "could not copy unix socket path\n");
        goto err_close_sock;
    }
    if (bind(sock_fd, (struct sockaddr *)&sun, sizeof(sun)) < 0) {
        IVSHMEM_SERVER_DEBUG(server, "cannot connect to %s: %s\n", sun.sun_path,
                             strerror(errno));
        goto err_close_sock;
    }

    if (listen(sock_fd, IVSHMEM_SERVER_LISTEN_BACKLOG) < 0) {
        IVSHMEM_SERVER_DEBUG(server, "listen() failed: %s\n", strerror(errno));
        goto err_close_sock;
    }

    server->sock_fd = sock_fd;
    server->shm_fd = shm_fd;
    server->state_table = state_table;

    return 0;

err_close_sock:
    close(sock_fd);
err_unmap:
    munmap(state_table, 4096);
err_close_shm:
    if (server->args.use_shm_open) {
        shm_unlink(server->args.shm_path);
    }
    close(shm_fd);
    shm_unlink(server->args.shm_path);
    return -1;
}

/* close connections to clients, the unix socket and the shm fd */
void
ivshmem_server_close(IvshmemServer *server)
{
    IvshmemServerPeer *peer, *npeer;

    IVSHMEM_SERVER_DEBUG(server, "close server\n");

    QTAILQ_FOREACH_SAFE(peer, &server->peer_list, next, npeer) {
        ivshmem_server_free_peer(server, peer);
    }

    unlink(server->args.unix_socket_path);
    if (server->args.use_shm_open) {
        shm_unlink(server->args.shm_path);
    }
    close(server->sock_fd);
    munmap(server->state_table, 4096);
    close(server->shm_fd);
    server->sock_fd = -1;
    server->shm_fd = -1;
}

/* get the fd_set according to the unix socket and the peer list */
void
ivshmem_server_get_fds(const IvshmemServer *server, fd_set *fds, int *maxfd)
{
    IvshmemServerPeer *peer;

    if (server->sock_fd == -1) {
        return;
    }

    FD_SET(server->sock_fd, fds);
    if (server->sock_fd >= *maxfd) {
        *maxfd = server->sock_fd + 1;
    }

    QTAILQ_FOREACH(peer, &server->peer_list, next) {
        FD_SET(peer->sock_fd, fds);
        if (peer->sock_fd >= *maxfd) {
            *maxfd = peer->sock_fd + 1;
        }
    }
}

/* process incoming messages on the sockets in fd_set */
int
ivshmem_server_handle_fds(IvshmemServer *server, fd_set *fds, int maxfd)
{
    IvshmemServerPeer *peer, *peer_next;

    if (server->sock_fd < maxfd && FD_ISSET(server->sock_fd, fds) &&
        ivshmem_server_handle_new_conn(server) < 0 && errno != EINTR) {
        IVSHMEM_SERVER_DEBUG(server, "ivshmem_server_handle_new_conn() "
                             "failed\n");
        return -1;
    }

    QTAILQ_FOREACH_SAFE(peer, &server->peer_list, next, peer_next) {
        /* any message from a peer socket result in a close() */
        IVSHMEM_SERVER_DEBUG(server, "peer->sock_fd=%d\n", peer->sock_fd);
        if (peer->sock_fd < maxfd && FD_ISSET(peer->sock_fd, fds)) {
            ivshmem_server_free_peer(server, peer);
        }
    }

    return 0;
}

/* lookup peer from its id */
IvshmemServerPeer *
ivshmem_server_search_peer(IvshmemServer *server, int64_t peer_id)
{
    IvshmemServerPeer *peer;

    QTAILQ_FOREACH(peer, &server->peer_list, next) {
        if (peer->id == peer_id) {
            return peer;
        }
    }
    return NULL;
}
