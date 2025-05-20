/*
 * unix socket functions for qemu
 *
 * (c) 2008 Gerd Hoffmann <kraxel@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/sockets.h"
#include "qapi/error.h"

#include "socket-helpers.h"

static bool saddr_is_abstract(UnixSocketAddress *saddr)
{
#ifdef CONFIG_LINUX
    return saddr->abstract;
#else
    return false;
#endif
}

static bool saddr_is_tight(UnixSocketAddress *saddr)
{
#ifdef CONFIG_LINUX
    return !saddr->has_tight || saddr->tight;
#else
    return false;
#endif
}

int unix_listen_saddr(UnixSocketAddress *saddr, int num, Error **errp)
{
    bool abstract = saddr_is_abstract(saddr);
    struct sockaddr_un un;
    int sock, fd;
    char *pathbuf = NULL;
    const char *path;
    size_t pathlen;
    size_t addrlen;

    sock = qemu_socket(PF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        error_setg_errno(errp, errno, "Failed to create Unix socket");
        return -1;
    }

    if (saddr->path[0] || abstract) {
        path = saddr->path;
    } else {
        path = pathbuf = g_strdup_printf("%s/qemu-socket-XXXXXX",
                                         g_get_tmp_dir());
    }

    pathlen = strlen(path);
    if (pathlen > sizeof(un.sun_path) ||
        (abstract && pathlen > (sizeof(un.sun_path) - 1))) {
        error_setg(errp, "UNIX socket path '%s' is too long", path);
        error_append_hint(errp, "Path must be less than %zu bytes\n",
                          abstract ? sizeof(un.sun_path) - 1 :
                          sizeof(un.sun_path));
        goto err;
    }

    if (pathbuf != NULL) {
        /*
         * This dummy fd usage silences the mktemp() insecure warning.
         * Using mkstemp() doesn't make things more secure here
         * though.  bind() complains about existing files, so we have
         * to unlink first and thus re-open the race window.  The
         * worst case possible is bind() failing, i.e. a DoS attack.
         */
        fd = mkstemp(pathbuf);
        if (fd < 0) {
            error_setg_errno(errp, errno,
                             "Failed to make a temporary socket %s", pathbuf);
            goto err;
        }
        close(fd);
    }

    if (!abstract && unlink(path) < 0 && errno != ENOENT) {
        error_setg_errno(errp, errno,
                         "Failed to unlink socket %s", path);
        goto err;
    }

    memset(&un, 0, sizeof(un));
    un.sun_family = AF_UNIX;
    addrlen = sizeof(un);

    if (abstract) {
        un.sun_path[0] = '\0';
        memcpy(&un.sun_path[1], path, pathlen);
        if (saddr_is_tight(saddr)) {
            addrlen = offsetof(struct sockaddr_un, sun_path) + 1 + pathlen;
        }
    } else {
        memcpy(un.sun_path, path, pathlen);
    }

    if (bind(sock, (struct sockaddr *) &un, addrlen) < 0) {
        error_setg_errno(errp, errno, "Failed to bind socket to %s", path);
        goto err;
    }
    if (listen(sock, num) < 0) {
        error_setg_errno(errp, errno, "Failed to listen on socket");
        goto err;
    }

    g_free(pathbuf);
    return sock;

err:
    g_free(pathbuf);
    close(sock);
    return -1;
}

int unix_connect_saddr(UnixSocketAddress *saddr, Error **errp)
{
    bool abstract = saddr_is_abstract(saddr);
    struct sockaddr_un un;
    int sock, rc;
    size_t pathlen;
    size_t addrlen;

    if (saddr->path == NULL) {
        error_setg(errp, "unix connect: no path specified");
        return -1;
    }

    sock = qemu_socket(PF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        error_setg_errno(errp, errno, "Failed to create socket");
        return -1;
    }

    pathlen = strlen(saddr->path);
    if (pathlen > sizeof(un.sun_path) ||
        (abstract && pathlen > (sizeof(un.sun_path) - 1))) {
        error_setg(errp, "UNIX socket path '%s' is too long", saddr->path);
        error_append_hint(errp, "Path must be less than %zu bytes\n",
                          abstract ? sizeof(un.sun_path) - 1 :
                          sizeof(un.sun_path));
        goto err;
    }

    memset(&un, 0, sizeof(un));
    un.sun_family = AF_UNIX;
    addrlen = sizeof(un);

    if (abstract) {
        un.sun_path[0] = '\0';
        memcpy(&un.sun_path[1], saddr->path, pathlen);
        if (saddr_is_tight(saddr)) {
            addrlen = offsetof(struct sockaddr_un, sun_path) + 1 + pathlen;
        }
    } else {
        memcpy(un.sun_path, saddr->path, pathlen);
    }
    /* connect to peer */
    do {
        rc = 0;
        if (connect(sock, (struct sockaddr *) &un, addrlen) < 0) {
            rc = -errno;
        }
    } while (rc == -EINTR);

    if (rc < 0) {
        error_setg_errno(errp, -rc, "Failed to connect to '%s'",
                         saddr->path);
        goto err;
    }

    return sock;

 err:
    close(sock);
    return -1;
}

/* compatibility wrapper */
int unix_listen(const char *str, Error **errp)
{
    UnixSocketAddress *saddr;
    int sock;

    saddr = g_new0(UnixSocketAddress, 1);
    saddr->path = g_strdup(str);
    sock = unix_listen_saddr(saddr, 1, errp);
    qapi_free_UnixSocketAddress(saddr);
    return sock;
}

int unix_connect(const char *path, Error **errp)
{
    UnixSocketAddress *saddr;
    int sock;

    saddr = g_new0(UnixSocketAddress, 1);
    saddr->path = g_strdup(path);
    sock = unix_connect_saddr(saddr, errp);
    qapi_free_UnixSocketAddress(saddr);
    return sock;
}
