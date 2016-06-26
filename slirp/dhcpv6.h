#ifndef SLIRP_DHCPV6_H
#define SLIRP_DHCPV6_H

#define DHCPV6_SERVER_PORT 547

void dhcpv6_input(struct sockaddr_in6 *srcsas, struct mbuf *m);

#endif
