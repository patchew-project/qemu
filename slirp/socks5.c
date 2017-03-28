/* some parts from nmap/ncat GPLv2 */

#include "qemu/osdep.h"
#include "qemu/sockets.h"

#include "socks5.h"

static int socks5_proxy_connect(int fd, const char *server, int port)
{
    struct sockaddr_in saddr;
    struct hostent *he;

    he = gethostbyname(server);
    if (he == NULL) {
        errno = EINVAL;
        return -1;
    }
    saddr.sin_family = AF_INET;
    saddr.sin_addr = *(struct in_addr *)he->h_addr;
    saddr.sin_port = htons(port);

    return connect(fd, (struct sockaddr *)&saddr, sizeof(saddr));
}

static int socks5_send_negociate(int fd, const char *user,
                                 const char *password)
{
    struct socks5_connect socks5msg;
    int len;

    memset(&socks5msg, 0, sizeof(socks5msg));
    socks5msg.ver = SOCKS5_VERSION;
    socks5msg.nmethods = 1;
    socks5msg.methods[0] = SOCKS5_AUTH_NONE;
    len = 3;

    if (user && password) {
        socks5msg.nmethods++;
        socks5msg.methods[1] = SOCKS5_AUTH_USERPASS;
        len++;
    }

    return send(fd, (char *)&socks5msg, len, 0);
}

static int socks5_recv_negociate(int fd)
{
    char socksbuf[2];

    /* socksbuf[0] is the protocol version number: 0x05
     * socksbuf[1] is the selected authentification protocol
     */

    if (recv(fd, socksbuf, 2, 0) != 2) {
        return -1;
    }

    if (socksbuf[0] != SOCKS5_VERSION) {
        errno = EINVAL;
        return -1;
    }

    return socksbuf[1];
}

static int socks5_send_authenticate(int fd, const char *user,
                                    const char *password)
{
    struct socks5_auth socks5auth;
    int len;

    if (user == NULL || password == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (strlen(user) + strlen(password) > SOCKS_BUFF_SIZE - 2) {
        errno = EINVAL;
        return -1;
    }

    socks5auth.ver = 1;
    socks5auth.data[0] = strlen(user);
    memcpy(socks5auth.data + 1, user, strlen(user));
    len = 2 + strlen(user);

    socks5auth.data[len - 1] = strlen(password);
    memcpy(socks5auth.data + len, password, strlen(password));
    len += 1 + strlen(password);

    return send(fd, (char *)&socks5auth, len, 0);
}

static int socks5_recv_authenticate(int fd)
{
    char socksbuf[2];
    if (recv(fd, socksbuf, 2, 0) != 2) {
        return -1;
    }
    if (socksbuf[0] != 1 || socksbuf[1] != 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static int socks5_send_connect(int fd, struct sockaddr_storage *addr)
{
    struct socks5_request socks5msg;
    int len;

    memset(&socks5msg, 0, sizeof(socks5msg));

    socks5msg.ver = SOCKS5_VERSION;
    socks5msg.cmd = SOCKS_CONNECT;
    socks5msg.rsv = 0;

    switch (addr->ss_family) {
    case AF_INET: {
            struct sockaddr_in *a = (struct sockaddr_in *)addr;

            socks5msg.atyp = SOCKS5_ATYP_IPv4;
            memcpy(socks5msg.dst, &a->sin_addr, sizeof(a->sin_addr));
            len = sizeof(a->sin_addr);
            memcpy(socks5msg.dst + len, &a->sin_port, sizeof(a->sin_port));
            len += sizeof(a->sin_port);
        }
        break;
    case AF_INET6: {
            struct sockaddr_in6 *a = (struct sockaddr_in6 *)addr;

            socks5msg.atyp = SOCKS5_ATYP_IPv6;
            memcpy(socks5msg.dst, &a->sin6_addr, sizeof(a->sin6_addr));
            len = sizeof(a->sin6_addr);
            memcpy(socks5msg.dst + len, &a->sin6_port, sizeof(a->sin6_port));
            len += sizeof(a->sin6_port);
        }
        break;
    default:
        errno = EINVAL;
        return -1;
    }
    len += 4;

    return send(fd, (char *)&socks5msg, len, 0);
}

static int socks5_recv_connect(int fd)
{
    struct socks5_request socks5msg;

    if (recv(fd, &socks5msg, 4, 0) != 4) {
        return -1;
    }

    if (socks5msg.ver != SOCKS5_VERSION) {
        errno = EINVAL;
        return -1;
    }

    if (socks5msg.cmd != 0x00) {
        errno = EINVAL;
        return -1;
    }

    switch (socks5msg.atyp) {
    case SOCKS5_ATYP_IPv4:
        if (recv(fd, socks5msg.dst, 6, 0) != 6) {
            return -1;
        }
        break;
    case SOCKS5_ATYP_IPv6:
        if (recv(fd, socks5msg.dst, 18, 0) != 18) {
            return -1;
        }
        break;
    case SOCKS5_ATYP_NAME:
        if (recv(fd, socks5msg.dst, 1, 0) != 1) {
            return -1;
        }
        if (recv(fd, socks5msg.dst + 1,
                 socks5msg.dst[0], 0) != socks5msg.dst[0]) {
            return -1;
        }
        break;
    default:
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int socks5_socket(socks5_state_t *state)
{
    *state = SOCKS5_STATE_CONNECT;
    return qemu_socket(AF_INET, SOCK_STREAM, 0);
}

int socks5_connect(int fd, const char *server, int port,
                   socks5_state_t *state)
{
    if (*state != SOCKS5_STATE_CONNECT) {
        *state = SOCKS5_STATE_NONE;
        errno = EINVAL;
        return -1;
    }

    *state = SOCKS5_STATE_NEGOCIATE;
    return socks5_proxy_connect(fd, server, port);
}

int socks5_send(int fd, const char *user, const char *password,
                struct sockaddr_storage addr, socks5_state_t *state)
{
    int ret;

    switch (*state) {
    case SOCKS5_STATE_NEGOCIATE:
        ret = socks5_send_negociate(fd, user, password);
        if (ret < 0) {
            return ret;
        }
        ret = 0;
        *state = SOCKS5_STATE_NEGOCIATING;
        break;
    case SOCKS5_STATE_AUTHENTICATE:
        ret = socks5_send_authenticate(fd, user, password);
        if (ret < 0) {
            return ret;
        }
        ret = 0;
        *state = SOCKS5_STATE_AUTHENTICATING;
        break;
    case SOCKS5_STATE_ESTABLISH:
        ret = socks5_send_connect(fd, &addr);
        if (ret < 0) {
            return ret;
        }
        ret = 0;
        *state = SOCKS5_STATE_ESTABLISHING;
        break;
    case SOCKS5_STATE_NONE:
        ret = 1;
        break;
    default:
        ret = 0;
        break;
    }
    return ret;
}

void socks5_recv(int fd, socks5_state_t *state)
{
    int ret;

    switch (*state) {
    case SOCKS5_STATE_NEGOCIATING:
        switch (socks5_recv_negociate(fd)) {
        case SOCKS5_AUTH_NONE: /* no authentification */
            *state = SOCKS5_STATE_ESTABLISH;
            break;
        case SOCKS5_AUTH_USERPASS: /* username/password */
            *state = SOCKS5_STATE_AUTHENTICATE;
            break;
        default:
            break;
        }
        break;
    case SOCKS5_STATE_AUTHENTICATING:
        ret = socks5_recv_authenticate(fd);
        if (ret >= 0) {
            *state = SOCKS5_STATE_ESTABLISH;
        }
        break;
    case SOCKS5_STATE_ESTABLISHING:
        ret = socks5_recv_connect(fd);
        if (ret >= 0) {
            *state = SOCKS5_STATE_NONE;
        }
        break;
    default:
        break;
    }
}
