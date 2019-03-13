/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 1982, 1986, 1993
 * The Regents of the University of California.  All rights reserved.
 */

#ifndef UDP_H
#define UDP_H

#define UDP_TTL 0x60
#define UDP_UDPDATALEN 16192

/*
 * Udp protocol header.
 * Per RFC 768, September, 1981.
 */
struct udphdr {
    uint16_t uh_sport;          /* source port */
    uint16_t uh_dport;          /* destination port */
    int16_t  uh_ulen;           /* udp length */
    uint16_t uh_sum;            /* udp checksum */
};

/*
 * UDP kernel structures and variables.
 */
struct udpiphdr {
	        struct  ipovly ui_i;            /* overlaid ip structure */
	        struct  udphdr ui_u;            /* udp header */
};
#define ui_mbuf         ui_i.ih_mbuf.mptr
#define ui_x1           ui_i.ih_x1
#define ui_pr           ui_i.ih_pr
#define ui_len          ui_i.ih_len
#define ui_src          ui_i.ih_src
#define ui_dst          ui_i.ih_dst
#define ui_sport        ui_u.uh_sport
#define ui_dport        ui_u.uh_dport
#define ui_ulen         ui_u.uh_ulen
#define ui_sum          ui_u.uh_sum

/*
 * Names for UDP sysctl objects
 */
#define UDPCTL_CHECKSUM         1       /* checksum UDP packets */
#define UDPCTL_MAXID            2

struct mbuf;

void udp_init(Slirp *);
void udp_cleanup(Slirp *);
void udp_input(register struct mbuf *, int);
int udp_attach(struct socket *, unsigned short af);
void udp_detach(struct socket *);
struct socket * udp_listen(Slirp *, uint32_t, unsigned, uint32_t, unsigned,
                           int);
int udp_output(struct socket *so, struct mbuf *m,
                struct sockaddr_in *saddr, struct sockaddr_in *daddr,
                int iptos);

void udp6_input(register struct mbuf *);
int udp6_output(struct socket *so, struct mbuf *m,
                struct sockaddr_in6 *saddr, struct sockaddr_in6 *daddr);

#endif
