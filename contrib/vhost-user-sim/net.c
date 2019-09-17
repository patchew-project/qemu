/*
 * vhost-user sim network device
 *
 * Copyright (c) 2017 Intel Corporation. All rights reserved.
 * Copyright (c) 2019 Intel Corporation. All rights reserved.
 *
 * Author:
 *  Johannes Berg <johannes.berg@intel.com>
 *
 * This work is based on the "vhost-user-blk" sample code by
 * Changpeng Liu <changpeng.liu@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 only.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "contrib/libvhost-user/libvhost-user-glib.h"
#include "contrib/libvhost-user/libvhost-user.h"
#include "qemu/iov.h"
#include <gmodule.h>
#include "main.h"
#include "cal.h"

static unsigned int clients;
static GList *netdevs;
G_LOCK_DEFINE(net);

typedef struct VuNetDev {
    SimCalendarEntry entry;
    VugDev parent;
    VuVirtq *rxq, *txq;
    GSequenceIter *iter;
    int idx;
} VuNetDev;

typedef struct VuNetPacket {
    SimCalendarEntry entry;
    void *transmitter;
    int txidx;
    unsigned int len;
    char buf[];
} VuNetPacket;

static void vu_net_panic_cb(VuDev *vu_dev, const char *buf)
{
    if (buf) {
        g_warning("vu_net_panic_cb: %s", buf);
    }
}

static void send_to_one(VuNetDev *ndev, VuNetPacket *pkt)
{
    VuVirtqElement *elem;
    VuDev *vu_dev = &ndev->parent.parent;

    elem = vu_queue_pop(vu_dev, ndev->rxq, sizeof(VuVirtqElement));
    if (!elem) {
        /* no space on this device, drop the frame for it */
        fprintf(stderr, "dropped packet to net %d!\n", ndev->idx);
        return;
    }
    fprintf(stderr, "forwarding packet to net %d\n", ndev->idx);
    g_assert(elem->in_num && !elem->out_num);
    iov_from_buf(elem->in_sg, elem->in_num, 0, pkt->buf, pkt->len);

    vu_queue_push(vu_dev, ndev->rxq, elem, pkt->len);
    /* this _sync is key so the recipient can request scheduler time */
    vu_queue_notify_sync(vu_dev, ndev->rxq);

    free(elem);
}

static void vu_netpkt_calendar_cb(SimCalendarEntry *entry)
{
    VuNetPacket *pkt = container_of(entry, VuNetPacket, entry);
    /* send to all devices */
    GList *l;

    G_LOCK(net);
    for (l = netdevs; l; l = l->next) {
        if (l->data != pkt->transmitter) {
            send_to_one(l->data, pkt);
        }
    }
    G_UNLOCK(net);

    calendar_run_done(&pkt->entry);
    g_free((void *)pkt->entry.name);
    g_free(pkt);
}

static int vu_net_virtio_process_pkt(VuNetDev *ndev)
{
    VuVirtq *vq = ndev->txq;
    VugDev *gdev = &ndev->parent;
    VuDev *vu_dev = &gdev->parent;
    VuVirtqElement *elem;
    VuNetPacket *pkt;
    ssize_t sz;

    elem = vu_queue_pop(vu_dev, vq, sizeof(VuVirtqElement));
    if (!elem) {
        return -1;
    }

    g_assert(elem->out_num);

    sz = iov_size(elem->out_sg, elem->out_num);
    pkt = g_malloc(sizeof(*pkt) + sz);
    pkt->entry.time = calendar_get_time() + 50 * 1000 * 1000; /* 50ms */
    pkt->entry.callback = vu_netpkt_calendar_cb;
    pkt->entry.name = g_strdup_printf("packet from %d", ndev->idx);
    pkt->len = sz;
    pkt->transmitter = ndev;
    pkt->txidx = ndev->idx;
    iov_to_buf(elem->out_sg, elem->out_num, 0, pkt->buf, sz);
    calendar_entry_add(&pkt->entry);

    vu_queue_push(vu_dev, vq, elem, 0);
    /* the reclaim interrupt should also be predictable, so _sync() */
    vu_queue_notify_sync(vu_dev, vq);

    return 0;
}

static void vu_netdev_calendar_cb(SimCalendarEntry *entry)
{
    VuNetDev *ndev = container_of(entry, VuNetDev, entry);
    int ret;

    G_LOCK(net);
    fprintf(stderr, "NET: handle TX IRQ from net %d\n", ndev->idx);
    /* here we handle TX from the VQ */
    do {
        ret = vu_net_virtio_process_pkt(ndev);
    } while (!ret);
    fprintf(stderr, "NET: handle TX from net %d completed\n", ndev->idx);
    G_UNLOCK(net);

    calendar_run_done(entry);
}

static uint64_t vu_net_get_protocol_features(VuDev *dev)
{
    return 1ULL << VHOST_USER_PROTOCOL_F_INBAND_NOTIFICATIONS;
}

static void vu_net_process_vq(VuDev *vu_dev, int idx)
{
    VugDev *gdev = container_of(vu_dev, VugDev, parent);
    VuNetDev *ndev = container_of(gdev, VuNetDev, parent);

    assert(vu_dev && ndev);

    fprintf(stderr, "insert IRQ from %s at %lld\n", ndev->entry.name, calendar_get_time());

    /* insert the calendar entry to handle the interrupt */
    calendar_entry_add_unless_present(&ndev->entry, calendar_get_time());
}

static void vu_net_queue_set_started(VuDev *vu_dev, int idx, bool started)
{
    VugDev *gdev = container_of(vu_dev, VugDev, parent);
    VuNetDev *ndev = container_of(gdev, VuNetDev, parent);
    VuVirtq *vq;

    assert(vu_dev);

    vq = vu_get_queue(vu_dev, idx);
    /* set up the read fd */
    switch (idx) {
    case 0:
        ndev->rxq = vq;
        break;
    case 1:
        ndev->txq = vq;
        vu_set_queue_handler(vu_dev, vq, started ? vu_net_process_vq : NULL);
        break;
    }
}

static int
vu_net_process_msg(VuDev *vu_dev, VhostUserMsg *msg, int *do_reply)
{
    VugDev *gdev = container_of(vu_dev, VugDev, parent);
    VuNetDev *ndev = container_of(gdev, VuNetDev, parent);

    if (msg->request != VHOST_USER_NONE) {
        return 0;
    }

    *do_reply = 0;
    fprintf(stderr, "net client %d disconnected\n", ndev->idx);

    G_LOCK(net);
    netdevs = g_list_remove(netdevs, ndev);
    clients--;

    vug_deinit(gdev);
    G_UNLOCK(net);
    calendar_entry_destroy(&ndev->entry);
    g_free((void *)ndev->entry.name);
    g_free(ndev);

    return 1;
}

static const VuDevIface vu_net_iface = {
    .queue_set_started = vu_net_queue_set_started,
    .get_protocol_features = vu_net_get_protocol_features,
    .process_msg = vu_net_process_msg,
};

gboolean vu_net_client_connected(GIOChannel *src,
                                 GIOCondition cond,
                                 gpointer data)
{
    int lsock = g_io_channel_unix_get_fd(src);
    int csock = accept(lsock, NULL, NULL);
    VuNetDev *ndev;

    if (csock < 0) {
        fprintf(stderr, "Accept error %s\n", strerror(errno));
        return TRUE;
    }

    ndev = g_new0(VuNetDev, 1);
    if (!ndev) {
        return TRUE;
    }

    clients++;
    ndev->idx = clients;
    ndev->entry.name = g_strdup_printf("net-irq %d", clients);
    ndev->entry.callback = vu_netdev_calendar_cb;
    vug_init(&ndev->parent, 2, csock, vu_net_panic_cb,
             &vu_net_iface);
    fprintf(stderr, "net client %d connected\n", clients);

    G_LOCK(net);
    netdevs = g_list_prepend(netdevs, ndev);
    G_UNLOCK(net);

    return TRUE;
}
