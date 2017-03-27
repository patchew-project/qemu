#ifndef SOCKS5_H
#define SOCKS5_H

#include <sys/types.h>
#include <sys/socket.h>

/* some parts from nmap/ncat GPLv2 */

#define SOCKS_BUFF_SIZE 512

struct socks4_data {
    char version;
    char type;
    unsigned short port;
    uint32_t address;
    char data[SOCKS_BUFF_SIZE]; /* to hold FQDN and username */
} __attribute__((packed));

struct socks5_connect {
    char ver;
    char nmethods;
    char methods[3];
} __attribute__((packed));

struct socks5_auth {
  char ver; /* must be always 1 */
  char data[SOCKS_BUFF_SIZE];
} __attribute__((packed));

struct socks5_request {
    char ver;
    char cmd;
    char rsv;
    char atyp;
    char dst[SOCKS_BUFF_SIZE]; /* addr/name and port info */
} __attribute__((packed));

/* defines */

/* Default port for SOCKS5 */
#define DEFAULT_SOCKS5_PORT 1080

/* SOCKS4 protocol responses */
#define SOCKS4_VERSION          4
#define SOCKS_CONNECT           1
#define SOCKS_BIND              2
#define SOCKS4_CONN_ACC         90
#define SOCKS4_CONN_REF         91
#define SOCKS4_CONN_IDENT       92
#define SOCKS4_CONN_IDENTDIFF   93

/* SOCKS5 protocol */
#define SOCKS5_VERSION          5
#define SOCKS5_AUTH_NONE        0
#define SOCKS5_AUTH_GSSAPI      1
#define SOCKS5_AUTH_USERPASS    2
#define SOCKS5_AUTH_FAILED      255
#define SOCKS5_ATYP_IPv4        1
#define SOCKS5_ATYP_NAME        3
#define SOCKS5_ATYP_IPv6        4


/* Length of IPv6 address */
#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 46
#endif

typedef enum {
    SOCKS5_STATE_NONE = 0,
    SOCKS5_STATE_CONNECT,
    SOCKS5_STATE_NEGOCIATE,
    SOCKS5_STATE_NEGOCIATING,
    SOCKS5_STATE_AUTHENTICATE,
    SOCKS5_STATE_AUTHENTICATING,
    SOCKS5_STATE_ESTABLISH,
    SOCKS5_STATE_ESTABLISHING,
} socks5_state_t;

int socks5_socket(socks5_state_t *state);
int socks5_connect(int fd, const char *server, int port,
                   socks5_state_t *state);
int socks5_send(int fd, const char *user, const char *passwd,
                struct sockaddr_storage addr, socks5_state_t *state);
void socks5_recv(int fd, socks5_state_t *state);
#endif
