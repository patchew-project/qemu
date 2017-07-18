/*
 * QEMU System Emulator
 *
 * Copyright (c) 2015-2017 Cambridge GREys Limited
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

/* IANA-assigned IP protocol ID for GRE */


#ifndef IPPROTO_GRE
#define IPPROTO_GRE 0x2F
#endif

#define GRE_MODE_CHECKSUM     htons(8 << 12)   /* checksum */
#define GRE_MODE_RESERVED     htons(4 << 12)   /* unused */
#define GRE_MODE_KEY          htons(2 << 12)   /* KEY present */
#define GRE_MODE_SEQUENCE     htons(1 << 12)   /* no sequence */


/* GRE TYPE for Ethernet in GRE aka GRETAP */

#define GRE_IRB htons(0x6558)

struct gre_minimal_header {
   uint16_t header;
   uint16_t arptype;
};

typedef struct GRETunnelParams {
    /*
     * GRE parameters
     */

    uint32_t rx_key;
    uint32_t tx_key;
    uint32_t sequence;

    /* Flags */

    bool ipv6;
    bool udp;
    bool has_sequence;
    bool pin_sequence;
    bool checksum;
    bool key;

    /* Precomputed GRE specific offsets */

    uint32_t key_offset;
    uint32_t sequence_offset;
    uint32_t checksum_offset;

    struct gre_minimal_header header_bits;

} GRETunnelParams;



static void gre_form_header(void *us)
{
    NetUnifiedState *s = (NetUnifiedState *) us;
    GRETunnelParams *p = (GRETunnelParams *) s->params;

    uint32_t *sequence;

    *((uint32_t *) s->header_buf) = *((uint32_t *) &p->header_bits);

    if (p->key) {
        stl_be_p(
            (uint32_t *) (s->header_buf + p->key_offset),
            p->tx_key
        );
    }
    if (p->has_sequence) {
        sequence = (uint32_t *)(s->header_buf + p->sequence_offset);
        if (p->pin_sequence) {
            *sequence = 0;
        } else {
            stl_be_p(sequence, ++p->sequence);
        }
    }
}

static int gre_verify_header(void *us, uint8_t *buf)
{

    NetUnifiedState *s = (NetUnifiedState *) us;
    GRETunnelParams *p = (GRETunnelParams *) s->params;
    uint32_t key;


    if (!p->ipv6) {
        buf += sizeof(struct iphdr) /* fix for ipv4 raw */;
    }

    if (*((uint32_t *) buf) != *((uint32_t *) &p->header_bits)) {
        if (!s->header_mismatch) {
            error_report("header type disagreement, expecting %0x, got %0x",
                *((uint32_t *) &p->header_bits), *((uint32_t *) buf));
        }
        return -1;
    }

    if (p->key) {
        key = ldl_be_p(buf + p->key_offset);
        if (key != p->rx_key) {
            if (!s->header_mismatch) {
                error_report("unknown key id %0x, expecting %0x",
                    key, p->rx_key);
            }
            return -1;
        }
    }
    return 0;
}

int net_init_gre(const Netdev *netdev,
                    const char *name,
                    NetClientState *peer, Error **errp)
{
    /* FIXME error_setg(errp, ...) on failure */
    const NetdevGREOptions *gre;
    NetUnifiedState *s;
    NetClientState *nc;
    GRETunnelParams *p;

    int fd = -1, gairet;
    struct addrinfo hints;
    struct addrinfo *result = NULL;

    nc = qemu_new_unified_net_client(name, peer);

    s = DO_UPCAST(NetUnifiedState, nc, nc);

    p = g_malloc(sizeof(GRETunnelParams));

    s->params = p;
    p->header_bits.arptype = GRE_IRB;
    p->header_bits.header = 0;

    s->form_header = &gre_form_header;
    s->verify_header = &gre_verify_header;
    s->queue_head = 0;
    s->queue_tail = 0;
    s->header_mismatch = false;

    assert(netdev->type == NET_CLIENT_DRIVER_GRE);
    gre = &netdev->u.gre;

    if (gre->has_ipv6 && gre->ipv6) {
        p->ipv6 = gre->ipv6;
    } else {
        p->ipv6 = false;
    }

    s->offset = 4;
    p->key_offset = 4;
    p->sequence_offset = 4;
    p->checksum_offset = 4;

    if (gre->has_rxkey || gre->has_txkey) {
        if (gre->has_rxkey && gre->has_txkey) {
            p->key = true;
            p->header_bits.header |= GRE_MODE_KEY;
        } else {
            goto outerr;
        }
    } else {
        p->key = false;
    }

    if (p->key) {
        p->rx_key = gre->rxkey;
        p->tx_key = gre->txkey;
        s->offset += 4;
        p->sequence_offset += 4;
    }


    if (gre->has_sequence && gre->sequence) {
        s->offset += 4;
        p->has_sequence = true;
        p->header_bits.header |= GRE_MODE_SEQUENCE;
    } else {
        p->sequence = false;
    }

    if (gre->has_pinsequence && gre->pinsequence) {
        /* pin sequence implies that there is sequence */
        p->has_sequence = true;
        p->pin_sequence = true;
    } else {
        p->pin_sequence = false;
    }

    memset(&hints, 0, sizeof(hints));

    if (p->ipv6) {
        hints.ai_family = AF_INET6;
    } else {
        hints.ai_family = AF_INET;
    }

    hints.ai_socktype = SOCK_RAW;
    hints.ai_protocol = IPPROTO_GRE;

    gairet = getaddrinfo(gre->src, NULL, &hints, &result);

    if ((gairet != 0) || (result == NULL)) {
        error_report(
            "gre_open : could not resolve src, errno = %s",
            gai_strerror(gairet)
        );
        goto outerr;
    }
    fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (fd == -1) {
        fd = -errno;
        error_report("gre_open : socket creation failed, errno = %d", -fd);
        goto outerr;
    }
    if (bind(fd, (struct sockaddr *) result->ai_addr, result->ai_addrlen)) {
        error_report("gre_open :  could not bind socket err=%i", errno);
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
    hints.ai_socktype = SOCK_RAW;
    hints.ai_protocol = IPPROTO_GRE;

    result = NULL;
    gairet = getaddrinfo(gre->dst, NULL, &hints, &result);
    if ((gairet != 0) || (result == NULL)) {
        error_report(
            "gre_open : could not resolve dst, error = %s",
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

    if ((p->ipv6) || (p->udp)) {
        s->header_size = s->offset;
    } else {
        s->header_size = s->offset + sizeof(struct iphdr);
    }

    qemu_net_finalize_unified_init(s, fd);

    p->sequence = 0;

    snprintf(s->nc.info_str, sizeof(s->nc.info_str),
             "gre: connected");
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

