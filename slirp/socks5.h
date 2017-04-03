#ifndef SOCKS5_H
#define SOCKS5_H

#include <sys/types.h>
#include <sys/socket.h>

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
int socks5_send(int fd, const char *user, const char *password,
                struct sockaddr_storage addr, socks5_state_t *state);
void socks5_recv(int fd, socks5_state_t *state);
#endif
