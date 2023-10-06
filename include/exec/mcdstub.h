#ifndef MCDSTUB_H
#define MCDSTUB_H

#define DEFAULT_MCDSTUB_PORT "1235"

/**
 * mcd_tcp_server_start: start the tcp server to connect via mcd
 * @device: connection spec for mcd
 */
int mcdserver_start(const char *device);

#endif
