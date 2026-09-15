/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * QEMU remote attach
 *
 * Copyright (c) 2013 Xilinx Inc
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
 *
 * This code is licensed under the GNU GPL.
 */

#include "qemu/osdep.h"
#include "system/system.h"
#include "chardev/char.h"
#include "system/cpus.h"
#include "system/cpu-timers.h"
#include "exec/icount.h"
#include "system/reset.h"
#include "hw/core/sysbus.h"
#include "hw/core/hw-error.h"
#include "qemu/thread.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "migration/vmstate.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "qemu/cutils.h"
#include "trace.h"

#include "hw/core/remote-port-proto.h"
#include "hw/core/remote-port.h"

#define REMOTE_PORT_CLASS(klass)    \
     OBJECT_CLASS_CHECK(RemotePortClass, (klass), TYPE_REMOTE_PORT)

static void rp_event_read_and_process(RemotePort *s);

static void sync_timer_hit(void *opaque);
static void syncresp_timer_hit(void *opaque);

void rp_rsp_mutex_lock(RemotePort *s)
{
    qemu_mutex_lock(&s->rsp_mutex);
}

void rp_rsp_mutex_unlock(RemotePort *s)
{
    qemu_mutex_unlock(&s->rsp_mutex);
}

int64_t rp_normalized_vmclk(RemotePort *s)
{
    int64_t clk;

    clk = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    clk -= s->peer.clk_base;
    return clk;
}

static void rp_restart_sync_timer_bare(RemotePort *s)
{
    if (!s->do_sync) {
        return;
    }

    if (s->sync.quantum) {
        ptimer_stop(s->sync.ptimer);
        ptimer_set_limit(s->sync.ptimer, s->sync.quantum, 1);
        ptimer_run(s->sync.ptimer, 1);
    }
}

void rp_restart_sync_timer(RemotePort *s)
{
    if (s->doing_sync) {
        return;
    }
    ptimer_transaction_begin(s->sync.ptimer);
    rp_restart_sync_timer_bare(s);
    ptimer_transaction_commit(s->sync.ptimer);
}

static void rp_fatal_error(RemotePort *s, const char *reason)
{
    int64_t clk = rp_normalized_vmclk(s);
    error_report("%s: %s clk=%" PRIu64 " ns", s->prefix, reason, clk);
    exit(EXIT_FAILURE);
}

static ssize_t rp_recv(RemotePort *s, void *buf, size_t count)
{
    ssize_t r;

    r = qemu_chr_fe_read_all(&s->chr, buf, count);
    if (r <= 0) {
        return r;
    }
    if (r != count) {
        error_report("%s: Bad read, expected %zd but got %zd",
                     s->prefix, count, r);
        rp_fatal_error(s, "Bad read");
    }

    return r;
}

ssize_t rp_write(RemotePort *s, const void *buf, size_t count)
{
    ssize_t r;

    qemu_mutex_lock(&s->write_mutex);
    r = qemu_chr_fe_write_all(&s->chr, buf, count);
    qemu_mutex_unlock(&s->write_mutex);
    assert(r == count);
    if (r <= 0) {
        error_report("%s: Disconnected r=%zd buf=%p count=%zd",
                     s->prefix, r, buf, count);
        rp_fatal_error(s, "Bad write");
    }
    return r;
}

static unsigned int rp_has_work(RemotePort *s)
{
    unsigned int work = s->rx_queue.wpos - s->rx_queue.rpos;
    return work;
}

/* Response handling.  */
RemotePortRespSlot *rp_dev_timed_wait_resp(RemotePort *s, uint32_t dev,
                                            uint32_t id, int timems)
{
    int i;

    assert(s->devs[dev]);

    /* Find a free slot.  */
    for (i = 0; i < ARRAY_SIZE(s->dev_state[dev].rsp_queue); i++) {
        if (s->dev_state[dev].rsp_queue[i].used == false) {
            break;
        }
    }

    if (i == ARRAY_SIZE(s->dev_state[dev].rsp_queue) ||
        s->dev_state[dev].rsp_queue[i].used == true) {
        error_report("Number of outstanding transactions exceeded! %d",
                      RP_MAX_OUTSTANDING_TRANSACTIONS);
        rp_fatal_error(s, "Internal error");
    }

    /* Got a slot, fill it in.  */
    s->dev_state[dev].rsp_queue[i].id = id;
    s->dev_state[dev].rsp_queue[i].valid = false;
    s->dev_state[dev].rsp_queue[i].used = true;

    while (!s->dev_state[dev].rsp_queue[i].valid) {
        rp_rsp_mutex_unlock(s);
        rp_event_read_and_process(s);
        rp_rsp_mutex_lock(s);
        if (s->dev_state[dev].rsp_queue[i].valid) {
            break;
        }
        if (!rp_has_work(s)) {
            if (timems) {
                if (!qemu_cond_timedwait(&s->progress_cond, &s->rsp_mutex,
                                       timems)) {
                    /*
                     * TimeOut!
                     */
                    break;
                }
            } else {
                qemu_cond_wait(&s->progress_cond, &s->rsp_mutex);
            }
        }
    }
    return &s->dev_state[dev].rsp_queue[i];
}

RemotePortRespSlot *rp_dev_wait_resp(RemotePort *s, uint32_t dev, uint32_t id)
{
    return rp_dev_timed_wait_resp(s, dev, id, 0);
}

RemotePortDynPkt rp_wait_resp(RemotePort *s)
{
    while (!rp_dpkt_is_valid(&s->rspqueue)) {
        rp_rsp_mutex_unlock(s);
        rp_event_read_and_process(s);
        rp_rsp_mutex_lock(s);
        /* Need to recheck the condition with the rsp lock taken.  */
        if (rp_dpkt_is_valid(&s->rspqueue)) {
            break;
        }
        trace_rp_wait_resp(s->prefix);
        if (!rp_has_work(s)) {
            qemu_cond_wait(&s->progress_cond, &s->rsp_mutex);
        }
    }
    return s->rspqueue;
}

static void rp_cmd_hello(RemotePort *s, struct rp_pkt *pkt)
{
    s->peer.version = pkt->hello.version;
    if (pkt->hello.version.major != RP_VERSION_MAJOR) {
        error_report("remote-port version missmatch remote=%d.%d local=%d.%d",
                      pkt->hello.version.major, pkt->hello.version.minor,
                      RP_VERSION_MAJOR, RP_VERSION_MINOR);
        rp_fatal_error(s, "Bad version");
    }

    if (pkt->hello.caps.len) {
        void *caps = (char *) pkt + pkt->hello.caps.offset;

        rp_process_caps(&s->peer, caps, pkt->hello.caps.len);
    }
}

static void rp_cmd_sync(RemotePort *s, struct rp_pkt *pkt)
{
    size_t enclen;
    int64_t clk;
    int64_t diff;

    assert(!(pkt->hdr.flags & RP_PKT_FLAGS_response));

    clk = rp_normalized_vmclk(s);
    diff = pkt->sync.timestamp - clk;

    enclen = rp_encode_sync_resp(pkt->hdr.id, pkt->hdr.dev, &s->sync.rsp.sync,
                                 pkt->sync.timestamp);
    assert(enclen == sizeof s->sync.rsp.sync);

    /* We have temporarily disabled blocking syncs into QEMU.  */
    if (diff <= 0LL || true) {
        /* We are already a head of time. Respond and issue a sync.  */
        rp_write(s, (void *) &s->sync.rsp, enclen);
        return;
    }

    ptimer_transaction_begin(s->sync.ptimer_resp);
    ptimer_set_limit(s->sync.ptimer_resp, diff, 1);
    ptimer_run(s->sync.ptimer_resp, 1);
    s->sync.resp_timer_enabled = true;
    ptimer_transaction_commit(s->sync.ptimer_resp);
}

static void rp_say_hello(RemotePort *s)
{
    struct rp_pkt_hello pkt;
    uint32_t caps[] = {
        CAP_BUSACCESS_EXT_BASE,
        CAP_BUSACCESS_EXT_BYTE_EN,
        CAP_WIRE_POSTED_UPDATES,
        CAP_ATS,
    };
    size_t len;

    len = rp_encode_hello_caps(s->current_id++, 0, &pkt, RP_VERSION_MAJOR,
                               RP_VERSION_MINOR,
                               caps, caps, sizeof caps / sizeof caps[0]);
    rp_write(s, (void *) &pkt, len);

    if (sizeof caps) {
        rp_write(s, caps, sizeof caps);
    }
}

static void rp_say_sync(RemotePort *s, int64_t clk)
{
    struct rp_pkt_sync pkt;
    size_t len;

    len = rp_encode_sync(s->current_id++, 0, &pkt, clk);
    rp_write(s, (void *) &pkt, len);
}

static void syncresp_timer_hit(void *opaque)
{
    RemotePort *s = REMOTE_PORT(opaque);

    s->sync.resp_timer_enabled = false;
    rp_write(s, (void *) &s->sync.rsp, sizeof s->sync.rsp.sync);
    memset(&s->sync.rsp, 0, sizeof s->sync.rsp);
}

static void sync_timer_hit(void *opaque)
{
    RemotePort *s = REMOTE_PORT(opaque);
    int64_t clk;
    RemotePortDynPkt rsp;

    clk = rp_normalized_vmclk(s);
    if (s->sync.resp_timer_enabled) {
        s->sync.need_sync = true;
        rp_restart_sync_timer_bare(s);
        return;
    }

    /* Sync.  */
    s->doing_sync = true;
    s->sync.need_sync = false;
    qemu_mutex_lock(&s->rsp_mutex);
    /* Send the sync.  */
    rp_say_sync(s, clk);

    rsp = rp_wait_resp(s);
    rp_dpkt_invalidate(&rsp);
    qemu_mutex_unlock(&s->rsp_mutex);
    s->doing_sync = false;

    rp_restart_sync_timer_bare(s);
}

void rp_process(RemotePort *s)
{
    while (true) {
        struct rp_pkt *pkt;
        unsigned int rpos;
        bool actioned = false;
        RemotePortDevice *dev;
        RemotePortDeviceClass *rpdc;

        qemu_mutex_lock(&s->rsp_mutex);
        if (!rp_has_work(s)) {
            qemu_mutex_unlock(&s->rsp_mutex);
            break;
        }
        rpos = s->rx_queue.rpos;

        pkt = s->rx_queue.pkt[rpos].pkt;
        trace_rp_process(s->prefix, s->rx_queue.rpos, s->rx_queue.wpos,
                         pkt->hdr.cmd, pkt->hdr.dev);

        /*
         * To handle recursiveness, we need to advance the index
         * index before processing the packet.
         */
        s->rx_queue.rpos++;
        s->rx_queue.rpos %= ARRAY_SIZE(s->rx_queue.pkt);
        qemu_mutex_unlock(&s->rsp_mutex);

        dev = s->devs[pkt->hdr.dev];
        if (dev) {
            rpdc = REMOTE_PORT_DEVICE_GET_CLASS(dev);
            if (rpdc->ops[pkt->hdr.cmd]) {
                rpdc->ops[pkt->hdr.cmd](dev, pkt);
                actioned = true;
            }
        }

        switch (pkt->hdr.cmd) {
        case RP_CMD_sync:
            rp_cmd_sync(s, pkt);
            break;
        default:
            assert(actioned);
        }

        s->rx_queue.inuse[rpos] = false;
        qemu_sem_post(&s->rx_queue.sem);
    }
}

static void rp_event_read_and_process(RemotePort *s)
{
    event_notifier_test_and_clear(&s->event_notifier);

    rp_process(s);
}

static void rp_event_read_cb(EventNotifier *n)
{
    RemotePort *s = container_of(n, RemotePort, event_notifier);

    rp_event_read_and_process(s);
}

static void rp_event_notify(RemotePort *s)
{
    event_notifier_set(&s->event_notifier);
}

/* Handover a pkt to CPU or IO-thread context.  */
static void rp_pt_handover_pkt(RemotePort *s, RemotePortDynPkt *dpkt)
{
    bool full;

    /*
     * Take the rsp lock around the wpos update, otherwise
     * rp_wait_resp will race with us.
     */
    qemu_mutex_lock(&s->rsp_mutex);
    s->rx_queue.wpos++;
    s->rx_queue.wpos %= ARRAY_SIZE(s->rx_queue.pkt);
    /*
     * Ensure rx_queue index update is visible to consumer
     * before signaling event, to prevent lost wakeup
     */
    smp_mb();
    rp_event_notify(s);
    qemu_cond_signal(&s->progress_cond);
    qemu_mutex_unlock(&s->rsp_mutex);

    do {
        full = s->rx_queue.inuse[s->rx_queue.wpos];
        if (full) {
            trace_rp_pt_rx_queue_full(s->prefix, s->rx_queue.rpos,
                                      s->rx_queue.wpos);
            qemu_sem_timedwait(&s->rx_queue.sem, 2 * 1000);
        }
    } while (full);
}

static bool rp_pt_cmd_sync(RemotePort *s, struct rp_pkt *pkt)
{
    size_t enclen;
    int64_t clk;
    int64_t diff = 0;
    struct rp_pkt rsp;

    assert(!(pkt->hdr.flags & RP_PKT_FLAGS_response));

    if (use_icount) {
        clk = rp_normalized_vmclk(s);
        diff = pkt->sync.timestamp - clk;
    }
    enclen = rp_encode_sync_resp(pkt->hdr.id, pkt->hdr.dev, &rsp.sync,
                                 pkt->sync.timestamp);
    assert(enclen == sizeof rsp.sync);

    if (!use_icount || diff < s->sync.quantum) {
        /* We are still OK.  */
        rp_write(s, (void *) &rsp, enclen);
        return true;
    }

    /* We need IO or CPU thread sync.  */
    return false;
}

static bool rp_pt_process_pkt(RemotePort *s, RemotePortDynPkt *dpkt)
{
    struct rp_pkt *pkt = dpkt->pkt;

    trace_rp_pt_process_pkt(s->prefix, pkt->hdr.cmd, pkt->hdr.id, pkt->hdr.dev,
                            pkt->hdr.flags & RP_PKT_FLAGS_response);

    if (pkt->hdr.dev >= ARRAY_SIZE(s->devs)) {
        /* FIXME: Respond with an error.  */
        return true;
    }

    if (pkt->hdr.flags & RP_PKT_FLAGS_response) {
        uint32_t dev = pkt->hdr.dev;
        uint32_t id = pkt->hdr.id;
        int i;

        if (pkt->hdr.flags & RP_PKT_FLAGS_posted) {
            return true;
        }

        qemu_mutex_lock(&s->rsp_mutex);

        /* Try to find a per-device slot first.  */
        for (i = 0; i < ARRAY_SIZE(s->dev_state[dev].rsp_queue); i++) {
            if (s->devs[dev] && s->dev_state[dev].rsp_queue[i].used == true
                && s->dev_state[dev].rsp_queue[i].id == id) {
                break;
            }
        }

        if (i < ARRAY_SIZE(s->dev_state[dev].rsp_queue)) {
            /* Found a per device one.  */
            assert(s->dev_state[dev].rsp_queue[i].valid == false);

            rp_dpkt_swap(&s->dev_state[dev].rsp_queue[i].rsp, dpkt);
            s->dev_state[dev].rsp_queue[i].valid = true;

            qemu_cond_signal(&s->progress_cond);
        } else {
            rp_dpkt_swap(&s->rspqueue, dpkt);
            qemu_cond_signal(&s->progress_cond);
        }

        qemu_mutex_unlock(&s->rsp_mutex);
        return true;
    }

    switch (pkt->hdr.cmd) {
    case RP_CMD_hello:
        rp_cmd_hello(s, pkt);
        break;
    case RP_CMD_sync:
        if (rp_pt_cmd_sync(s, pkt)) {
            return true;
        }
        /* Fall-through.  */
    case RP_CMD_read:
    case RP_CMD_write:
    case RP_CMD_interrupt:
    case RP_CMD_ats_req:
    case RP_CMD_ats_inv:
        rp_pt_handover_pkt(s, dpkt);
        break;
    default:
        g_assert_not_reached();
        break;
    }
    return false;
}

static int rp_read_pkt(RemotePort *s, RemotePortDynPkt *dpkt)
{
    struct rp_pkt *pkt = dpkt->pkt;
    int used;
    int r;

    r = rp_recv(s, pkt, sizeof pkt->hdr);
    if (r <= 0) {
        return r;
    }
    used = rp_decode_hdr((void *) &pkt->hdr);
    assert(used == sizeof pkt->hdr);

    if (pkt->hdr.len) {
        rp_dpkt_alloc(dpkt, sizeof pkt->hdr + pkt->hdr.len);
        /* pkt may move due to realloc.  */
        pkt = dpkt->pkt;
        r = rp_recv(s, &pkt->hdr + 1, pkt->hdr.len);
        if (r <= 0) {
            return r;
        }
        rp_decode_payload(pkt);
    }

    return used + r;
}

static void *rp_protocol_thread(void *arg)
{
    RemotePort *s = REMOTE_PORT(arg);
    unsigned int i;
    int r;

    /* Make sure we have a decent bufsize to start with.  */
    rp_dpkt_alloc(&s->rsp, sizeof s->rsp.pkt->busaccess + 1024);
    rp_dpkt_alloc(&s->rspqueue, sizeof s->rspqueue.pkt->busaccess + 1024);
    for (i = 0; i < ARRAY_SIZE(s->rx_queue.pkt); i++) {
        rp_dpkt_alloc(&s->rx_queue.pkt[i],
                      sizeof s->rx_queue.pkt[i].pkt->busaccess + 1024);
        s->rx_queue.inuse[i] = false;
    }

    rp_say_hello(s);

    while (1) {
        RemotePortDynPkt *dpkt;
        unsigned int wpos = s->rx_queue.wpos;
        bool handled;

        dpkt = &s->rx_queue.pkt[wpos];
        s->rx_queue.inuse[wpos] = true;

        r = rp_read_pkt(s, dpkt);
        if (r <= 0) {
            /* Disconnected.  */
            break;
        }
        handled = rp_pt_process_pkt(s, dpkt);
        if (handled) {
            s->rx_queue.inuse[wpos] = false;
        }
    }

    if (!s->finalizing) {
        rp_fatal_error(s, "Disconnected");
    }
    return NULL;
}

static void rp_reset(DeviceState *dev)
{
    RemotePort *s = REMOTE_PORT(dev);

    if (s->reset_done) {
        return;
    }

    qemu_thread_create(&s->thread, "remote-port", rp_protocol_thread, s,
                       QEMU_THREAD_JOINABLE);

    rp_restart_sync_timer(s);
    s->reset_done = true;
}

static void rp_realize(DeviceState *dev, Error **errp)
{
    RemotePort *s = REMOTE_PORT(dev);
    Chardev *chr = NULL;

    s->peer.clk_base = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    qemu_mutex_init(&s->write_mutex);
    qemu_mutex_init(&s->rsp_mutex);
    qemu_cond_init(&s->progress_cond);

    s->prefix = object_get_canonical_path(OBJECT(dev));

    if (s->chrdev_id) {
        chr = qemu_chr_find(s->chrdev_id);
    }

    if (!chr) {
        error_setg(errp, "Unable to connect to remote-port channel");
        return;
    }

    if (!qemu_chr_fe_init(&s->chr, chr, errp)) {
        return;
    }

    s->chrdev = chr;

    if (event_notifier_init(&s->event_notifier, 0) < 0) {
        error_setg(errp,
                   "Unable to initialize event notifier for remote port");
        return;
    }

    aio_set_event_notifier(qemu_get_aio_context(), &s->event_notifier,
                           rp_event_read_cb,
                           NULL, NULL);

    /*
     * Pick up the quantum from the local property setup.
     * After config negotiation with the peer, sync.quantum value might
     * change.
     */
    s->sync.quantum = s->peer.local_cfg.quantum;

    s->sync.ptimer = ptimer_init(sync_timer_hit, s, PTIMER_POLICY_LEGACY);
    s->sync.ptimer_resp = ptimer_init(syncresp_timer_hit, s,
                                      PTIMER_POLICY_LEGACY);

    /* The Sync-quantum is expressed in nano-seconds.  */
    ptimer_transaction_begin(s->sync.ptimer);
    ptimer_set_freq(s->sync.ptimer, 1000 * 1000 * 1000);
    ptimer_transaction_commit(s->sync.ptimer);

    ptimer_transaction_begin(s->sync.ptimer_resp);
    ptimer_set_freq(s->sync.ptimer_resp, 1000 * 1000 * 1000);
    ptimer_transaction_commit(s->sync.ptimer_resp);

    qemu_sem_init(&s->rx_queue.sem, ARRAY_SIZE(s->rx_queue.pkt) - 1);
}

static void rp_unrealize(DeviceState *dev)
{
    RemotePort *s = REMOTE_PORT(dev);

    s->finalizing = true;

    aio_set_event_notifier(qemu_get_aio_context(), &s->event_notifier,
                            NULL, NULL, NULL);

    info_report("%s: Wait for remote-port to disconnect", s->prefix);
    qemu_chr_fe_disconnect(&s->chr);
    if (s->reset_done) {
        qemu_thread_join(&s->thread);
    }
    qemu_chr_fe_deinit(&s->chr, false);

    event_notifier_cleanup(&s->event_notifier);
}

static const VMStateDescription vmstate_rp = {
    .name = TYPE_REMOTE_PORT,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST(),
    }
};

static Property rp_properties[] = {
    DEFINE_PROP_STRING("chrdev-id", RemotePort, chrdev_id),
    DEFINE_PROP_BOOL("sync", RemotePort, do_sync, false),
    DEFINE_PROP_UINT64("sync-quantum", RemotePort, peer.local_cfg.quantum,
                       1000000),
};

static void rp_prop_allow_set_link(const Object *obj, const char *name,
                                   Object *val, Error **errp)
{
}

static void rp_init(Object *obj)
{
    RemotePort *s = REMOTE_PORT(obj);
    int t;
    int i;

    for (i = 0; i < REMOTE_PORT_MAX_DEVS; ++i) {
        char *name = g_strdup_printf("remote-port-dev%d", i);
        object_property_add_link(obj, name, TYPE_REMOTE_PORT_DEVICE,
                             (Object **)&s->devs[i],
                             rp_prop_allow_set_link,
                             OBJ_PROP_LINK_STRONG);
        g_free(name);


        for (t = 0; t < RP_MAX_OUTSTANDING_TRANSACTIONS; t++) {
            s->dev_state[i].rsp_queue[t].used = false;
            s->dev_state[i].rsp_queue[t].valid = false;
            rp_dpkt_alloc(&s->dev_state[i].rsp_queue[t].rsp,
               sizeof s->dev_state[i].rsp_queue[t].rsp.pkt->busaccess + 1024);
        }
    }
}

static void rp_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->legacy_reset = rp_reset;
    dc->realize = rp_realize;
    dc->unrealize = rp_unrealize;
    dc->vmsd = &vmstate_rp;
    device_class_set_props_n(dc, rp_properties, ARRAY_SIZE(rp_properties));
}

static const TypeInfo rp_info = {
    .name          = TYPE_REMOTE_PORT,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(RemotePort),
    .instance_init = rp_init,
    .class_init    = rp_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { },
    },
};

static const TypeInfo rp_device_info = {
    .name          = TYPE_REMOTE_PORT_DEVICE,
    .parent        = TYPE_INTERFACE,
    .class_size    = sizeof(RemotePortDeviceClass),
};

static void rp_register_types(void)
{
    type_register_static(&rp_info);
    type_register_static(&rp_device_info);
}

type_init(rp_register_types)
