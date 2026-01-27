#include "qemu/osdep.h"
#include "netpass.h"

#include "migration/migration.h"
#include "migration/vmstate.h"
#include "net/queue.h"
#include "net/filter.h"
#include "net/net.h"
#include "net/vhost_net.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/iov.h"
#include "qemu/typedefs.h"
#include "qom/object.h"
#include "trace.h"

struct NetPassState {
    NetFilterState parent_obj;
    bool active;
    size_t packet_count;
    uint32_t qlength;
    uint32_t qcapacity;
    uint8_t *qbuffer;
    SocketReadState rs;
    QTAILQ_ENTRY(NetPassState) next;
};

static void netpass_queue_clear(NetPassState *s)
{
    g_free(s->qbuffer);
    s->qbuffer = NULL;
    s->qcapacity = 0;
    s->qlength = 0;
    s->packet_count = 0;
}

OBJECT_DEFINE_SIMPLE_TYPE_WITH_INTERFACES(NetPassState, filter_netpass,
                                          FILTER_NETPASS, NETFILTER,
                                          { TYPE_VMSTATE_IF }, { } )

static bool netpass_vmstate_pre_save(void *opaque, Error **errp)
{
    NetPassState *s = opaque;
    s->active = false;
    return true;
}

static int netpass_vmstate_post_save(void *opaque)
{
    NetPassState *s = opaque;
    trace_migration_netpass_passed_packet_count(NETFILTER(s)->netdev_id, s->packet_count);
    netpass_queue_clear(s);
    return 0;
}

static void netpass_vmstate_post_load_bh(void *opaque)
{
    NetPassState *s = opaque;

    int ret = net_fill_rstate(&s->rs, s->qbuffer, s->qlength);
    if (ret == -1) {
        warn_report("migration: Failed to fill netpass rstate during load");
    }
    trace_migration_netpass_received_packet_count(NETFILTER(s)->netdev_id, s->packet_count);
    netpass_queue_clear(s);
}

static bool netpass_vmstate_post_load(void *opaque, int version_id, Error **errp)
{
    /*
     * Schedule on the main thread in case this function is running on the
     * postcopy listen thread and there is a fault during packet injection.
     */
    migration_bh_schedule(netpass_vmstate_post_load_bh, opaque);
    return true;
}

static char *filter_netpass_vmstate_if_get_id(VMStateIf *obj)
{
    NetFilterState *nf = NETFILTER(obj);
    return g_strconcat("filter-netpass/", nf->netdev_id,  NULL);
}

static const VMStateDescription vmstate_netpass = {
    .name = "filter-netpass",
    .version_id = 1,
    .minimum_version_id = 1,
    .phase = VMS_PHASE_NETPASS,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(qlength, NetPassState),
        VMSTATE_UINT32(qcapacity, NetPassState),
        VMSTATE_VBUFFER_ALLOC_UINT32(qbuffer, NetPassState, 0, NULL, qcapacity),
        VMSTATE_END_OF_LIST(),
    },
    .pre_save_errp = netpass_vmstate_pre_save,
    .post_save = netpass_vmstate_post_save,
    .post_load_errp = netpass_vmstate_post_load,
};

QTAILQ_HEAD(, NetPassState) filters = QTAILQ_HEAD_INITIALIZER(filters);

static void netpass_rs_finalize(SocketReadState *rs)
{
    NetPassState *s = container_of(rs, NetPassState, rs);
    NetFilterState *nf = NETFILTER(s);

    struct iovec iov = {
        .iov_len = rs->packet_len,
        .iov_base = rs->buf,
    };
    qemu_netfilter_pass_to_next(nf->netdev, 0, &iov, 1, nf);
    s->packet_count++;
}

static void filter_netpass_setup(NetFilterState *nf, Error **errp)
{
    NetPassState *s = FILTER_NETPASS(nf);

    s->active = false;
    s->qbuffer = NULL;
    s->qcapacity = 0;
    s->qlength = 0;
    s->packet_count = 0;
    net_socket_rs_init(&s->rs, netpass_rs_finalize, true);
}

static void filter_netpass_cleanup(NetFilterState *nf)
{
    NetPassState *s = FILTER_NETPASS(nf);

    s->active = false;
    netpass_queue_clear(s);
    if (nf->netdev) {
        qemu_set_netpass_enabled(nf->netdev, false);
    }
}

static ssize_t filter_netpass_receive_iov(NetFilterState *nf,
                                          NetClientState *sender,
                                          unsigned flags,
                                          const struct iovec *iov,
                                          int iovcnt,
                                          NetPacketSent *sent_cb)
{
    NetPassState *s = FILTER_NETPASS(nf);

    if (!s->active) {
        return 0;
    }

    uint32_t total_size = iov_size(iov, iovcnt);
    size_t req_cap = sizeof(uint32_t) + sizeof(uint32_t) + total_size;
    if (s->qcapacity - s->qlength < req_cap) {
        size_t new_capacity = s->qcapacity;
        while (new_capacity - s->qlength < req_cap) {
            new_capacity += 4096;
        }
        s->qbuffer = g_realloc(s->qbuffer, new_capacity);
        s->qcapacity = new_capacity;
    }
    uint32_t total_size_be = htonl(total_size);
    memcpy(&s->qbuffer[s->qlength], &total_size_be, sizeof(uint32_t));
    s->qlength += sizeof(uint32_t);
    uint32_t vnet_hdr_len_be = htonl(sender->vnet_hdr_len);
    memcpy(&s->qbuffer[s->qlength], &vnet_hdr_len_be, sizeof(uint32_t));
    s->qlength += sizeof(uint32_t);
    iov_to_buf_full(iov, iovcnt, 0, &s->qbuffer[s->qlength], total_size);
    s->qlength += total_size;
    s->packet_count++;

    return 0;
}

static void filter_netpass_class_init(ObjectClass *oc, const void *data)
{
    NetFilterClass *nfc = NETFILTER_CLASS(oc);
    VMStateIfClass *vc = VMSTATE_IF_CLASS(oc);

    nfc->setup = filter_netpass_setup;
    nfc->cleanup = filter_netpass_cleanup;
    nfc->receive_iov = filter_netpass_receive_iov;

    vc->get_id = filter_netpass_vmstate_if_get_id;
}

static void filter_netpass_init(Object *obj)
{
}

static void filter_netpass_finalize(Object *obj)
{
    NetPassState *s = FILTER_NETPASS(obj);
    (void)s;
}

int migration_netpass_setup(Error **errp)
{
    NetClientState *nc;

    QTAILQ_FOREACH(nc, &net_clients, next) {
        if (!nc->is_netdev) {
            continue;
        }
        if (get_vhost_net(nc)) {
            warn_report("migration: netpass is not supported with vhost=on");
            continue;
        }
        g_autofree char *filter_id = g_strconcat("netpass-", nc->name, NULL);
        Object *obj = object_new_with_props(TYPE_FILTER_NETPASS,
                                            object_get_objects_root(),
                                            filter_id, errp,
                                            "netdev", nc->name,
                                            "queue", "tx",
                                            NULL);
        if (!obj) {
            error_prepend(errp, "Failed to setup migration netpass");
            return -1;
        }
        trace_migration_netpass_setup_created_filter(nc->name);
        object_ref(obj);
        QTAILQ_INSERT_TAIL(&filters, FILTER_NETPASS(obj), next);
        vmstate_register(VMSTATE_IF(obj), VMSTATE_INSTANCE_ID_ANY,
                         &vmstate_netpass, obj);
    }
    return 0;
}

void migration_netpass_activate(void)
{
    NetPassState *s;
    QTAILQ_FOREACH(s, &filters, next) {
        s->packet_count = 0;
        s->active = true;
        qemu_set_netpass_enabled(NETFILTER(s)->netdev, true);
    }
}

void migration_netpass_cleanup(void)
{
    NetPassState *s, *ns;
    QTAILQ_FOREACH_SAFE(s, &filters, next, ns) {
        QTAILQ_REMOVE(&filters, s, next);
        vmstate_unregister(VMSTATE_IF(s), &vmstate_netpass, s);
        object_unref(s);
    }
}
