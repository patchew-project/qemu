/* based on RFC 1928
 *   SOCKS Protocol Version 5
 * based on RFC 1929
 *   Username/Password Authentication for SOCKS V5
 * TODO:
 *   - RFC 1961 GSS-API Authentication Method for SOCKS Version 5
 *   - manage buffering on recv()
 *   - IPv6 connection to proxy
 */

#include "qemu/osdep.h"
#include "qemu/sockets.h"
#include "qemu/log.h"

#include "socks5.h"

#define SOCKS_LEN_MAX                  UINT8_MAX

#define SOCKS_VERSION_5                0x05

#define SOCKS5_AUTH_METHOD_NONE        0x00
#define SOCKS5_AUTH_METHOD_GSSAPI      0x01
#define SOCKS5_AUTH_METHOD_PASSWORD    0x02
#define SOCKS5_AUTH_METHOD_REJECTED    0xff

#define SOCKS5_AUTH_PASSWORD_VERSION   0x01
#define SOCKS5_AUTH_PASSWORD_SUCCESS   0x00

#define SOCKS5_CMD_CONNECT             0x01
#define SOCKS5_CMD_BIND                0x02
#define SOCKS5_CMD_UDP_ASSOCIATE       0x03

#define SOCKS5_ATYPE_IPV4              0x01
#define SOCKS5_ATYPE_FQDN              0x03
#define SOCKS5_ATYPE_IPV6              0x04

#define SOCKS5_CMD_SUCCESS             0x00
#define SOCKS5_CMD_SERVER_FAILURE      0x01
#define SOCKS5_CMD_NOT_ALLOWED         0x02
#define SOCKS5_CMD_NETWORK_UNREACHABLE 0x03
#define SOCKS5_CMD_HOST_UNREACHABLE    0x04
#define SOCKS5_CMD_CONNECTION_REFUSED  0x05
#define SOCKS5_CMD_TTL_EXPIRED         0x06
#define SOCKS5_CMD_NOT_SUPPORTED       0x07
#define SOCKS5_CMD_ATYPE_NOT_SUPPORTED 0x08

#define SOCKS5_NEGOCIATE_HDR_LEN       2
#define SOCKS5_PASSWD_HDR_LEN          2
#define SOCKS5_CONNECT_HDR_LEN         4
#define SOCKS5_ATYPE_IPV4_LEN          (sizeof(struct in_addr) + \
                                        sizeof(in_port_t))
#define SOCKS5_ATYPE_IPV6_LEN          (sizeof(struct in6_addr) + \
                                        sizeof(in_port_t))

static int socks5_proxy_connect(int fd, const char *server, int port)
{
    struct sockaddr_in saddr;
    struct hostent *he;

    he = gethostbyname(server);
    if (he == NULL) {
        errno = EINVAL;
        return -1;
    }
    /* TODO: IPv6 */
    saddr.sin_family = AF_INET;
    saddr.sin_addr = *(struct in_addr *)he->h_addr;
    saddr.sin_port = htons(port);

    return connect(fd, (struct sockaddr *)&saddr, sizeof(saddr));
}

static int socks5_send_negociate(int fd, const char *user,
                                 const char *password)
{
    uint8_t cmd[4];
    int len = 0;

    cmd[len++] = SOCKS_VERSION_5;
    if (user && password) {
        cmd[len++] = 2;
        cmd[len++] = SOCKS5_AUTH_METHOD_NONE;
        cmd[len++] = SOCKS5_AUTH_METHOD_PASSWORD;
    } else {
        cmd[len++] = 1;
        cmd[len++] = SOCKS5_AUTH_METHOD_NONE;
    }
    return send(fd, cmd, len, 0);
}

static int socks5_recv_negociate(int fd)
{
    char reply[2];

    /* reply[0] is the protocol version number: 0x05
     * reply[1] is the selected authentification protocol
     */

    if (recv(fd, reply, SOCKS5_NEGOCIATE_HDR_LEN, 0) !=
        SOCKS5_NEGOCIATE_HDR_LEN) {
        return -1;
    }

    if (reply[0] != SOCKS_VERSION_5) {
        errno = EINVAL;
        return -1;
    }

    return (unsigned char)reply[1];
}

static int socks5_send_password(int fd, const char *user,
                                const char *password)
{
    uint8_t *cmd;
    int len = 0, ulen, plen;

    if (user == NULL || password == NULL) {
        errno = EINVAL;
        return -1;
    }

    ulen = strlen(user);
    plen = strlen(password);
    if (ulen > SOCKS_LEN_MAX || plen > SOCKS_LEN_MAX) {
        errno = EINVAL;
        return -1;
    }

    cmd = alloca(3 + ulen + plen);

    cmd[len++] = SOCKS5_AUTH_PASSWORD_VERSION;
    cmd[len++] = ulen;
    memcpy(cmd + len, user, ulen);
    len += ulen;
    cmd[len++] = plen;
    memcpy(cmd + len, password, plen);

    return send(fd, cmd, len, 0);
}

static int socks5_recv_password(int fd)
{
    char reply[2];
    if (recv(fd, reply, SOCKS5_PASSWD_HDR_LEN, 0) != SOCKS5_PASSWD_HDR_LEN) {
        return -1;
    }

    /* reply[0] is the subnegociation version number: 0x01
     * reply[1] is the status
     */
    if (reply[0] != SOCKS5_AUTH_PASSWORD_VERSION ||
        reply[1] != SOCKS5_AUTH_PASSWORD_SUCCESS) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static int socks5_send_connect(int fd, struct sockaddr_storage *addr)
{
    uint8_t cmd[22]; /* max size with IPv6 address */
    int len = 0;

    cmd[len++] = SOCKS_VERSION_5;
    cmd[len++] = SOCKS5_CMD_CONNECT;
    cmd[len++] = 0; /* reserved */

    switch (addr->ss_family) {
    case AF_INET: {
            struct sockaddr_in *a = (struct sockaddr_in *)addr;
            cmd[len++] = SOCKS5_ATYPE_IPV4;
            memcpy(cmd + len, &a->sin_addr, sizeof(struct in_addr));
            len += sizeof(struct in_addr);
            memcpy(cmd + len, &a->sin_port, sizeof(in_port_t));
            len += sizeof(in_port_t);
        }
        break;
    case AF_INET6: {
            struct sockaddr_in6 *a = (struct sockaddr_in6 *)addr;
            cmd[len++] = SOCKS5_ATYPE_IPV6;
            memcpy(cmd + len, &a->sin6_addr, sizeof(struct in6_addr));
            len += sizeof(struct in6_addr);
            memcpy(cmd + len, &a->sin6_port, sizeof(in_port_t));
            len += sizeof(in_port_t);
        }
        break;
    default:
        errno = EINVAL;
        return -1;
    }

    return send(fd, cmd, len, 0);
}

static int socks5_recv_connect(int fd)
{
    uint8_t reply[7 + SOCKS_LEN_MAX]; /* can contains a FQDN */

    /*
     * reply[0] is protocol version: 5
     * reply[1] is reply field
     * reply[2] is reserved
     * reply[3] is address type */

    if (recv(fd, reply, SOCKS5_CONNECT_HDR_LEN, 0) != SOCKS5_CONNECT_HDR_LEN) {
        return -1;
    }

    if (reply[0] != SOCKS_VERSION_5) {
        qemu_log_mask(LOG_GUEST_ERROR, "Invalid SOCKS version: %d\n", reply[0]);
        errno = EINVAL;
        return -1;
    }

    if (reply[1] != SOCKS5_CMD_SUCCESS) {
        qemu_log_mask(LOG_GUEST_ERROR, "SOCKS5 connection error: %d\n",
                      reply[1]);
        errno = EINVAL;
        return -1;
    }

    switch (reply[3]) {
    case SOCKS5_ATYPE_IPV4:
        if (recv(fd, reply + SOCKS5_CONNECT_HDR_LEN,
                 SOCKS5_ATYPE_IPV4_LEN, 0) != SOCKS5_ATYPE_IPV4_LEN) {
            return -1;
        }
        break;
    case SOCKS5_ATYPE_IPV6:
        if (recv(fd, reply + SOCKS5_CONNECT_HDR_LEN,
                 SOCKS5_ATYPE_IPV6_LEN, 0) != SOCKS5_ATYPE_IPV6_LEN) {
            return -1;
        }
        break;
    case SOCKS5_ATYPE_FQDN:
        if (recv(fd, reply + SOCKS5_CONNECT_HDR_LEN, 1, 0) != 1) {
            return -1;
        }
        if (recv(fd, reply + SOCKS5_CONNECT_HDR_LEN + 1,
                 reply[SOCKS5_CONNECT_HDR_LEN], 0) !=
            reply[SOCKS5_CONNECT_HDR_LEN]) {
            return -1;
        }
        qemu_log_mask(LOG_GUEST_ERROR, "Unsupported SOCKS5 ATYPE: FDDN\n");
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
        *state = SOCKS5_STATE_NEGOCIATING;
        break;
    case SOCKS5_STATE_AUTHENTICATE:
        ret = socks5_send_password(fd, user, password);
        if (ret < 0) {
            return ret;
        }
        *state = SOCKS5_STATE_AUTHENTICATING;
        break;
    case SOCKS5_STATE_ESTABLISH:
        ret = socks5_send_connect(fd, &addr);
        if (ret < 0) {
            return ret;
        }
        *state = SOCKS5_STATE_ESTABLISHING;
        break;
    case SOCKS5_STATE_NONE:
        return 1;
    case SOCKS5_STATE_NEGOCIATING:
    case SOCKS5_STATE_AUTHENTICATING:
    case SOCKS5_STATE_ESTABLISHING:
        return 0;
    case SOCKS5_STATE_CONNECT:
    case SOCKS5_STATE_ERROR:
        *state = SOCKS5_STATE_ERROR;
        errno = EINVAL;
        return -1;
    }
    return 0;
}

void socks5_recv(int fd, socks5_state_t *state)
{
    int ret;

    switch (*state) {
    case SOCKS5_STATE_NEGOCIATING:
        ret = socks5_recv_negociate(fd);
        if (ret < 0) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "SOCKS5 AUTH method error: %d\n", errno);
            *state = SOCKS5_STATE_ERROR;
            return;
        }
        switch (ret) {
        case SOCKS5_AUTH_METHOD_NONE: /* no authentification */
            *state = SOCKS5_STATE_ESTABLISH;
            break;
        case SOCKS5_AUTH_METHOD_PASSWORD: /* username/password */
            *state = SOCKS5_STATE_AUTHENTICATE;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "SOCKS5 unsupported AUTH method: %d\n", ret);
            *state = SOCKS5_STATE_ERROR;
            break;
        }
        break;
    case SOCKS5_STATE_AUTHENTICATING:
        ret = socks5_recv_password(fd);
        if (ret < 0) {
            *state = SOCKS5_STATE_ERROR;
            return;
        }
        *state = SOCKS5_STATE_ESTABLISH;
        break;
    case SOCKS5_STATE_ESTABLISHING:
        ret = socks5_recv_connect(fd);
        if (ret < 0) {
            *state = SOCKS5_STATE_ERROR;
            return;
        }
        *state = SOCKS5_STATE_NONE;
        break;
    case SOCKS5_STATE_NONE:
    case SOCKS5_STATE_CONNECT:
    case SOCKS5_STATE_NEGOCIATE:
    case SOCKS5_STATE_AUTHENTICATE:
    case SOCKS5_STATE_ESTABLISH:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Internal error: invalid state in socks5_recv(): %d\n",
                      *state);
        *state = SOCKS5_STATE_ERROR;
        break;
    case SOCKS5_STATE_ERROR:
        break;
    }
}
