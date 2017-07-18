/*
 * QEMU System Emulator
 *
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


/* vxlan header RRRRIRRR in top byte, 3 bytes reserved */
#define HEADER_RESERVED 24
#define VNID_RESERVED 8
#define VXLAN_BIT 3
#define VXLAN_DATA_PACKET (1 << (HEADER_RESERVED + VXLAN_BIT))
#define VNID_OFFSET 4
#define VXLAN_HEADER_SIZE 8

typedef struct VXLANTunnelParams {

    /* Rather skimpy - VXLAN is very simple at present */

    uint32_t vnid;

} VXLANTunnelParams;



static void vxlan_form_header(void *us)
{
    NetUnifiedState *s = (NetUnifiedState *) us;
    VXLANTunnelParams *p = (VXLANTunnelParams *) s->params;

    stl_be_p((uint32_t *) s->header_buf, VXLAN_DATA_PACKET);
    stl_be_p(
            (uint32_t *) (s->header_buf + VNID_OFFSET),
            p->vnid
        );
}
static int vxlan_verify_header(void *us, uint8_t *buf)
{

    NetUnifiedState *s = (NetUnifiedState *) us;
    VXLANTunnelParams *p = (VXLANTunnelParams *) s->params;
    uint32_t header;
    uint32_t vnid;

    header = ldl_be_p(buf);
    if ((header & VXLAN_DATA_PACKET) == 0) {
        if (!s->header_mismatch) {
            error_report(
                "header type disagreement, expecting %0x, got %0x",
                VXLAN_DATA_PACKET, header
            );
        }
        return -1;
    }

    vnid = ldl_be_p(buf + VNID_OFFSET);
    if (vnid != p->vnid) {
        if (!s->header_mismatch) {
            error_report("unknown vnid id %0x, expecting %0x", vnid, p->vnid);
        }
        return -1;
    }
    return 0;
}

int net_init_vxlan(const Netdev *netdev,
                    const char *name,
                    NetClientState *peer, Error **errp)
{
    /* FIXME error_setg(errp, ...) on failure */
    const NetdevVXLANOptions *vxlan;
    NetUnifiedState *s;
    NetClientState *nc;
    VXLANTunnelParams *p;

    int fd = -1, gairet;
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    const char *default_port = "4789";
    const char *srcport;
    const char *dstport;

    nc = qemu_new_unified_net_client(name, peer);

    s = DO_UPCAST(NetUnifiedState, nc, nc);

    p = g_malloc(sizeof(VXLANTunnelParams));

    s->params = p;

    s->form_header = &vxlan_form_header;
    s->verify_header = &vxlan_verify_header;
    s->queue_head = 0;
    s->queue_tail = 0;
    s->header_mismatch = false;
    s->header_size = VXLAN_HEADER_SIZE;

    assert(netdev->type == NET_CLIENT_DRIVER_VXLAN);
    vxlan = &netdev->u.vxlan;

    if (vxlan->has_srcport) {
        srcport = vxlan->srcport;
    } else {
        srcport = default_port;
    }

    if (vxlan->has_dstport) {
        dstport = vxlan->dstport;
    } else {
        dstport = default_port;
    }
    s->offset = VXLAN_HEADER_SIZE;

    /* we store it shifted to the correct position, so we do not need
     * to recompute it each time
     */

    p->vnid = vxlan->vnid << VNID_RESERVED;

    memset(&hints, 0, sizeof(hints));

    if (vxlan->has_ipv6) {
        hints.ai_family = AF_INET6;
    } else {
        hints.ai_family = AF_INET;
    }

    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = 0;

    gairet = getaddrinfo(vxlan->src, srcport, &hints, &result);

    if ((gairet != 0) || (result == NULL)) {
        error_report(
            "vxlan_open : could not resolve src, errno = %s",
            gai_strerror(gairet)
        );
        goto outerr;
    }
    fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (fd == -1) {
        fd = -errno;
        error_report("vxlan_open : socket creation failed, errno = %d", -fd);
        goto outerr;
    }
    if (bind(fd, (struct sockaddr *) result->ai_addr, result->ai_addrlen)) {
        error_report("vxlan_open :  could not bind socket err=%i", errno);
        goto outerr;
    }
    if (result) {
        freeaddrinfo(result);
    }

    memset(&hints, 0, sizeof(hints));

    if (vxlan->has_ipv6) {
        hints.ai_family = AF_INET6;
    } else {
        hints.ai_family = AF_INET;
    }
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = 0;

    result = NULL;
    gairet = getaddrinfo(vxlan->dst, dstport, &hints, &result);
    if ((gairet != 0) || (result == NULL)) {
        error_report(
            "vxlan_open : could not resolve dst, error = %s",
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

    qemu_net_finalize_unified_init(s, fd);

    snprintf(s->nc.info_str, sizeof(s->nc.info_str),
             "vxlan: connected");
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

