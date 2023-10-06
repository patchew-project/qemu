#ifndef MCDSTUB_H
#define MCDSTUB_H

#define DEFAULT_MCDSTUB_PORT "1235"

/* breakpoint defines */
#define MCD_BREAKPOINT_SW        0
#define MCD_BREAKPOINT_HW        1
#define MCD_WATCHPOINT_WRITE     2
#define MCD_WATCHPOINT_READ      3
#define MCD_WATCHPOINT_ACCESS    4

/**
 * mcd_tcp_server_start: start the tcp server to connect via mcd
 * @device: connection spec for mcd
 */
int mcdserver_start(const char *device);

#endif
