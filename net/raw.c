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
#include <sys/ioctl.h>
#include <net/if.h>
#include "net/net.h"
 #include <sys/socket.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include "clients.h"
#include "qemu-common.h"
#include "qemu/error-report.h"
#include "qemu/option.h"
#include "qemu/sockets.h"
#include "qemu/iov.h"
#include "qemu/main-loop.h"
#include "unified.h"

static int noop(void *us, uint8_t *buf)
{
    return 0;
}

int net_init_raw(const Netdev *netdev,
                    const char *name,
                    NetClientState *peer, Error **errp)
{

    const NetdevRawOptions *raw;
    NetUnifiedState *s;
    NetClientState *nc;

    int fd = -1;
    int err;

    struct ifreq ifr;
    struct sockaddr_ll sock;


    nc = qemu_new_unified_net_client(name, peer);

    s = DO_UPCAST(NetUnifiedState, nc, nc);

    fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (fd == -1) {
        err = -errno;
        error_report("raw_open : raw socket creation failed, errno = %d", -err);
        goto outerr;
    }


    s->form_header = NULL;
    s->verify_header = &noop;
    s->queue_head = 0;
    s->queue_tail = 0;
    s->header_mismatch = false;
    s->dgram_dst = NULL;
    s->dst_size = 0;

    assert(netdev->type == NET_CLIENT_DRIVER_RAW);
    raw = &netdev->u.raw;

    memset(&ifr, 0, sizeof(struct ifreq));
    strncpy((char *) &ifr.ifr_name, raw->ifname, sizeof(ifr.ifr_name) - 1);

    if (ioctl(fd, SIOCGIFINDEX, (void *) &ifr) < 0) {
        err = -errno;
        error_report("SIOCGIFINDEX, failed to get raw interface index for %s",
            raw->ifname);
        goto outerr;
    }

    sock.sll_family = AF_PACKET;
    sock.sll_protocol = htons(ETH_P_ALL);
    sock.sll_ifindex = ifr.ifr_ifindex;

    if (bind(fd, (struct sockaddr *) &sock, sizeof(struct sockaddr_ll)) < 0) {
        error_report("raw: failed to bind raw socket");
        err = -errno;
        goto outerr;
    }

    s->offset = 0;

    qemu_net_finalize_unified_init(s, fd);

    snprintf(s->nc.info_str, sizeof(s->nc.info_str),
             "raw: connected");
    return 0;
outerr:
    qemu_del_net_client(nc);
    if (fd >= 0) {
        close(fd);
    }
    return -1;
}

