/*
 * QEMU Hyper-V VMBus
 *
 * Copyright (c) 2017-2018 Virtuozzo International GmbH.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/vmbus/vmbus.h"
#include "hw/sysbus.h"
#include "hw/loader.h"
#include "trace.h"

#define TYPE_VMBUS "vmbus"
#define VMBUS(obj) OBJECT_CHECK(VMBus, (obj), TYPE_VMBUS)

#define TYPE_VMBUS_BRIDGE "vmbus-bridge"

#define VMBUS_CHAN_RELID_COUNT      HV_EVENT_FLAGS_COUNT
#define VMBUS_CHAN_FIRST_RELID      1 /* 0 is reserved */
#define VMBUS_CHAN_CONNID_COUNT     HV_EVENT_FLAGS_COUNT
#define VMBUS_CHAN_FIRST_CONNID     (VMBUS_MONITOR_CONNECTION_ID + 1)

#define VMBUS_VMSTATE_VERSION_ID    1

#define VMBUS_RX_QUEUE_CAPACITY     16

typedef struct VMBusGpadl
{
    QTAILQ_ENTRY(VMBusGpadl) link;

    uint32_t id;
    uint32_t child_relid;

    uint32_t num_gfns;
    uint32_t seen_gfns;
    uint64_t *gfns;

    bool alive;
    bool in_use;
} VMBusGpadl;

typedef enum VMBusOfferState {
    VMOFFER_INIT,
    VMOFFER_SENDING,
    VMOFFER_SENT,
} VMBusOfferState;

typedef enum VMBusChannelState {
    VMCHAN_INIT,
    VMCHAN_OPENING,
    VMCHAN_OPEN,
} VMBusChannelState;


typedef struct VMBusRingBuf {
    VMBusGpadl *gpadl;
    AddressSpace *as;
    DMADirection dir;
    dma_addr_t rb_addr;
    uint32_t base;
    uint32_t len;
    uint32_t my_idx;
    uint32_t pending_sz;
} VMBusRingBuf;

typedef struct VMBusChannel
{
    VMBusDevice *dev;

    uint32_t relid;
    uint16_t subchan_idx;
    uint32_t connid;
    uint32_t open_id;
    uint32_t target_vp;
    uint32_t rb_gpadl;
    uint32_t rb_rcv_offset;

    VMBusOfferState offer_state;
    VMBusChannelState state;

    VMBusChannelNotifyCb notify_cb;
    EventNotifier notifier;

    VMBus *vmbus;
    HvSintRoute *notify_route;
    VMBusGpadl *gpadl;

    VMBusRingBuf ringbuf_rcv;
    VMBusRingBuf ringbuf_snd;

    uint32_t snd_reserved;

    QTAILQ_ENTRY(VMBusChannel) link;
} VMBusChannel;

typedef enum VMBusState {
    VMBUS_LISTEN,
    VMBUS_HANDSHAKE,
    VMBUS_OFFER,
    VMBUS_CREATE_GPADL,
    VMBUS_TEARDOWN_GPADL,
    VMBUS_OPEN_CHANNEL,
    VMBUS_UNLOAD,
    VMBUS_STATE_MAX
} VMBusState;

typedef struct VMBus
{
    BusState parent;

    VMBusState state;
    bool msg_in_progress;
    uint32_t version;
    uint32_t target_vp;
    HvSintRoute *sint_route;
    hwaddr int_page_gpa;

    DECLARE_BITMAP(chan_relid_bitmap, VMBUS_CHAN_RELID_COUNT);
    DECLARE_BITMAP(connection_id_bitmap, VMBUS_CHAN_CONNID_COUNT);

    struct hyperv_post_message_input rx_queue[VMBUS_RX_QUEUE_CAPACITY];
    uint8_t rx_queue_head;
    uint8_t rx_queue_size;
    QemuMutex rx_queue_lock;

    QTAILQ_HEAD(, VMBusGpadl) gpadl_list;
    QTAILQ_HEAD(, VMBusChannel) channel_list;

    EventNotifier notifier;
} VMBus;

static void vmbus_resched(VMBus *vmbus);
static void vmbus_msg_cb(void *data, int status);

static bool gpadl_full(VMBusGpadl *gpadl)
{
    return gpadl->seen_gfns == gpadl->num_gfns;
}

static bool gpadl_broken(VMBusGpadl *gpadl)
{
    return !gpadl->num_gfns;
}

static VMBusGpadl *create_gpadl(VMBus *vmbus, uint32_t id,
                                uint32_t child_relid, uint32_t num_gfns)
{
    VMBusGpadl *gpadl = g_new0(VMBusGpadl, 1);

    gpadl->id = id;
    gpadl->child_relid = child_relid;
    gpadl->num_gfns = num_gfns;
    gpadl->gfns = g_new(uint64_t, num_gfns);
    QTAILQ_INSERT_HEAD(&vmbus->gpadl_list, gpadl, link);
    return gpadl;
}

static void free_gpadl(VMBus *vmbus, VMBusGpadl *gpadl)
{
    QTAILQ_REMOVE(&vmbus->gpadl_list, gpadl, link);
    g_free(gpadl->gfns);
    g_free(gpadl);
}

static VMBusGpadl *find_gpadl(VMBus *vmbus, uint32_t gpadl_id)
{
    VMBusGpadl *gpadl;
    QTAILQ_FOREACH(gpadl, &vmbus->gpadl_list, link) {
        if (gpadl->id == gpadl_id) {
            return gpadl;
        }
    }
    return NULL;
}

VMBusGpadl *vmbus_get_gpadl(VMBusChannel *chan, uint32_t gpadl_id)
{
    VMBusGpadl *gpadl = find_gpadl(chan->vmbus, gpadl_id);
    if (gpadl) {
        if (!gpadl->alive || gpadl->in_use) {
            return NULL;
        }
        gpadl->in_use = true;
    }
    return gpadl;
}

void vmbus_put_gpadl(VMBusGpadl *gpadl)
{
    gpadl->in_use = false;
}

uint32_t vmbus_gpadl_len(VMBusGpadl *gpadl)
{
    return gpadl->num_gfns * TARGET_PAGE_SIZE;
}

typedef struct GpadlIter {
    VMBusGpadl *gpadl;
    AddressSpace *as;
    DMADirection dir;
    uint32_t off;
    /* cached mapping of the currently accessed page, up to page boundary */
    void *map;
} GpadlIter;

static void gpadl_iter_init(GpadlIter *iter, VMBusGpadl *gpadl,
                            AddressSpace *as, DMADirection dir, uint32_t off)
{
    iter->gpadl = gpadl;
    iter->as = as;
    iter->dir = dir;
    iter->off = off;
    iter->map = NULL;
}

static ssize_t gpadl_iter_rw(GpadlIter *iter, void *buf, uint32_t len)
{
    ssize_t ret = len;

    while (len) {
        uint32_t off_pgoff = iter->off & ~TARGET_PAGE_MASK;
        uint32_t pgleft = TARGET_PAGE_SIZE - off_pgoff;
        uint32_t cplen = MIN(pgleft, len);
        void *p;

        if (!iter->map) {
            dma_addr_t maddr;
            dma_addr_t mlen = pgleft;
            uint32_t idx = iter->off >> TARGET_PAGE_BITS;
            assert(idx < iter->gpadl->num_gfns);

            maddr = (iter->gpadl->gfns[idx] << TARGET_PAGE_BITS) | off_pgoff;

            iter->map = dma_memory_map(iter->as, maddr, &mlen, iter->dir);
            if (mlen != pgleft) {
                dma_memory_unmap(iter->as, iter->map, mlen, iter->dir, 0);
                return -EFAULT;
            }
        }

        p = (void *)(((uintptr_t)iter->map & TARGET_PAGE_MASK) | off_pgoff);
        if (iter->dir == DMA_DIRECTION_FROM_DEVICE) {
            memcpy(p, buf, cplen);
        } else {
            memcpy(buf, p, cplen);
        }

        buf += cplen;
        len -= cplen;
        iter->off += cplen;
        if (cplen == pgleft) {
            uint32_t mlen = TARGET_PAGE_SIZE -
                ((uintptr_t)iter->map & ~TARGET_PAGE_MASK);
            dma_memory_unmap(iter->as, iter->map, mlen, iter->dir, mlen);
            iter->map = NULL;
        }
    }

    return ret;
}

static void gpadl_iter_done(GpadlIter *iter)
{
    if (iter->map) {
        uint32_t map_pgoff = (uintptr_t)iter->map & ~TARGET_PAGE_MASK;
        uint32_t off_pgoff = iter->off & ~TARGET_PAGE_MASK;
        assert(map_pgoff <= off_pgoff);
        dma_memory_unmap(iter->as, iter->map, TARGET_PAGE_SIZE - map_pgoff,
                         iter->dir, off_pgoff - map_pgoff);
    }
}

static void gpadl_iter_set(GpadlIter *iter, uint32_t new_off)
{
    if (iter->map) {
        uint32_t map_pgoff = (uintptr_t)iter->map & ~TARGET_PAGE_MASK;
        uint32_t off_pgoff = iter->off & ~TARGET_PAGE_MASK;
        if ((iter->off & TARGET_PAGE_MASK) != (new_off & TARGET_PAGE_MASK) ||
            (new_off & ~TARGET_PAGE_MASK) < map_pgoff) {
            dma_memory_unmap(iter->as, iter->map, TARGET_PAGE_SIZE - map_pgoff,
                             iter->dir, off_pgoff - map_pgoff);
            iter->map = NULL;
        }
    }
    iter->off = new_off;
}

ssize_t vmbus_iov_to_gpadl(VMBusChannel *chan, VMBusGpadl *gpadl, uint32_t off,
                           const struct iovec *iov, size_t iov_cnt)
{
    GpadlIter iter;
    size_t i;
    ssize_t ret = 0;

    gpadl_iter_init(&iter, gpadl, chan->dev->dma_as,
                    DMA_DIRECTION_FROM_DEVICE, off);
    for (i = 0; i < iov_cnt; i++) {
        ret = gpadl_iter_rw(&iter, iov[i].iov_base, iov[i].iov_len);
        if (ret < 0) {
            goto out;
        }
    }
out:
    gpadl_iter_done(&iter);
    return ret;
}

int vmbus_map_sgl(QEMUSGList *sgl, DMADirection dir, struct iovec *iov,
                  unsigned iov_cnt, size_t len, size_t off)
{
    int ret_cnt = 0, ret;
    unsigned i;
    ScatterGatherEntry *sg = sgl->sg;

    for (i = 0; i < sgl->nsg; i++) {
        if (sg[i].len > off) {
            break;
        }
        off -= sg[i].len;
    }
    for (; len && i < sgl->nsg; i++) {
        dma_addr_t mlen = MIN(sg[i].len - off, len);
        dma_addr_t addr = sg[i].base + off;
        len -= mlen;
        off = 0;

        for (; mlen; ret_cnt++) {
            dma_addr_t l = mlen;
            dma_addr_t a = addr;

            if (ret_cnt == iov_cnt) {
                ret = -ENOBUFS;
                goto err;
            }

            iov[ret_cnt].iov_base = dma_memory_map(sgl->as, a, &l, dir);
            if (!l) {
                ret = -EFAULT;
                goto err;
            }
            iov[ret_cnt].iov_len = l;
            addr += l;
            mlen -= l;
        }
    }

    return ret_cnt;
err:
    vmbus_unmap_sgl(sgl, dir, iov, ret_cnt, 0);
    return ret;
}

void vmbus_unmap_sgl(QEMUSGList *sgl, DMADirection dir, struct iovec *iov,
                     unsigned iov_cnt, size_t accessed)
{
    unsigned i;

    for (i = 0; i < iov_cnt; i++) {
        size_t acsd = MIN(accessed, iov[i].iov_len);
        dma_memory_unmap(sgl->as, iov[i].iov_base, iov[i].iov_len, dir, acsd);
        accessed -= acsd;
    }
}

static int gpadl_pre_load(void *opaque)
{
    memset(opaque, 0, sizeof(VMBusGpadl));
    return 0;
}

static const VMStateDescription vmstate_gpadl = {
    .name = "vmbus/gpadl",
    .version_id = 0,
    .minimum_version_id = 0,
    .pre_load = gpadl_pre_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(id, VMBusGpadl),
        VMSTATE_UINT32(child_relid, VMBusGpadl),
        VMSTATE_UINT32(num_gfns, VMBusGpadl),
        VMSTATE_UINT32(seen_gfns, VMBusGpadl),
        VMSTATE_VARRAY_UINT32_ALLOC(gfns, VMBusGpadl, num_gfns, 0,
                                    vmstate_info_uint64, uint64_t),
        VMSTATE_BOOL(alive, VMBusGpadl),
        VMSTATE_END_OF_LIST()
    }
};

static vmbus_ring_buffer *ringbuf_map(const VMBusRingBuf *ringbuf)
{
    vmbus_ring_buffer *rb;
    dma_addr_t mlen = sizeof(*rb);

    rb = dma_memory_map(ringbuf->as, ringbuf->rb_addr, &mlen,
                        DMA_DIRECTION_FROM_DEVICE);
    if (mlen != sizeof(*rb)) {
        dma_memory_unmap(ringbuf->as, rb, mlen, DMA_DIRECTION_FROM_DEVICE, 0);
        return NULL;
    }
    return rb;
}

static void ringbuf_unmap(const VMBusRingBuf *ringbuf, vmbus_ring_buffer *rb,
                          bool dirty)
{
    dma_memory_unmap(ringbuf->as, rb, sizeof(*rb), 1, dirty ? sizeof(*rb) : 0);
}

static int ringbuf_init(VMBusRingBuf *ringbuf, VMBusChannel *chan,
                        DMADirection dir)
{
    int ret = 0;
    int off_pg, len_pg;
    vmbus_ring_buffer *rb;
    VMBusGpadl *gpadl = chan->gpadl;

    if (dir == DMA_DIRECTION_FROM_DEVICE) {
        off_pg = chan->rb_rcv_offset;
        len_pg = gpadl->num_gfns - chan->rb_rcv_offset;
    } else {
        off_pg = 0;
        len_pg = chan->rb_rcv_offset;
    }

    if (len_pg < 1) {
        return -EINVAL;
    }

    ringbuf->gpadl = gpadl;
    ringbuf->as = chan->dev->dma_as;
    ringbuf->dir = dir;
    ringbuf->rb_addr = gpadl->gfns[off_pg] << TARGET_PAGE_BITS;
    ringbuf->base = (off_pg + 1) << TARGET_PAGE_BITS;
    ringbuf->len = (len_pg - 1) << TARGET_PAGE_BITS;

    rb = ringbuf_map(ringbuf);
    if (!rb) {
        return -EFAULT;
    }

    if (dir == DMA_DIRECTION_FROM_DEVICE) {
        ringbuf->my_idx = rb->write_index;
        rb->feature_bits |= VMBUS_RING_BUFFER_FEAT_PENDING_SZ;
        ringbuf->pending_sz = rb->pending_send_sz;
    } else {
        ringbuf->my_idx = rb->read_index;
    }
    if (ringbuf->my_idx >= ringbuf->len) {
        error_report("%s: inconsistent ring buffer: idx:%u len:%u", __func__,
                     ringbuf->my_idx, ringbuf->len);
        ret = -EIO;
        goto out;
    }

out:
    ringbuf_unmap(ringbuf, rb, true);
    return ret;
}

typedef struct VMBusRingIter {
    VMBusRingBuf *ringbuf;
    vmbus_ring_buffer *rb;
    GpadlIter gpiter;
} VMBusRingIter;

static int ring_iter_init(VMBusRingIter *iter, VMBusRingBuf *ringbuf)
{
    iter->ringbuf = ringbuf;
    gpadl_iter_init(&iter->gpiter, ringbuf->gpadl, ringbuf->as, ringbuf->dir,
                    ringbuf->base + ringbuf->my_idx);
    iter->rb = ringbuf_map(ringbuf);
    return iter->rb ? 0 : -EFAULT;
}

static uint32_t rb_avail(uint32_t my_idx, uint32_t other_idx, uint32_t len,
                         bool is_write)
{
    uint32_t ret = other_idx + len - my_idx - is_write;
    if (ret >= len) {
        ret -= len;
    }
    return ret;
}

static bool ring_iter_rcv_update_idx(VMBusRingIter *iter)
{
    VMBusRingBuf *ringbuf = iter->ringbuf;
    vmbus_ring_buffer *rb = iter->rb;
    uint32_t old_idx = ringbuf->my_idx;

    assert(ringbuf->dir == DMA_DIRECTION_TO_DEVICE);

    ringbuf->my_idx = iter->gpiter.off - ringbuf->base;

    /*
     * make sure the data operation is finished before we update the index;
     * pairs with (the guest-side equivalent of) the final smp_mb() in
     * ringbuf_snd_request_room()
     */
    smp_mb();

    rb->read_index = ringbuf->my_idx;
    smp_mb();

    if (rb->interrupt_mask) {
        return false;
    }

    if (rb->feature_bits & VMBUS_RING_BUFFER_FEAT_PENDING_SZ) {
        uint32_t other_idx;
        uint32_t wanted = rb->pending_send_sz;

        /* barrier-less shortcut */
        if (!wanted) {
            return false;
        }

        /* pairs with smp_mb() in ringbuf_snd_request_room() */
        smp_rmb();
        other_idx = rb->write_index;

        /* other (write) side wasn't blocked on our (read) state */
        if (rb_avail(other_idx, old_idx, ringbuf->len, true) >= wanted) {
            return false;
        }
        /* there's not enough space for the other (write) side */
        if (rb_avail(other_idx, ringbuf->my_idx, ringbuf->len, true) <
            wanted) {
            return false;
        }
    }

    return true;
}

static bool ring_iter_snd_update_idx(VMBusRingIter *iter)
{
    VMBusRingBuf *ringbuf = iter->ringbuf;
    vmbus_ring_buffer *rb = iter->rb;
    uint32_t old_idx = ringbuf->my_idx;
    uint32_t wlen;

    assert(ringbuf->dir == DMA_DIRECTION_FROM_DEVICE);

    ringbuf->my_idx = iter->gpiter.off - ringbuf->base;
    wlen = rb_avail(old_idx, ringbuf->my_idx, ringbuf->len, false);

    if (ringbuf->pending_sz) {
        /* we didn't have enough room at ringbuf_snd_request_room() */
        assert(wlen < ringbuf->pending_sz);
        /* successful send reduces our needs; inform the other side */
        ringbuf->pending_sz -= wlen;
        rb->pending_send_sz = ringbuf->pending_sz;
    }

    /*
     * make sure the data operation is finished before we update the index;
     * pairs with (the guest-side equivalent of) the barrier in
     * ring_iter_avail()
     */
    smp_mb();

    rb->write_index = ringbuf->my_idx;
    smp_mb();

    if (rb->interrupt_mask) {
        return false;
    }

    /*
     * other (read) side hasn't caught up with our (write) previous state
     * so it's not blocked
     */
    if (rb_avail(rb->read_index, ringbuf->my_idx, ringbuf->len, false) >
        wlen) {
        return false;
    }
    return true;
}

static void ring_iter_done(VMBusRingIter *iter, bool dirty)
{
    gpadl_iter_done(&iter->gpiter);
    ringbuf_unmap(iter->ringbuf, iter->rb, dirty);
}

static uint32_t ring_iter_rcv_avail(VMBusRingIter *iter)
{
    uint32_t other_idx;
    assert(iter->gpiter.dir == DMA_DIRECTION_TO_DEVICE);

    other_idx = iter->rb->write_index;
    /*
     * pairs with (the guest-side equivalent of) the barrier at the
     * beginning of ring_iter_snd_update_idx()
     */
    smp_mb();

    return rb_avail(iter->gpiter.off - iter->ringbuf->base, other_idx,
                    iter->ringbuf->len, false);
}

static int ring_iter_rw(VMBusRingIter *iter, void *buf, uint32_t len)
{
    int ret1 = 0, ret2 = 0;
    uint32_t remain = iter->ringbuf->len + iter->ringbuf->base -
        iter->gpiter.off;

    if (len >= remain) {
        ret1 = gpadl_iter_rw(&iter->gpiter, buf, remain);
        if (ret1 < 0) {
            return ret1;
        }
        assert(!iter->gpiter.map);
        gpadl_iter_set(&iter->gpiter, iter->ringbuf->base);
        buf += remain;
        len -= remain;
    }
    ret2 = gpadl_iter_rw(&iter->gpiter, buf, len);
    if (ret2 < 0) {
        return ret2;
    }
    return ret1 + ret2;
}

static void ring_iter_set(VMBusRingIter *iter, uint32_t new_off)
{
    new_off += iter->ringbuf->my_idx;
    if (new_off >= iter->ringbuf->len) {
        new_off -= iter->ringbuf->len;
    }
    gpadl_iter_set(&iter->gpiter, iter->ringbuf->base + new_off);
}

static int ringbuf_snd_request_room(VMBusRingBuf *ringbuf, uint32_t needed)
{
    uint32_t avail;
    bool dirty = false;
    vmbus_ring_buffer *rb;

    assert(ringbuf->dir == DMA_DIRECTION_FROM_DEVICE);

    rb = ringbuf_map(ringbuf);
    if (!rb) {
        return -EFAULT;
    }

    avail = rb_avail(ringbuf->my_idx, rb->read_index, ringbuf->len, true);

    if (needed <= avail) {
        needed = 0;
    }

    if (needed != ringbuf->pending_sz) {
        ringbuf->pending_sz = needed;
        /* pairs with smp_rmb() in ring_iter_rcv_update_idx() */
        smp_mb();
        rb->pending_send_sz = needed;
        dirty = true;
    }
    /*
     * pairs with (the guest-side equivalent of) smp_mb() at the
     * beginning of ring_iter_rcv_update_idx()
     */
    smp_mb();

    ringbuf_unmap(ringbuf, rb, dirty);
    return needed ? -ENOSPC : 0;
}


VMBusDevice *vmbus_channel_device(VMBusChannel *chan)
{
    return chan->dev;
}

VMBusChannel *vmbus_device_channel(VMBusDevice *dev, uint32_t chan_idx)
{
    if (chan_idx >= dev->num_channels) {
        return NULL;
    }
    return &dev->channels[chan_idx];
}

uint32_t vmbus_channel_idx(VMBusChannel *chan)
{
    return chan - chan->dev->channels;
}

void vmbus_notify_channel(VMBusChannel *chan)
{
    event_notifier_set(&chan->notifier);
}

static bool channel_broken(VMBusChannel *chan)
{
    return !chan->gpadl;
}

static int signal_channel(VMBusChannel *chan)
{
    int res = 0;
    unsigned long *int_map, mask;
    unsigned idx;
    hwaddr addr = chan->vmbus->int_page_gpa;
    hwaddr len = TARGET_PAGE_SIZE / 2, dirty = 0;

    trace_vmbus_signal_channel(chan->relid);

    if (!addr) {
        return hyperv_set_evt_flag(chan->notify_route, chan->relid);
    }

    int_map = cpu_physical_memory_map(addr, &len, 1);
    if (len != TARGET_PAGE_SIZE / 2) {
        res = -ENXIO;
        goto unmap;
    }

    idx = BIT_WORD(chan->relid);
    mask = BIT_MASK(chan->relid);
    if ((atomic_fetch_or(&int_map[idx], mask) & mask) != mask) {
        res = kvm_hv_sint_route_set_sint(chan->notify_route);
        dirty = len;
    }

unmap:
    cpu_physical_memory_unmap(int_map, len, 1, dirty);
    return res;
}

int vmbus_channel_send(VMBusChannel *chan, uint16_t pkt_type,
                       void *desc, uint32_t desclen,
                       void *msg, uint32_t msglen,
                       bool need_comp, uint64_t transaction_id)
{
    int ret = 0;
    vmbus_packet_hdr hdr;
    uint32_t totlen;
    VMBusRingIter iter;

    if (chan->state != VMCHAN_OPEN) {
        return -EINVAL;
    }

    ret = ring_iter_init(&iter, &chan->ringbuf_snd);
    if (ret) {
        return ret;
    }

    hdr.type = pkt_type;
    hdr.offset_qwords = sizeof(hdr) / sizeof(uint64_t) +
        DIV_ROUND_UP(desclen, sizeof(uint64_t));
    hdr.len_qwords = hdr.offset_qwords +
        DIV_ROUND_UP(msglen, sizeof(uint64_t));
    hdr.flags = need_comp ? VMBUS_PACKET_FLAG_REQUEST_COMPLETION : 0;
    hdr.transaction_id = transaction_id;
    totlen = (hdr.len_qwords + 1) * sizeof(uint64_t);

    assert(totlen <= chan->snd_reserved);

    ret = ring_iter_rw(&iter, &hdr, sizeof(hdr));
    if (ret < 0) {
        goto out;
    }
    if (desclen) {
        assert(desc);
        ret = ring_iter_rw(&iter, desc, desclen);
        if (ret < 0) {
            goto out;
        }
        ring_iter_set(&iter, hdr.offset_qwords * sizeof(uint64_t));
    }
    ret = ring_iter_rw(&iter, msg, msglen);
    if (ret < 0) {
        goto out;
    }
    ring_iter_set(&iter, totlen);

    if (ring_iter_snd_update_idx(&iter)) {
        signal_channel(chan);
    }
    chan->snd_reserved -= totlen;
out:
    ring_iter_done(&iter, ret >= 0);
    return ret;
}

int vmbus_chan_send_completion(VMBusChanReq *req)
{
    assert(req->comp);
    return vmbus_channel_send(req->chan, VMBUS_PACKET_COMP, NULL, 0, req->comp,
                              req->msglen, false, req->transaction_id);
}

int vmbus_channel_reserve(VMBusChannel *chan,
                          uint32_t desclen, uint32_t msglen)
{
    uint32_t needed = chan->snd_reserved +
        sizeof(vmbus_packet_hdr) +
        ROUND_UP(desclen, sizeof(uint64_t)) +
        ROUND_UP(msglen, sizeof(uint64_t)) +
        sizeof(uint64_t);

    int ret = ringbuf_snd_request_room(&chan->ringbuf_snd, needed);

    if (!ret) {
        chan->snd_reserved = needed;
    }
    return ret;
}

static int sgl_from_gpa_ranges(VMBusRingIter *iter, uint32_t len,
                               VMBusChanReq *req)
{
    int ret;
    vmbus_pkt_gpa_direct hdr;
    hwaddr curaddr = 0;
    hwaddr curlen = 0;
    int num;

    if (len < sizeof(hdr)) {
        return -EIO;
    }
    ret = ring_iter_rw(iter, &hdr, sizeof(hdr));
    if (ret < 0) {
        return ret;
    }
    len -= sizeof(hdr);

    num = (len - hdr.rangecount * sizeof(vmbus_gpa_range)) / sizeof(uint64_t);
    if (num < 0) {
        return -EIO;
    }
    qemu_sglist_init(&req->sgl, DEVICE(req->chan->dev), num, iter->gpiter.as);

    for (; hdr.rangecount; hdr.rangecount--) {
        vmbus_gpa_range range;

        if (len < sizeof(range)) {
            goto eio;
        }
        ret = ring_iter_rw(iter, &range, sizeof(range));
        if (ret < 0) {
            goto err;
        }
        len -= sizeof(range);

        if (range.byte_offset & TARGET_PAGE_MASK) {
            goto eio;
        }

        for (; range.byte_count; range.byte_offset = 0) {
            uint64_t paddr;
            uint32_t plen = MIN(range.byte_count,
                                TARGET_PAGE_SIZE - range.byte_offset);

            if (len < sizeof(uint64_t)) {
                goto eio;
            }
            ret = ring_iter_rw(iter, &paddr, sizeof(paddr));
            if (ret < 0) {
                goto err;
            }
            len -= sizeof(uint64_t);
            paddr <<= TARGET_PAGE_BITS;
            paddr |= range.byte_offset;
            range.byte_count -= plen;

            if (curaddr + curlen == paddr) {
                /* consecutive fragments - join */
                curlen += plen;
            } else {
                if (curlen) {
                    qemu_sglist_add(&req->sgl, curaddr, curlen);
                }

                curaddr = paddr;
                curlen = plen;
            }
        }
    }

    if (curlen) {
        qemu_sglist_add(&req->sgl, curaddr, curlen);
    }

    return 0;
eio:
    ret = -EIO;
err:
    qemu_sglist_destroy(&req->sgl);
    return ret;
}

static VMBusChanReq *vmbus_alloc_req(VMBusChannel *chan,
                                     uint32_t size, uint16_t pkt_type,
                                     uint32_t msglen, uint64_t transaction_id,
                                     bool with_comp)
{
    VMBusChanReq *req;
    uint32_t msgoff = QEMU_ALIGN_UP(size, __alignof__(*req->msg));
    uint32_t compoff = QEMU_ALIGN_UP(msgoff + msglen, __alignof__(*req->comp));
    uint32_t complen = with_comp ? msglen : 0;
    uint32_t totlen = compoff + complen;

    req = g_malloc0(totlen);
    req->chan = chan;
    req->pkt_type = pkt_type;
    req->msg = (void *)req + msgoff;
    req->msglen = msglen;
    req->transaction_id = transaction_id;
    req->comp = with_comp ? ((void *)req + compoff) : NULL;
    return req;
}

void *vmbus_channel_recv(VMBusChannel *chan, uint32_t size)
{
    VMBusRingIter iter;
    vmbus_packet_hdr hdr = {0};
    VMBusChanReq *req = NULL;
    uint32_t avail;
    uint32_t pktlen, msglen, msgoff, desclen;
    bool with_comp;

    assert(size >= sizeof(*req));

    if (chan->state != VMCHAN_OPEN) {
        return NULL;
    }

    if (ring_iter_init(&iter, &chan->ringbuf_rcv)) {
        return NULL;
    }

    avail = ring_iter_rcv_avail(&iter);
    if (avail < sizeof(hdr)) {
        goto err;
    }

    if (ring_iter_rw(&iter, &hdr, sizeof(hdr)) < 0) {
        goto err;
    }

    pktlen = hdr.len_qwords * sizeof(uint64_t);
    if (pktlen + sizeof(uint64_t) > avail) {
        goto err;
    }

    msgoff = hdr.offset_qwords * sizeof(uint64_t);
    if (msgoff > pktlen || msgoff < sizeof(hdr)) {
        error_report("%s: malformed packet: %u %u", __func__, msgoff, pktlen);
        goto err;
    }

    msglen = pktlen - msgoff;

    with_comp = hdr.flags & VMBUS_PACKET_FLAG_REQUEST_COMPLETION;
    if (with_comp && vmbus_channel_reserve(chan, 0, msglen)) {
        goto err;
    }

    req = vmbus_alloc_req(chan, size, hdr.type, msglen, hdr.transaction_id,
                          with_comp);

    switch (hdr.type) {
    case VMBUS_PACKET_DATA_USING_GPA_DIRECT:
        desclen = msgoff - sizeof(hdr);
        if (sgl_from_gpa_ranges(&iter, desclen, req) < 0) {
            error_report("%s: failed to convert GPA ranges to SGL", __func__);
            goto err;
        }
        break;
    case VMBUS_PACKET_DATA_INBAND:
    case VMBUS_PACKET_COMP:
        break;
    default:
        error_report("%s: unexpected msg type: %x", __func__, hdr.type);
        goto err;
    }

    ring_iter_set(&iter, msgoff);
    if (ring_iter_rw(&iter, req->msg, msglen) < 0) {
        goto err;
    }
    ring_iter_set(&iter, pktlen + sizeof(uint64_t));

    if (ring_iter_rcv_update_idx(&iter)) {
        signal_channel(chan);
    }
    ring_iter_done(&iter, true);
    return req;
err:
    vmbus_release_req(req);
    ring_iter_done(&iter, false);
    return NULL;
}

void vmbus_release_req(void *req)
{
    VMBusChanReq *r = req;

    if (!req) {
        return;
    }

    if (r->sgl.dev) {
        qemu_sglist_destroy(&r->sgl);
    }
    g_free(req);
}

static const VMStateDescription vmstate_sgent = {
    .name = "vmbus/sgentry",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(base, ScatterGatherEntry),
        VMSTATE_UINT64(len, ScatterGatherEntry),
        VMSTATE_END_OF_LIST()
    }
};

typedef struct VMBusChanReqSave {
    uint16_t chan_idx;
    uint16_t pkt_type;
    uint32_t msglen;
    void *msg;
    uint64_t transaction_id;
    bool with_comp;
    uint32_t num;
    ScatterGatherEntry *sgl;
} VMBusChanReqSave;

static const VMStateDescription vmstate_vmbus_chan_req = {
    .name = "vmbus/vmbus_chan_req",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(chan_idx, VMBusChanReqSave),
        VMSTATE_UINT16(pkt_type, VMBusChanReqSave),
        VMSTATE_UINT32(msglen, VMBusChanReqSave),
        VMSTATE_VBUFFER_ALLOC_UINT32(msg, VMBusChanReqSave, 0, NULL, msglen),
        VMSTATE_UINT64(transaction_id, VMBusChanReqSave),
        VMSTATE_BOOL(with_comp, VMBusChanReqSave),
        VMSTATE_UINT32(num, VMBusChanReqSave),
        VMSTATE_STRUCT_VARRAY_POINTER_UINT32(sgl, VMBusChanReqSave, num,
                                             vmstate_sgent, ScatterGatherEntry),
        VMSTATE_END_OF_LIST()
    }
};

void vmbus_save_req(QEMUFile *f, VMBusChanReq *req)
{
    VMBusChanReqSave req_save;

    req_save.chan_idx = req->chan->subchan_idx;
    req_save.pkt_type = req->pkt_type;
    req_save.msglen = req->msglen;
    req_save.msg = req->msg;
    req_save.transaction_id = req->transaction_id;
    req_save.with_comp = !!req->comp;
    req_save.num = req->sgl.nsg;
    req_save.sgl = g_memdup(req->sgl.sg,
                            req_save.num * sizeof(ScatterGatherEntry));

    vmstate_save_state(f, &vmstate_vmbus_chan_req, &req_save, NULL);

    g_free(req_save.sgl);
}

void *vmbus_load_req(QEMUFile *f, VMBusDevice *dev, uint32_t size)
{
    VMBusChanReqSave req_save;
    VMBusChanReq *req = NULL;
    VMBusChannel *chan = NULL;
    uint32_t i;

    vmstate_load_state(f, &vmstate_vmbus_chan_req, &req_save, 0);

    if (req_save.chan_idx >= dev->num_channels) {
        error_report("%s: %u(chan_idx) > %u(num_channels)", __func__,
                     req_save.chan_idx, dev->num_channels);
        goto out;
    }
    chan = &dev->channels[req_save.chan_idx];

    if (vmbus_channel_reserve(chan, 0, req_save.msglen)) {
        goto out;
    }

    req = vmbus_alloc_req(chan, size, req_save.pkt_type, req_save.msglen,
                          req_save.transaction_id, req_save.with_comp);
    if (req_save.msglen) {
        memcpy(req->msg, req_save.msg, req_save.msglen);
    }

    for (i = 0; i < req_save.num; i++) {
        qemu_sglist_add(&req->sgl, req_save.sgl[i].base, req_save.sgl[i].len);
    }

out:
    if (req_save.msglen) {
        g_free(req_save.msg);
    }
    if (req_save.num) {
        g_free(req_save.sgl);
    }
    return req;
}

static void channel_event_cb(EventNotifier *e)
{
    VMBusChannel *chan = container_of(e, VMBusChannel, notifier);
    if (event_notifier_test_and_clear(e)) {
        chan->notify_cb(chan);
    }
}

static int register_chan_ids(VMBusChannel *chan)
{
    int ret;

    ret = hyperv_set_evt_notifier(chan->connid, &chan->notifier);
    if (ret) {
        return ret;
    }

    set_bit(chan->relid, chan->vmbus->chan_relid_bitmap);
    set_bit(chan->connid, chan->vmbus->connection_id_bitmap);
    return 0;
}

static void unregister_chan_ids(VMBusChannel *chan)
{
    clear_bit(chan->connid, chan->vmbus->connection_id_bitmap);
    clear_bit(chan->relid, chan->vmbus->chan_relid_bitmap);
    hyperv_set_evt_notifier(chan->connid, NULL);
}

static void init_channel(VMBus *vmbus, VMBusDevice *dev, VMBusDeviceClass *vdc,
                         VMBusChannel *chan, uint16_t idx, Error **errp)
{
    int res;

    chan->dev = dev;
    chan->notify_cb = vdc->chan_notify_cb;
    chan->subchan_idx = idx;
    chan->vmbus = vmbus;

    chan->relid = find_next_zero_bit(vmbus->chan_relid_bitmap,
                                     VMBUS_CHAN_RELID_COUNT,
                                     VMBUS_CHAN_FIRST_RELID);
    if (chan->relid == VMBUS_CHAN_RELID_COUNT) {
        error_setg(errp, "no spare relid");
        return;
    }

    chan->connid = find_next_zero_bit(vmbus->connection_id_bitmap,
                                      VMBUS_CHAN_CONNID_COUNT,
                                      VMBUS_CHAN_FIRST_CONNID);
    if (chan->connid == VMBUS_CHAN_CONNID_COUNT) {
        error_setg(errp, "no spare connid");
        return;
    }

    res = event_notifier_init(&chan->notifier, 0);
    if (res) {
        error_setg(errp, "event_notifier_init: %d", res);
        return;
    }

    event_notifier_set_handler(&chan->notifier, channel_event_cb);

    res = register_chan_ids(chan);
    if (res) {
        error_setg(errp, "register_chan_ids: %d", res);
        event_notifier_cleanup(&chan->notifier);
        return;
    }
}

static void deinit_channel(VMBusChannel *chan)
{
    assert(chan->state == VMCHAN_INIT);
    QTAILQ_REMOVE(&chan->vmbus->channel_list, chan, link);
    unregister_chan_ids(chan);
    event_notifier_cleanup(&chan->notifier);
}

static void create_channels(VMBus *vmbus, VMBusDevice *dev, Error **errp)
{
    uint16_t i;
    VMBusDeviceClass *vdc = VMBUS_DEVICE_GET_CLASS(dev);
    Error *err = NULL;

    dev->num_channels = vdc->num_channels ? vdc->num_channels(dev) : 1;
    if (dev->num_channels < 1) {
        error_setg(&err, "invalid #channels: %u", dev->num_channels);
        goto error_out;
    }

    dev->channels = g_new0(VMBusChannel, dev->num_channels);
    for (i = 0; i < dev->num_channels; i++) {
        init_channel(vmbus, dev, vdc, &dev->channels[i], i, &err);
        if (err) {
            goto err_init;
        }

        /*
         * The guest drivers depend on the device subchannels (channels #1+) to
         * be offered after the main channel (channel #0) of that device.  To
         * ensure that, record the channels on the channel list in the order
         * they appear within the device.
         */
        QTAILQ_INSERT_TAIL(&vmbus->channel_list, &dev->channels[i], link);
    }

    return;

err_init:
    while (i--) {
        deinit_channel(&dev->channels[i]);
    }
error_out:
    error_propagate(errp, err);
}

static void free_channels(VMBus *vmbus, VMBusDevice *dev)
{
    uint16_t i;
    for (i = 0; i < dev->num_channels; i++) {
        deinit_channel(&dev->channels[i]);
    }
    g_free(dev->channels);
}

static HvSintRoute *make_sint_route(VMBus *vmbus, uint32_t vp_index)
{
    VMBusChannel *chan;

    if (vp_index == vmbus->target_vp) {
        hyperv_sint_route_ref(vmbus->sint_route);
        return vmbus->sint_route;
    }

    QTAILQ_FOREACH(chan, &vmbus->channel_list, link) {
        if (chan->target_vp == vp_index && chan->notify_route) {
            hyperv_sint_route_ref(chan->notify_route);
            return chan->notify_route;
        }
    }

    return hyperv_sint_route_new(vp_index, VMBUS_SINT, NULL, NULL);
}

static void open_channel(VMBusChannel *chan)
{
    VMBusDeviceClass *vdc = VMBUS_DEVICE_GET_CLASS(chan->dev);

    chan->gpadl = vmbus_get_gpadl(chan, chan->rb_gpadl);
    if (!chan->gpadl) {
        return;
    }

    if (ringbuf_init(&chan->ringbuf_rcv, chan, DMA_DIRECTION_TO_DEVICE) ||
        ringbuf_init(&chan->ringbuf_snd, chan, DMA_DIRECTION_FROM_DEVICE)) {
        goto put_gpadl;
    }

    chan->notify_route = make_sint_route(chan->vmbus, chan->target_vp);
    if (!chan->notify_route) {
        goto put_gpadl;
    }

    if (vdc->open_channel && vdc->open_channel(chan->dev)) {
        goto err_vdc_open;
    }

    return;

err_vdc_open:
    hyperv_sint_route_unref(chan->notify_route);
put_gpadl:
    vmbus_put_gpadl(chan->gpadl);
    chan->gpadl = NULL;
}

static void close_channel(VMBusChannel *chan)
{
    VMBusDeviceClass *vdc = VMBUS_DEVICE_GET_CLASS(chan->dev);

    if (chan->state != VMCHAN_OPEN) {
        return;
    }

    if (vdc->close_channel) {
        vdc->close_channel(chan->dev);
    }

    vmbus_put_gpadl(chan->gpadl);
    chan->gpadl = NULL;
    chan->state = VMCHAN_INIT;
    chan->snd_reserved = 0;
    hyperv_sint_route_unref(chan->notify_route);
    chan->notify_route = NULL;
}

static int channel_post_load(void *opaque, int version_id)
{
    VMBusChannel *chan = opaque;

    if (chan->state == VMCHAN_OPENING || chan->state == VMCHAN_OPEN) {
        open_channel(chan);
    }

    /*
     * if the channel was still transitioning we'll report failure via reply
     * message, no need to error out now
     */
    if (chan->state == VMCHAN_OPEN && channel_broken(chan)) {
        chan->state = VMCHAN_INIT;
        return -1;
    }

    if (chan->state == VMCHAN_OPEN) {
        /* resume processing on the guest side if it missed the notification */
        kvm_hv_sint_route_set_sint(chan->notify_route);
    }
    return 0;
}

static const VMStateDescription vmstate_channel = {
    .name = "vmbus/channel",
    .version_id = 0,
    .minimum_version_id = 0,
    .post_load = channel_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(relid, VMBusChannel),
        VMSTATE_UINT16(subchan_idx, VMBusChannel),
        VMSTATE_UINT32(connid, VMBusChannel),
        VMSTATE_UINT32(open_id, VMBusChannel),
        VMSTATE_UINT32(target_vp, VMBusChannel),
        VMSTATE_UINT32(rb_gpadl, VMBusChannel),
        VMSTATE_UINT32(rb_rcv_offset, VMBusChannel),
        VMSTATE_UINT32(offer_state, VMBusChannel),
        VMSTATE_UINT32(state, VMBusChannel),
        VMSTATE_END_OF_LIST()
    }
};

static VMBusChannel *find_channel(VMBus *vmbus, uint32_t relid)
{
    VMBusChannel *chan;
    QTAILQ_FOREACH(chan, &vmbus->channel_list, link) {
        if (chan->relid == relid) {
            return chan;
        }
    }
    return NULL;
}

static int enqueue_incoming_message(VMBus *vmbus,
                                    const struct hyperv_post_message_input *msg)
{
    uint8_t idx, prev_size;

    qemu_mutex_lock(&vmbus->rx_queue_lock);

    if (vmbus->rx_queue_size == VMBUS_RX_QUEUE_CAPACITY) {
        qemu_mutex_unlock(&vmbus->rx_queue_lock);
        return -ENOBUFS;
    }

    prev_size = vmbus->rx_queue_size;
    idx = (vmbus->rx_queue_head + vmbus->rx_queue_size) %
        VMBUS_RX_QUEUE_CAPACITY;
    memcpy(&vmbus->rx_queue[idx], msg, sizeof(*msg));
    vmbus->rx_queue_size++;

    qemu_mutex_unlock(&vmbus->rx_queue_lock);

    /* only need to resched if the queue was empty before */
    if (!prev_size) {
        vmbus_resched(vmbus);
    }

    return 0;
}

static uint64_t vmbus_recv_message(const struct hyperv_post_message_input *msg,
                                   void *data)
{
    VMBus *vmbus = data;
    struct vmbus_message_header *vmbus_msg;

    if (msg->message_type != HV_MESSAGE_VMBUS) {
        return HV_STATUS_INVALID_HYPERCALL_INPUT;
    }

    if (msg->payload_size < sizeof(struct vmbus_message_header)) {
        return HV_STATUS_INVALID_HYPERCALL_INPUT;
    }

    vmbus_msg = (struct vmbus_message_header*)msg->payload;

    trace_vmbus_recv_message(vmbus_msg->message_type, msg->payload_size);

    if (vmbus_msg->message_type == VMBUS_MSG_INVALID ||
        vmbus_msg->message_type >= VMBUS_MSG_COUNT) {
        error_report("vmbus: unknown message type %#x",
                     vmbus_msg->message_type);
        return HV_STATUS_INVALID_HYPERCALL_INPUT;
    }

    if (enqueue_incoming_message(vmbus, msg)) {
        return HV_STATUS_INSUFFICIENT_BUFFERS;
    }
    return HV_STATUS_SUCCESS;
}

static bool vmbus_initialized(VMBus *vmbus)
{
    return vmbus->version > 0 && vmbus->version <= VMBUS_VERSION_CURRENT;
}

static void vmbus_reset_all(VMBus *vmbus)
{
    qbus_reset_all(BUS(vmbus));
}

static void post_msg(VMBus *vmbus, void *msgdata, uint32_t msglen)
{
    int ret;
    struct hyperv_message msg = {
        .header.message_type = HV_MESSAGE_VMBUS,
    };

    assert(!vmbus->msg_in_progress);
    assert(msglen <= sizeof(msg.payload));
    assert(msglen >= sizeof(struct vmbus_message_header));

    vmbus->msg_in_progress = true;

    trace_vmbus_post_msg(((struct vmbus_message_header *)msgdata)->message_type,
                         msglen);

    memcpy(msg.payload, msgdata, msglen);
    msg.header.payload_size = ROUND_UP(msglen, VMBUS_MESSAGE_SIZE_ALIGN);

    ret = hyperv_post_msg(vmbus->sint_route, &msg);
    if (ret == 0 || ret == -EAGAIN) {
        return;
    }

    error_report("message delivery fatal failure: %d; aborting vmbus", ret);
    vmbus_reset_all(vmbus);
}

static int vmbus_init(VMBus *vmbus)
{
    if (vmbus->target_vp != (uint32_t)-1) {
        vmbus->sint_route = hyperv_sint_route_new(vmbus->target_vp, VMBUS_SINT,
                                                  vmbus_msg_cb, vmbus);
        if (!vmbus->sint_route) {
            error_report("failed to set up SINT route");
            return -ENOMEM;
        }
    }
    return 0;
}

static void vmbus_deinit(VMBus *vmbus)
{
    while (!QTAILQ_EMPTY(&vmbus->gpadl_list)) {
        free_gpadl(vmbus, QTAILQ_FIRST(&vmbus->gpadl_list));
    }

    hyperv_sint_route_unref(vmbus->sint_route);

    vmbus->sint_route = NULL;
    vmbus->int_page_gpa = 0;
    vmbus->target_vp = (uint32_t)-1;
    vmbus->version = 0;
    vmbus->state = VMBUS_LISTEN;
    vmbus->msg_in_progress = false;
}

static void handle_initiate_contact(VMBus *vmbus,
                                    vmbus_message_initiate_contact *msg,
                                    uint32_t msglen)
{
    if (msglen < sizeof(*msg)) {
        return;
    }

    trace_vmbus_initiate_contact(msg->version_requested, msg->target_vcpu,
            msg->monitor_page1, msg->monitor_page2, msg->interrupt_page);

    /*
     * Reset vmbus on INITIATE_CONTACT regardless of its previous state.
     * Useful, in particular, with vmbus-aware BIOS which can't shut vmbus down
     * before handing over to OS loader.
     */
    vmbus_reset_all(vmbus);

    vmbus->target_vp = msg->target_vcpu;
    vmbus->version = msg->version_requested;
    if (vmbus->version < VMBUS_VERSION_WIN8) {
        /* linux passes interrupt page even when it doesn't need it */
        vmbus->int_page_gpa = msg->interrupt_page;
    }
    vmbus->state = VMBUS_HANDSHAKE;

    if (vmbus_init(vmbus)) {
        error_report("failed to init vmbus; aborting");
        vmbus_deinit(vmbus);
        return;
    }
}

static void send_handshake(VMBus *vmbus)
{
    struct vmbus_message_version_response msg = {
        .header.message_type = VMBUS_MSG_VERSION_RESPONSE,
        .version_supported = vmbus_initialized(vmbus),
    };

    post_msg(vmbus, &msg, sizeof(msg));
}

static void complete_handshake(VMBus *vmbus)
{
    vmbus->state = VMBUS_LISTEN;
}

static void handle_request_offers(VMBus *vmbus, void *msgdata, uint32_t msglen)
{
    VMBusChannel *chan;

    if (!vmbus_initialized(vmbus)) {
        return;
    }

    QTAILQ_FOREACH(chan, &vmbus->channel_list, link) {
        if (chan->offer_state == VMOFFER_INIT) {
            chan->offer_state = VMOFFER_SENDING;
            break;
        }
    }

    vmbus->state = VMBUS_OFFER;
}

static void send_offer(VMBus *vmbus)
{
    VMBusChannel *chan;
    struct vmbus_message_header alloffers_msg = {
        .message_type = VMBUS_MSG_ALLOFFERS_DELIVERED,
    };

    QTAILQ_FOREACH(chan, &vmbus->channel_list, link) {
        if (chan->offer_state == VMOFFER_SENDING) {
            VMBusDeviceClass *vdc = VMBUS_DEVICE_GET_CLASS(chan->dev);
            QemuUUID classid = vdc->classid;
            QemuUUID instanceid = chan->dev->instanceid;
            struct vmbus_message_offer_channel msg = {
                .header.message_type = VMBUS_MSG_OFFERCHANNEL,
                .child_relid = chan->relid,
                .connection_id = chan->connid,
                .channel_flags = vdc->channel_flags,
                .mmio_size_mb = vdc->mmio_size_mb,
                .sub_channel_index = vmbus_channel_idx(chan),
                .interrupt_flags = VMBUS_OFFER_INTERRUPT_DEDICATED,
            };

            /* Hyper-V wants LE GUIDs */
            qemu_uuid_bswap(&classid);
            memcpy(msg.type_uuid, &classid, sizeof(classid));
            qemu_uuid_bswap(&instanceid);
            memcpy(msg.instance_uuid, &instanceid, sizeof(instanceid));

            post_msg(vmbus, &msg, sizeof(msg));
            return;
        }
    }

    /* no more offers, send completion message */
    post_msg(vmbus, &alloffers_msg, sizeof(alloffers_msg));
}

static void complete_offer(VMBus *vmbus)
{
    VMBusChannel *chan;

    QTAILQ_FOREACH(chan, &vmbus->channel_list, link) {
        if (chan->offer_state == VMOFFER_SENDING) {
            chan->offer_state = VMOFFER_SENT;
            chan = QTAILQ_NEXT(chan, link);
            if (chan) {
                chan->offer_state = VMOFFER_SENDING;
            }
            /* more offers or terminator, no state transition */
            return;
        }
    }
    /* no more offers, can listen again */
    vmbus->state = VMBUS_LISTEN;
}


static void handle_gpadl_header(VMBus *vmbus, vmbus_message_gpadl_header *msg,
                                uint32_t msglen)
{
    VMBusGpadl *gpadl;
    uint32_t num_gfns_tot, num_gfns, i;

    if (msglen < sizeof(*msg) || !vmbus_initialized(vmbus)) {
        return;
    }

    if (msg->rangecount != 1 ||
        (msg->range.byte_count & (TARGET_PAGE_SIZE - 1)) ||
        find_gpadl(vmbus, msg->gpadl_id)) {
        /* create a temporary broken GPADL to reply with failure */
        num_gfns_tot = 0;
    } else {
        num_gfns_tot = msg->range.byte_count  >> TARGET_PAGE_BITS;
    }

    gpadl = create_gpadl(vmbus, msg->gpadl_id, msg->child_relid, num_gfns_tot);

    num_gfns = (msglen - sizeof(*msg)) / sizeof(uint64_t);
    if (num_gfns > num_gfns_tot) {
        num_gfns = num_gfns_tot;
    }

    for (i = 0; i < num_gfns; i++) {
        gpadl->gfns[gpadl->seen_gfns++] = msg->range.pfn_array[i];
    }

    if (gpadl_full(gpadl)) {
        vmbus->state = VMBUS_CREATE_GPADL;
    }
}

static void handle_gpadl_body(VMBus *vmbus, vmbus_message_gpadl_body *msg,
                              uint32_t msglen)
{
    VMBusGpadl *gpadl;
    uint32_t num_gfns_left, num_gfns, i;

    if (msglen < sizeof(*msg) || !vmbus_initialized(vmbus)) {
        return;
    }

    gpadl = find_gpadl(vmbus, msg->gpadl_id);
    if (!gpadl) {
        /* create a temporary broken GPADL to reply with failure */
        gpadl = create_gpadl(vmbus, msg->gpadl_id, 0, 0);
    }

    num_gfns_left = gpadl->num_gfns - gpadl->seen_gfns;
    num_gfns = (msglen - sizeof(*msg)) / sizeof(uint64_t);
    assert(num_gfns_left);
    if (num_gfns > num_gfns_left) {
        num_gfns = num_gfns_left;
    }

    for (i = 0; i < num_gfns; i++) {
        gpadl->gfns[gpadl->seen_gfns++] = msg->pfn_array[i];
    }

    if (gpadl_full(gpadl)) {
        vmbus->state = VMBUS_CREATE_GPADL;
    }
}

static void send_create_gpadl(VMBus *vmbus)
{
    VMBusGpadl *gpadl;

    QTAILQ_FOREACH(gpadl, &vmbus->gpadl_list, link) {
        if (gpadl_full(gpadl) && !gpadl->alive) {
            struct vmbus_message_gpadl_created msg = {
                .header.message_type = VMBUS_MSG_GPADL_CREATED,
                .gpadl_id = gpadl->id,
                .child_relid = gpadl->child_relid,
                .status = gpadl_broken(gpadl),
            };

            post_msg(vmbus, &msg, sizeof(msg));
            return;
        }
    }

    assert(false);
}

static void complete_create_gpadl(VMBus *vmbus)
{
    VMBusGpadl *gpadl;

    QTAILQ_FOREACH(gpadl, &vmbus->gpadl_list, link) {
        if (gpadl_full(gpadl) && !gpadl->alive) {
            if (gpadl_broken(gpadl)) {
                free_gpadl(vmbus, gpadl);
            } else {
                gpadl->alive = true;
            }

            vmbus->state = VMBUS_LISTEN;
            return;
        }
    }

    assert(false);
}

static void handle_gpadl_teardown(VMBus *vmbus,
                                  vmbus_message_gpadl_teardown *msg,
                                  uint32_t msglen)
{
    VMBusGpadl *gpadl;

    if (msglen < sizeof(*msg) || !vmbus_initialized(vmbus)) {
        return;
    }

    gpadl = find_gpadl(vmbus, msg->gpadl_id);
    if (!gpadl || gpadl->in_use) {
        return;
    }

    gpadl->alive = false;
    vmbus->state = VMBUS_TEARDOWN_GPADL;
}

static void send_teardown_gpadl(VMBus *vmbus)
{
    VMBusGpadl *gpadl;

    QTAILQ_FOREACH(gpadl, &vmbus->gpadl_list, link) {
        if (gpadl_full(gpadl) && !gpadl->alive) {
            struct vmbus_message_gpadl_torndown msg = {
                .header.message_type = VMBUS_MSG_GPADL_TORNDOWN,
                .gpadl_id = gpadl->id,
            };

            post_msg(vmbus, &msg, sizeof(msg));
            return;
        }
    }

    assert(false);
}

static void complete_teardown_gpadl(VMBus *vmbus)
{
    VMBusGpadl *gpadl;

    QTAILQ_FOREACH(gpadl, &vmbus->gpadl_list, link) {
        if (gpadl_full(gpadl) && !gpadl->alive) {
            free_gpadl(vmbus, gpadl);
            vmbus->state = VMBUS_LISTEN;
            return;
        }
    }

    assert(false);
}

static void handle_open_channel(VMBus *vmbus, vmbus_message_open_channel *msg,
                                uint32_t msglen)
{
    VMBusChannel *chan;

    if (msglen < sizeof(*msg) || !vmbus_initialized(vmbus)) {
        return;
    }

    chan = find_channel(vmbus, msg->child_relid);
    if (!chan || chan->state != VMCHAN_INIT) {
        /* FIXME: think of replying with an error */
        return;
    }

    chan->state = VMCHAN_OPENING;
    chan->rb_gpadl = msg->ring_buffer_gpadl_id;
    chan->rb_rcv_offset = msg->ring_buffer_offset;
    chan->target_vp = msg->target_vp;
    chan->open_id = msg->open_id;

    open_channel(chan);
    vmbus->state = VMBUS_OPEN_CHANNEL;
}

static void send_open_channel(VMBus *vmbus)
{
    VMBusChannel *chan;

    QTAILQ_FOREACH(chan, &vmbus->channel_list, link) {
        if (chan->state == VMCHAN_OPENING) {
            struct vmbus_message_open_result msg = {
                .header.message_type = VMBUS_MSG_OPENCHANNEL_RESULT,
                .child_relid = chan->relid,
                .open_id = chan->open_id,
                .status = channel_broken(chan),
            };

            post_msg(vmbus, &msg, sizeof(msg));
            return;
        }
    }

    assert(false);
}

static void complete_open_channel(VMBus *vmbus)
{
    VMBusChannel *chan;

    QTAILQ_FOREACH(chan, &vmbus->channel_list, link) {
        if (chan->state == VMCHAN_OPENING) {
            chan->state = channel_broken(chan) ? VMCHAN_INIT : VMCHAN_OPEN;
            vmbus->state = VMBUS_LISTEN;
            return;
        }
    }

    assert(false);
}

static void handle_close_channel(VMBus *vmbus, vmbus_message_close_channel *msg,
                                 uint32_t msglen)
{
    VMBusChannel *chan;

    if (msglen < sizeof(*msg) || !vmbus_initialized(vmbus)) {
        return;
    }

    chan = find_channel(vmbus, msg->child_relid);
    if (!chan) {
        return;
    }

    close_channel(chan);
}

static void handle_unload(VMBus *vmbus, void *msg, uint32_t msglen)
{
    vmbus->state = VMBUS_UNLOAD;
}

static void send_unload(VMBus *vmbus)
{
    vmbus_message_header msg = {
        .message_type = VMBUS_MSG_UNLOAD_RESPONSE,
    };

    qemu_mutex_lock(&vmbus->rx_queue_lock);
    vmbus->rx_queue_size = 0;
    qemu_mutex_unlock(&vmbus->rx_queue_lock);

    post_msg(vmbus, &msg, sizeof(msg));
    return;
}

static void complete_unload(VMBus *vmbus)
{
    vmbus_reset_all(vmbus);
}

static void process_incoming_message(VMBus *vmbus)
{
    struct hyperv_post_message_input *hv_msg;
    struct vmbus_message_header *msg;
    void *msgdata;
    uint32_t msglen;

    qemu_mutex_lock(&vmbus->rx_queue_lock);

    if (!vmbus->rx_queue_size) {
        goto unlock;
    }

    hv_msg = &vmbus->rx_queue[vmbus->rx_queue_head];
    msglen =  hv_msg->payload_size;
    if (msglen < sizeof(*msg)) {
        goto out;
    }
    msgdata = hv_msg->payload;
    msg = (struct vmbus_message_header*)msgdata;

    trace_vmbus_process_incoming_message(msg->message_type);

    switch (msg->message_type) {
    case VMBUS_MSG_INITIATE_CONTACT:
        handle_initiate_contact(vmbus, msgdata, msglen);
        break;
    case VMBUS_MSG_REQUESTOFFERS:
        handle_request_offers(vmbus, msgdata, msglen);
        break;
    case VMBUS_MSG_GPADL_HEADER:
        handle_gpadl_header(vmbus, msgdata, msglen);
        break;
    case VMBUS_MSG_GPADL_BODY:
        handle_gpadl_body(vmbus, msgdata, msglen);
        break;
    case VMBUS_MSG_GPADL_TEARDOWN:
        handle_gpadl_teardown(vmbus, msgdata, msglen);
        break;
    case VMBUS_MSG_OPENCHANNEL:
        handle_open_channel(vmbus, msgdata, msglen);
        break;
    case VMBUS_MSG_CLOSECHANNEL:
        handle_close_channel(vmbus, msgdata, msglen);
        break;
    case VMBUS_MSG_UNLOAD:
        handle_unload(vmbus, msgdata, msglen);
        break;
    default:
        error_report("unknown message type %#x", msg->message_type);
        break;
    }

out:
    vmbus->rx_queue_size--;
    vmbus->rx_queue_head++;
    vmbus->rx_queue_head %= VMBUS_RX_QUEUE_CAPACITY;

    vmbus_resched(vmbus);
unlock:
    qemu_mutex_unlock(&vmbus->rx_queue_lock);
}

static void vmbus_run(void *opaque)
{
    VMBus *vmbus = opaque;

    if (vmbus->msg_in_progress) {
        return;
    }

    switch(vmbus->state) {
    case VMBUS_LISTEN:
        process_incoming_message(vmbus);
        break;
    case VMBUS_HANDSHAKE:
        send_handshake(vmbus);
        break;
    case VMBUS_OFFER:
        send_offer(vmbus);
        break;
    case VMBUS_CREATE_GPADL:
        send_create_gpadl(vmbus);
        break;
    case VMBUS_TEARDOWN_GPADL:
        send_teardown_gpadl(vmbus);
        break;
    case VMBUS_OPEN_CHANNEL:
        send_open_channel(vmbus);
        break;
    case VMBUS_UNLOAD:
        send_unload(vmbus);
        break;
    default:
        assert(false);
    };
}

static void vmbus_msg_cb(void *data, int status)
{
    VMBus *vmbus = data;

    assert(vmbus->msg_in_progress);

    trace_vmbus_post_reply_callback(status);

    if (status == -EAGAIN) {
        goto out;
    }
    if (status) {
        error_report("message delivery fatal failure: %d; aborting vmbus",
                     status);
        vmbus_reset_all(vmbus);
        return;
    }

    switch (vmbus->state) {
    case VMBUS_HANDSHAKE:
        complete_handshake(vmbus);
        break;
    case VMBUS_OFFER:
        complete_offer(vmbus);
        break;
    case VMBUS_CREATE_GPADL:
        complete_create_gpadl(vmbus);
        break;
    case VMBUS_TEARDOWN_GPADL:
        complete_teardown_gpadl(vmbus);
        break;
    case VMBUS_OPEN_CHANNEL:
        complete_open_channel(vmbus);
        break;
    case VMBUS_UNLOAD:
        complete_unload(vmbus);
        break;
    default:
        assert(false);
    }

out:
    vmbus->msg_in_progress = false;
    vmbus_resched(vmbus);
}

static void vmbus_resched(VMBus *vmbus)
{
    aio_bh_schedule_oneshot(qemu_get_aio_context(), vmbus_run, vmbus);
}

static void vmbus_signal_event(EventNotifier *e)
{
    VMBusChannel *chan;
    VMBus *vmbus = container_of(e, VMBus, notifier);
    unsigned long *int_map;
    hwaddr addr, len;
    bool is_dirty = false;

    if (!event_notifier_test_and_clear(e)) {
        return;
    }

    trace_vmbus_signal_event();

    if (!vmbus->int_page_gpa) {
        return;
    }

    addr = vmbus->int_page_gpa + TARGET_PAGE_SIZE / 2;
    len = TARGET_PAGE_SIZE / 2;
    int_map = cpu_physical_memory_map(addr, &len, 1);
    if (len != TARGET_PAGE_SIZE / 2) {
        goto unmap;
    }

    QTAILQ_FOREACH(chan, &vmbus->channel_list, link) {
        if (bitmap_test_and_clear_atomic(int_map, chan->relid, 1)) {
            event_notifier_set(&chan->notifier);
            is_dirty = true;
        }
    }

unmap:
    cpu_physical_memory_unmap(int_map, len, 1, is_dirty);
}

static void vmbus_install_rom(VMBusDevice *vdev)
{
    VMBusDeviceClass *vdc = VMBUS_DEVICE_GET_CLASS(vdev);
    VMBus *vmbus = VMBUS(qdev_get_parent_bus(DEVICE(vdev)));
    BusChild *child;
    char uuid[UUID_FMT_LEN + 1];
    char romname[10 + UUID_FMT_LEN + 4 + 1];

    if (vdev->romfile) {
        /* device-specific rom */
        qemu_uuid_unparse(&vdc->instanceid, uuid);
        snprintf(romname, sizeof(romname), "vmbus/dev/%s.rom", uuid);
        rom_add_file(vdev->romfile, romname, 0, -1, true, NULL, NULL);
    } else if (vdc->romfile) {
        /* class-wide rom */
        QTAILQ_FOREACH(child, &BUS(vmbus)->children, sibling) {
            VMBusDevice *chlddev = VMBUS_DEVICE(child->child);

            /* another device of the same class has already installed it */
            if (chlddev != vdev && !chlddev->romfile &&
                VMBUS_DEVICE_GET_CLASS(chlddev) == vdc) {
                return;
            }
        }
        qemu_uuid_unparse(&vdc->classid, uuid);
        snprintf(romname, sizeof(romname), "vmbus/%s.rom", uuid);
        rom_add_file(vdc->romfile, romname, 0, -1, true, NULL, NULL);
    }
}

static void vmbus_dev_realize(DeviceState *dev, Error **errp)
{
    VMBusDevice *vdev = VMBUS_DEVICE(dev);
    VMBusDeviceClass *vdc = VMBUS_DEVICE_GET_CLASS(vdev);
    VMBus *vmbus = VMBUS(qdev_get_parent_bus(dev));
    BusChild *child;
    Error *err = NULL;
    char idstr[UUID_FMT_LEN + 1];

    if (!qemu_uuid_is_null(&vdc->instanceid)) {
        /* Class wants to only have a single instance with a fixed UUID */
        vdev->instanceid = vdc->instanceid;
    }
    assert(!qemu_uuid_is_null(&vdev->instanceid));

    /* Check for instance id collision for this class id */
    QTAILQ_FOREACH(child, &BUS(vmbus)->children, sibling) {
        VMBusDevice *child_dev = VMBUS_DEVICE(child->child);

        if (child_dev == vdev) {
            continue;
        }

        if (qemu_uuid_is_equal(&child_dev->instanceid, &vdev->instanceid)) {
            qemu_uuid_unparse(&vdev->instanceid, idstr);
            error_setg(&err, "duplicate vmbus device instance id %s", idstr);
            goto error_out;
        }
    }

    vdev->dma_as = &address_space_memory;

    create_channels(vmbus, vdev, &err);
    if (err) {
        goto error_out;
    }

    vmbus_install_rom(vdev);

    if (vdc->vmdev_realize) {
        vdc->vmdev_realize(vdev, &err);
        if (err) {
            goto err_vdc_realize;
        }
    }
    return;

err_vdc_realize:
    free_channels(vmbus, vdev);
error_out:
    error_propagate(errp, err);
}

static void vmbus_dev_reset(DeviceState *dev)
{
    uint16_t i;
    VMBusDevice *vdev = VMBUS_DEVICE(dev);
    VMBusDeviceClass *vdc = VMBUS_DEVICE_GET_CLASS(vdev);

    if (vdc->vmdev_reset) {
        vdc->vmdev_reset(vdev);
    }

    if (vdev->channels) {
        for (i = 0; i < vdev->num_channels; i++) {
            VMBusChannel *chan = &vdev->channels[i];
            if (chan->state == VMCHAN_OPEN) {
                close_channel(chan);
            }
            chan->offer_state = VMOFFER_INIT;
        }
    }
}

static void vmbus_dev_unrealize(DeviceState *dev, Error **errp)
{
    VMBusDevice *vdev = VMBUS_DEVICE(dev);
    VMBusDeviceClass *vdc = VMBUS_DEVICE_GET_CLASS(vdev);
    VMBus *vmbus = VMBUS(qdev_get_parent_bus(dev));

    if (vdc->vmdev_unrealize) {
        vdc->vmdev_unrealize(vdev, errp);
    }
    free_channels(vmbus, vdev);
}

static Property vmbus_dev_props[] = {
    DEFINE_PROP_STRING("romfile", VMBusDevice, romfile),
    DEFINE_PROP_END_OF_LIST()
};

static void vmbus_dev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *kdev = DEVICE_CLASS(klass);
    kdev->bus_type = TYPE_VMBUS;
    kdev->realize = vmbus_dev_realize;
    kdev->unrealize = vmbus_dev_unrealize;
    kdev->reset = vmbus_dev_reset;
    kdev->props = vmbus_dev_props;
}

static int vmbus_dev_post_load(void *opaque, int version_id)
{
    int ret;
    uint16_t i;
    VMBusDevice *dev = opaque;

    for (i = 0; i < dev->num_channels; i++) {
        ret = register_chan_ids(&dev->channels[i]);
        if (ret) {
            goto err_reg;
        }
    }

    return 0;

err_reg:
    while (i--) {
        unregister_chan_ids(&dev->channels[i]);
    }
    return ret;
}

const VMStateDescription vmstate_vmbus_dev = {
    .name = TYPE_VMBUS_DEVICE,
    .version_id = 0,
    .minimum_version_id = 0,
    .post_load = vmbus_dev_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY(instanceid.data, VMBusDevice, 16),
        VMSTATE_UINT16(num_channels, VMBusDevice),
        VMSTATE_STRUCT_VARRAY_POINTER_UINT16(channels, VMBusDevice, num_channels,
                                             vmstate_channel, VMBusChannel),
        VMSTATE_END_OF_LIST()
    }
};

/* vmbus generic device base */
static const TypeInfo vmbus_dev_type_info = {
    .name = TYPE_VMBUS_DEVICE,
    .parent = TYPE_DEVICE,
    .abstract = true,
    .instance_size = sizeof(VMBusDevice),
    .class_size = sizeof(VMBusDeviceClass),
    .class_init = vmbus_dev_class_init,
};

static void vmbus_realize(BusState *bus, Error **errp)
{
    int ret = 0;
    Error *local_err = NULL;
    VMBus *vmbus = VMBUS(bus);

    qemu_mutex_init(&vmbus->rx_queue_lock);

    QTAILQ_INIT(&vmbus->gpadl_list);
    QTAILQ_INIT(&vmbus->channel_list);

    ret = hyperv_set_msg_handler(VMBUS_MESSAGE_CONNECTION_ID,
                                 vmbus_recv_message, vmbus);
    if (ret != 0) {
        error_setg(&local_err, "hyperv set message handler failed: %d", ret);
        goto error_out;
    }

    ret = event_notifier_init(&vmbus->notifier, 0);
    if (ret != 0) {
        error_setg(&local_err, "event notifier failed to init with %d", ret);
        goto remove_msg_handler;
    }

    event_notifier_set_handler(&vmbus->notifier, vmbus_signal_event);
    ret = hyperv_set_evt_notifier(VMBUS_EVENT_CONNECTION_ID,
                                  &vmbus->notifier);
    if (ret != 0) {
        error_setg(&local_err, "hyperv set event handler failed with %d", ret);
        goto clear_event_notifier;
    }

    return;

clear_event_notifier:
    event_notifier_cleanup(&vmbus->notifier);
remove_msg_handler:
    hyperv_set_msg_handler(VMBUS_MESSAGE_CONNECTION_ID, NULL, NULL);
error_out:
    qemu_mutex_destroy(&vmbus->rx_queue_lock);
    error_propagate(errp, local_err);
}

static void vmbus_unrealize(BusState *bus, Error **errp)
{
    VMBus *vmbus = VMBUS(bus);

    hyperv_set_msg_handler(VMBUS_MESSAGE_CONNECTION_ID, NULL, NULL);
    hyperv_set_evt_notifier(VMBUS_MESSAGE_CONNECTION_ID, NULL);
    event_notifier_cleanup(&vmbus->notifier);

    qemu_mutex_destroy(&vmbus->rx_queue_lock);
}

static void vmbus_reset(BusState *bus)
{
    vmbus_deinit(VMBUS(bus));
}

static char *vmbus_get_dev_path(DeviceState *dev)
{
    BusState *bus = qdev_get_parent_bus(dev);
    return qdev_get_dev_path(bus->parent);
}

static char *vmbus_get_fw_dev_path(DeviceState *dev)
{
    VMBusDevice *vdev = VMBUS_DEVICE(dev);
    char path[32 + 1 + UUID_FMT_LEN + 1];
    int off;

    off = snprintf(path, sizeof(path), "%s@", qdev_fw_name(dev));
    if (sizeof(path) - off > UUID_FMT_LEN) {
        qemu_uuid_unparse(&vdev->instanceid, path + off);
    }
    return g_strdup(path);
}

static void vmbus_class_init(ObjectClass *klass, void *data)
{
    BusClass *k = BUS_CLASS(klass);

    k->get_dev_path = vmbus_get_dev_path;
    k->get_fw_dev_path = vmbus_get_fw_dev_path;
    k->realize = vmbus_realize;
    k->unrealize = vmbus_unrealize;
    k->reset = vmbus_reset;
}

static const TypeInfo vmbus_type_info = {
    .name = TYPE_VMBUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(VMBus),
    .class_init = vmbus_class_init,
};

typedef struct VMBusBridge {
    SysBusDevice parent_obj;

    VMBus bus;
} VMBusBridge;

#define VMBUS_BRIDGE(obj) OBJECT_CHECK(VMBusBridge, (obj), TYPE_VMBUS_BRIDGE)

static void vmbus_bridge_realize(DeviceState *dev, Error **errp)
{
    VMBus *vmbus = &VMBUS_BRIDGE(dev)->bus;
    qbus_create_inplace(vmbus, sizeof(VMBus), TYPE_VMBUS, dev, "vmbus");
}

static int vmbus_post_load(void *opaque, int version_id)
{
    int ret;
    VMBusChannel *chan;
    VMBus *vmbus = &VMBUS_BRIDGE(opaque)->bus;

    QTAILQ_FOREACH(chan, &vmbus->channel_list, link) {
        unregister_chan_ids(chan);
    }

    ret = vmbus_init(vmbus);
    if (ret) {
        return ret;
    }

    vmbus_resched(vmbus);
    return 0;
}

static const VMStateDescription vmstate_post_message_input = {
    .name = "vmbus/hyperv_post_message_input",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(connection_id, struct hyperv_post_message_input),
        VMSTATE_UINT32(message_type, struct hyperv_post_message_input),
        VMSTATE_UINT32(payload_size, struct hyperv_post_message_input),
        VMSTATE_UINT8_ARRAY(payload, struct hyperv_post_message_input,
                            HV_MESSAGE_PAYLOAD_SIZE),
        VMSTATE_END_OF_LIST()
    }
};

static bool vmbus_rx_queue_needed(void *opaque)
{
    VMBus *vmbus = &VMBUS_BRIDGE(opaque)->bus;
    return vmbus->rx_queue_size;
}

static const VMStateDescription vmstate_rx_queue = {
    .name = "vmbus/rx_queue",
    .version_id = 0,
    .minimum_version_id = 0,
    .needed = vmbus_rx_queue_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(bus.rx_queue_head, VMBusBridge),
        VMSTATE_UINT8(bus.rx_queue_size, VMBusBridge),
        VMSTATE_STRUCT_ARRAY(bus.rx_queue, VMBusBridge,
                             VMBUS_RX_QUEUE_CAPACITY, 0,
                             vmstate_post_message_input,
                             struct hyperv_post_message_input),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_vmbus_bridge = {
    .name = TYPE_VMBUS_BRIDGE,
    .version_id = 0,
    .minimum_version_id = 0,
    .post_load = vmbus_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(bus.state, VMBusBridge),
        VMSTATE_UINT32(bus.version, VMBusBridge),
        VMSTATE_UINT32(bus.target_vp, VMBusBridge),
        VMSTATE_UINT64(bus.int_page_gpa, VMBusBridge),
        VMSTATE_QTAILQ_V(bus.gpadl_list, VMBusBridge, 0,
                         vmstate_gpadl, VMBusGpadl, link),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription*[]) {
        &vmstate_rx_queue,
        NULL
    }
};

static void vmbus_bridge_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);
    k->realize = vmbus_bridge_realize;
    k->fw_name = "vmbus@0";
    set_bit(DEVICE_CATEGORY_BRIDGE, k->categories);
    k->vmsd = &vmstate_vmbus_bridge;
    k->user_creatable = false;
}

static const TypeInfo vmbus_bridge_type_info = {
    .name = TYPE_VMBUS_BRIDGE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(VMBusBridge),
    .class_init = vmbus_bridge_class_init,
};

VMBusBridge *vmbus_bridge;

void vmbus_create(void)
{
    DeviceState *dev;

    assert(!vmbus_exists());

    if (!hyperv_synic_usable()) {
        error_report("VMBus requires usable Hyper-V SynIC and VP_INDEX");
        exit(1);
    }

    dev = qdev_create(NULL, TYPE_VMBUS_BRIDGE);
    qdev_init_nofail(dev);
    vmbus_bridge = VMBUS_BRIDGE(dev);
}

bool vmbus_exists(void)
{
    return vmbus_bridge;
    // return qdev_find_recursive(sysbus_get_default(), TYPE_VMBUS_BRIDGE);
}

static void vmbus_register_types(void)
{
    type_register_static(&vmbus_bridge_type_info);
    type_register_static(&vmbus_dev_type_info);
    type_register_static(&vmbus_type_info);
}

type_init(vmbus_register_types)
