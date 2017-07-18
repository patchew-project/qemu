/*
 * QEMU System Emulator
 *
 * Copyright (c) 2015-2017 Cambridge Greys Limited
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2012-2014 Cisco Systems
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include <linux/ip.h>
#include <netdb.h>
#include "net/net.h"
#include "clients.h"
#include "qemu-common.h"
#include "qemu/error-report.h"
#include "qemu/option.h"
#include "qemu/sockets.h"
#include "qemu/iov.h"
#include "qemu/main-loop.h"
#include "unified.h"


/* Header set to 0x30000 signifies a data packet */

#define L2TPV3_DATA_PACKET 0x30000

/* IANA-assigned IP protocol ID for L2TPv3 */

#ifndef IPPROTO_L2TP
#define IPPROTO_L2TP 0x73
#endif

typedef struct L2TPV3TunnelParams {
    /*
     * L2TPv3 parameters
     */

    uint64_t rx_cookie;
    uint64_t tx_cookie;
    uint32_t rx_session;
    uint32_t tx_session;
    uint32_t counter;

    /* Flags */

    bool ipv6;
    bool udp;
    bool has_counter;
    bool pin_counter;
    bool cookie;
    bool cookie_is_64;

    /* Precomputed L2TPV3 specific offsets */
    uint32_t cookie_offset;
    uint32_t counter_offset;
    uint32_t session_offset;

} L2TPV3TunnelParams;



static void l2tpv3_form_header(void *us)
{
    NetUnifiedState *s = (NetUnifiedState *) us;
    L2TPV3TunnelParams *p = (L2TPV3TunnelParams *) s->params;

    uint32_t *counter;

    if (p->udp) {
        stl_be_p((uint32_t *) s->header_buf, L2TPV3_DATA_PACKET);
    }
    stl_be_p(
            (uint32_t *) (s->header_buf + p->session_offset),
            p->tx_session
        );
    if (p->cookie) {
        if (p->cookie_is_64) {
            stq_be_p(
                (uint64_t *)(s->header_buf + p->cookie_offset),
                p->tx_cookie
            );
        } else {
            stl_be_p(
                (uint32_t *) (s->header_buf + p->cookie_offset),
                p->tx_cookie
            );
        }
    }
    if (p->has_counter) {
        counter = (uint32_t *)(s->header_buf + p->counter_offset);
        if (p->pin_counter) {
            *counter = 0;
        } else {
            stl_be_p(counter, ++p->counter);
        }
    }
}


static int l2tpv3_verify_header(void *us, uint8_t *buf)
{

    NetUnifiedState *s = (NetUnifiedState *) us;
    L2TPV3TunnelParams *p = (L2TPV3TunnelParams *) s->params;
    uint32_t *session;
    uint64_t cookie;

    if ((!p->udp) && (!p->ipv6)) {
        buf += sizeof(struct iphdr) /* fix for ipv4 raw */;
    }

    /* we do not do a strict check for "data" packets as per
    * the RFC spec because the pure IP spec does not have
    * that anyway.
    */

    if (p->cookie) {
        if (p->cookie_is_64) {
            cookie = ldq_be_p(buf + p->cookie_offset);
        } else {
            cookie = ldl_be_p(buf + p->cookie_offset) & 0xffffffffULL;
        }
        if (cookie != p->rx_cookie) {
            if (!s->header_mismatch) {
                error_report("unknown cookie id");
            }
            return -1;
        }
    }
    session = (uint32_t *) (buf + p->session_offset);
    if (ldl_be_p(session) != p->rx_session) {
        if (!s->header_mismatch) {
            error_report("session mismatch");
        }
        return -1;
    }
    return 0;
}

int net_init_l2tpv3(const Netdev *netdev,
                    const char *name,
                    NetClientState *peer, Error **errp)
{
    /* FIXME error_setg(errp, ...) on failure */
    const NetdevL2TPv3Options *l2tpv3;
    NetUnifiedState *s;
    NetClientState *nc;
    L2TPV3TunnelParams *p;

    int fd = -1, gairet;
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    char *srcport, *dstport;

    nc = qemu_new_unified_net_client(name, peer);

    s = DO_UPCAST(NetUnifiedState, nc, nc);

    p = g_malloc(sizeof(L2TPV3TunnelParams));

    s->params = p;

    s->form_header = &l2tpv3_form_header;
    s->verify_header = &l2tpv3_verify_header;
    s->queue_head = 0;
    s->queue_tail = 0;
    s->header_mismatch = false;

    assert(netdev->type == NET_CLIENT_DRIVER_L2TPV3);
    l2tpv3 = &netdev->u.l2tpv3;

    if (l2tpv3->has_ipv6 && l2tpv3->ipv6) {
        p->ipv6 = l2tpv3->ipv6;
    } else {
        p->ipv6 = false;
    }

    if ((l2tpv3->has_offset) && (l2tpv3->offset > 256)) {
        error_report("l2tpv3_open : offset must be less than 256 bytes");
        goto outerr;
    }

    if (l2tpv3->has_rxcookie || l2tpv3->has_txcookie) {
        if (l2tpv3->has_rxcookie && l2tpv3->has_txcookie) {
            p->cookie = true;
        } else {
            goto outerr;
        }
    } else {
        p->cookie = false;
    }

    if (l2tpv3->has_cookie64 || l2tpv3->cookie64) {
        p->cookie_is_64  = true;
    } else {
        p->cookie_is_64  = false;
    }

    if (l2tpv3->has_udp && l2tpv3->udp) {
        p->udp = true;
        if (!(l2tpv3->has_srcport && l2tpv3->has_dstport)) {
            error_report("l2tpv3_open : need both src and dst port for udp");
            goto outerr;
        } else {
            srcport = l2tpv3->srcport;
            dstport = l2tpv3->dstport;
        }
    } else {
        p->udp = false;
        srcport = NULL;
        dstport = NULL;
    }


    s->offset = 4;
    p->session_offset = 0;
    p->cookie_offset = 4;
    p->counter_offset = 4;

    p->tx_session = l2tpv3->txsession;
    if (l2tpv3->has_rxsession) {
        p->rx_session = l2tpv3->rxsession;
    } else {
        p->rx_session = p->tx_session;
    }

    if (p->cookie) {
        p->rx_cookie = l2tpv3->rxcookie;
        p->tx_cookie = l2tpv3->txcookie;
        if (p->cookie_is_64 == true) {
            /* 64 bit cookie */
            s->offset += 8;
            p->counter_offset += 8;
        } else {
            /* 32 bit cookie */
            s->offset += 4;
            p->counter_offset += 4;
        }
    }

    memset(&hints, 0, sizeof(hints));

    if (p->ipv6) {
        hints.ai_family = AF_INET6;
    } else {
        hints.ai_family = AF_INET;
    }
    if (p->udp) {
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = 0;
        s->offset += 4;
        p->counter_offset += 4;
        p->session_offset += 4;
        p->cookie_offset += 4;
    } else {
        hints.ai_socktype = SOCK_RAW;
        hints.ai_protocol = IPPROTO_L2TP;
    }

    gairet = getaddrinfo(l2tpv3->src, srcport, &hints, &result);

    if ((gairet != 0) || (result == NULL)) {
        error_report(
            "l2tpv3_open : could not resolve src, errno = %s",
            gai_strerror(gairet)
        );
        goto outerr;
    }
    fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (fd == -1) {
        fd = -errno;
        error_report("l2tpv3_open : socket creation failed, errno = %d", -fd);
        goto outerr;
    }
    if (bind(fd, (struct sockaddr *) result->ai_addr, result->ai_addrlen)) {
        error_report("l2tpv3_open :  could not bind socket err=%i", errno);
        goto outerr;
    }
    if (result) {
        freeaddrinfo(result);
    }

    memset(&hints, 0, sizeof(hints));

    if (p->ipv6) {
        hints.ai_family = AF_INET6;
    } else {
        hints.ai_family = AF_INET;
    }
    if (p->udp) {
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = 0;
    } else {
        hints.ai_socktype = SOCK_RAW;
        hints.ai_protocol = IPPROTO_L2TP;
    }

    result = NULL;
    gairet = getaddrinfo(l2tpv3->dst, dstport, &hints, &result);
    if ((gairet != 0) || (result == NULL)) {
        error_report(
            "l2tpv3_open : could not resolve dst, error = %s",
            gai_strerror(gairet)
        );
        goto outerr;
    }

    s->dgram_dst = g_new0(struct sockaddr_storage, 1);
    memcpy(s->dgram_dst, result->ai_addr, result->ai_addrlen);
    s->dst_size = result->ai_addrlen;

    if (result) {
        freeaddrinfo(result);
    }

    if (l2tpv3->has_counter && l2tpv3->counter) {
        p->has_counter = true;
        s->offset += 4;
    } else {
        p->has_counter = false;
    }

    if (l2tpv3->has_pincounter && l2tpv3->pincounter) {
        p->has_counter = true;  /* pin counter implies that there is counter */
        p->pin_counter = true;
    } else {
        p->pin_counter = false;
    }

    if (l2tpv3->has_offset) {
        /* extra offset */
        s->offset += l2tpv3->offset;
    }

    if ((p->ipv6) || (p->udp)) {
        s->header_size = s->offset;
    } else {
        s->header_size = s->offset + sizeof(struct iphdr);
    }

    qemu_net_finalize_unified_init(s, fd);
    p->counter = 0;

    snprintf(s->nc.info_str, sizeof(s->nc.info_str),
             "l2tpv3: connected");
    return 0;
outerr:
    qemu_del_net_client(nc);
    if (fd >= 0) {
        close(fd);
    }
    if (result) {
        freeaddrinfo(result);
    }
    return -1;
}

