/*
 * QEMU Hyper-V network device support
 *
 * Copyright (c) 2018 Virtuozzo International GmbH.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/iov.h"
#include "hw/vmbus/vmbus.h"
#include "net/net.h"
#include "rndis.h"
#include "hvnet-proto.h"

#define TYPE_HV_NET "hv-net"
#define HV_NET_GUID "f8615163-df3e-46c5-913f-f2d2f965ed0e"

/* FIXME: generalize to vmbus.c? */
typedef struct HvNetRcvPkt {
    QTAILQ_ENTRY(HvNetRcvPkt) link;
    uint32_t off;
    uint32_t len;
} HvNetRcvPkt;

typedef enum HvNetState {
    HV_NET_RESET,
    HV_NET_INITIALIZING,
    HV_NET_INITIALIZED,
} HvNetState;

typedef struct HvNet {
    VMBusDevice parent;

    NICConf conf;
    NICState *nic;

    HvNetState state;

    uint16_t sndbuf_id;
    uint32_t sndbuf_gpadl_id;
    VMBusGpadl *sndbuf_gpadl;

    uint16_t rcvbuf_id;
    uint32_t rcvbuf_gpadl_id;
    VMBusGpadl *rcvbuf_gpadl;
    int32_t rcvbuf_slot_num;    /* int32_t for VMSTATE_STRUCT_VARRAY_ALLOC */
    uint16_t rcvbuf_slot_len;
    unsigned long *rcvbuf_slot_map;
    HvNetRcvPkt *rcvpkts;
    QTAILQ_HEAD(, HvNetRcvPkt) rcvpkts_free;

    struct {} reset_start;

    uint32_t protocol_ver;
    uint32_t ndis_maj_ver;
    uint32_t ndis_min_ver;
    uint32_t rndis_ctl;
    uint32_t rndis_req_id;
    uint32_t rndis_maj;
    uint32_t rndis_min;
    uint32_t max_xfer_size;
    uint32_t rndis_query_oid;
#define RNDIS_QUERY_INFO_LEN 32
    uint64_t rndis_query_info[RNDIS_QUERY_INFO_LEN];
    uint32_t rndis_query_info_len;
    uint32_t rndis_set_status;
    uint32_t rndis_packet_filter;

    bool link_down;

    uint32_t rx_pkts;
    uint32_t tx_pkts;
} HvNet;

#define HV_NET(obj) OBJECT_CHECK(HvNet, (obj), TYPE_HV_NET)

typedef struct HvNetReq
{
    VMBusChanReq vmreq;
    HvNet *net;
    unsigned iov_cnt;
    struct iovec iov[64];
} HvNetReq;

static int hv_net_init_req(HvNet *net, HvNetReq *req)
{
    int ret;
    QEMUSGList *sgl = &req->vmreq.sgl;

    req->net = net;

    if (!sgl->dev) {
        return 0;
    }

    ret = vmbus_map_sgl(sgl, DMA_DIRECTION_TO_DEVICE, req->iov,
                        ARRAY_SIZE(req->iov), -1, 0);
    if (ret >= 0) {
        req->iov_cnt = ret;
    } else {
        error_report("%s: failed to map SGL: %d", __func__, ret);
    }
    return ret;
}

static void hv_net_free_req(HvNetReq *req)
{
    vmbus_unmap_sgl(&req->vmreq.sgl, DMA_DIRECTION_TO_DEVICE, req->iov,
                    req->iov_cnt, 0);
    vmbus_release_req(req);
}

static int complete_req(HvNetReq *req)
{
    int ret = 0;
#if 0
    VMBusChanReq *vmreq = &req->vmreq;
    struct nvsp_msg_header *nhdr = vmreq->msg;

    error_report("%s >>>> msg: %x %lx", __func__,
                 vmreq->msglen ? nhdr->msg_type : -1, vmreq->transaction_id);
#endif
    if (req->vmreq.comp) {
        ret = vmbus_chan_send_completion(&req->vmreq);
    }

    hv_net_free_req(req);
    return ret;
}

static HvNetRcvPkt *get_rcv_pkt(HvNet *net, size_t len)
{
    uint32_t nr, start;
    HvNetRcvPkt *pkt;

    if (!len) {
        return NULL;
    }

    nr = DIV_ROUND_UP(len, net->rcvbuf_slot_len);
    start = bitmap_find_next_zero_area(net->rcvbuf_slot_map,
                                       net->rcvbuf_slot_num, 0, nr, 0);
    if (start >= net->rcvbuf_slot_num) {
        return NULL;
    }

    bitmap_set(net->rcvbuf_slot_map, start, nr);
    pkt = QTAILQ_FIRST(&net->rcvpkts_free);
    assert(pkt);
    QTAILQ_REMOVE(&net->rcvpkts_free, pkt, link);
    pkt->off = start * net->rcvbuf_slot_len;
    pkt->len = len;
    return pkt;
}

static void put_rcv_pkt(HvNet *net, HvNetRcvPkt *pkt)
{
    uint32_t nr, start;

    start = pkt->off / net->rcvbuf_slot_len;
    nr = DIV_ROUND_UP(pkt->len, net->rcvbuf_slot_len);
    bitmap_clear(net->rcvbuf_slot_map, start, nr);
    QTAILQ_INSERT_TAIL(&net->rcvpkts_free, pkt, link);
    pkt->len = 0;
}

static void put_rcv_pkt_by_tr_id(HvNet *net, uint64_t tr_id)
{
    /* transaction id comes from the guest and can't be trusted blindly */
    HvNetRcvPkt *pkt;

    if (tr_id >= net->rcvbuf_slot_num) {
        return;
    }
    pkt = &net->rcvpkts[tr_id];
    if (!pkt->len) {
        return;
    }
    put_rcv_pkt(net, pkt);
}

static void create_rcvbuf(HvNet *net)
{
    uint32_t gpadl_len;
    int i;

    gpadl_len = vmbus_gpadl_len(net->rcvbuf_gpadl);
    net->rcvbuf_slot_num = gpadl_len / net->rcvbuf_slot_len;
    net->rcvbuf_slot_map = bitmap_new(net->rcvbuf_slot_num);
    net->rcvpkts = g_new0(HvNetRcvPkt, net->rcvbuf_slot_num);
    QTAILQ_INIT(&net->rcvpkts_free);
    for (i = 0; i < net->rcvbuf_slot_num; i++) {
        QTAILQ_INSERT_TAIL(&net->rcvpkts_free, &net->rcvpkts[i], link);
    }
}

static void destroy_rcvbuf(HvNet *net)
{
    int i;

    if (!net->rcvbuf_gpadl) {
        return;
    }

    for (i = 0; i < net->rcvbuf_slot_num; i++) {
        if (!QTAILQ_IN_USE(&net->rcvpkts[i], link)) {
            put_rcv_pkt(net, &net->rcvpkts[i]);
        }
    }

    assert(bitmap_empty(net->rcvbuf_slot_map, net->rcvbuf_slot_num));
    g_free(net->rcvbuf_slot_map);
    g_free(net->rcvpkts);

    vmbus_put_gpadl(net->rcvbuf_gpadl);
    net->rcvbuf_gpadl = NULL;
}

static void destroy_sndbuf(HvNet *net)
{
    if (!net->sndbuf_gpadl) {
        return;
    }

    vmbus_put_gpadl(net->sndbuf_gpadl);
    net->sndbuf_gpadl = NULL;
}

static void handle_init(HvNetReq *req, struct nvsp_msg_init *msg_in,
                        uint32_t len, struct nvsp_msg_header *hdr_out)
{
    struct nvsp_msg_init_complete *msg_out;

    if (len < sizeof(*msg_in) || len < sizeof(*msg_out)) {
        return;
    }
    if (!hdr_out) {
        return;
    }

    hdr_out->msg_type = NVSP_MSG_TYPE_INIT_COMPLETE;
    msg_out = (struct nvsp_msg_init_complete *)(hdr_out + 1);

    if (msg_in->min_protocol_ver > msg_in->max_protocol_ver ||
        msg_in->min_protocol_ver > NVSP_PROTOCOL_VERSION_5 ||
        msg_in->max_protocol_ver < NVSP_PROTOCOL_VERSION_1) {
        msg_out->status = NVSP_STAT_FAIL;
        return;
    }

    req->net->protocol_ver = msg_out->negotiated_protocol_ver =
        msg_in->max_protocol_ver;
    msg_out->status = NVSP_STAT_SUCCESS;
}

static void handle_ndis_ver(HvNetReq *req, struct nvsp1_msg_ndis_ver *msg_in,
                            uint32_t len)
{
    if (len < sizeof(*msg_in)) {
        return;
    }

    req->net->ndis_maj_ver = msg_in->ndis_major_ver;
    req->net->ndis_min_ver = msg_in->ndis_minor_ver;
}

static void handle_rcv_buf(HvNetReq *req, struct nvsp1_msg_rcvbuf *msg_in,
                           uint32_t len, struct nvsp_msg_header *hdr_out)
{
    HvNet *net = req->net;
    struct nvsp1_msg_rcvbuf_complete *msg_out;

    if (len < sizeof(*msg_in) || len < sizeof(*msg_out)) {
        error_report("%s: bogus msg_size: %u", __func__, len);
        return;
    }
    if (!hdr_out) {
        error_report("%s: no reply msg", __func__);
        return;
    }

    hdr_out->msg_type = NVSP_MSG1_TYPE_SEND_RECV_BUF_COMPLETE;
    msg_out = (struct nvsp1_msg_rcvbuf_complete *)(hdr_out + 1);

    net->rcvbuf_gpadl_id = msg_in->gpadl_handle;
    net->rcvbuf_id = msg_in->id;
    net->rcvbuf_gpadl = vmbus_get_gpadl(req->vmreq.chan, net->rcvbuf_gpadl_id);
    if (!net->rcvbuf_gpadl) {
        msg_out->status = NVSP_STAT_FAIL;
        error_report("%s: failed to get gpadl %u", __func__, net->rcvbuf_gpadl_id);
        return;
    }

    create_rcvbuf(net);

    msg_out->status = NVSP_STAT_SUCCESS;
    msg_out->num_sections = 1;
    msg_out->sections[0].offset = 0;
    msg_out->sections[0].sub_alloc_size = net->rcvbuf_slot_len;
    msg_out->sections[0].num_sub_allocs = net->rcvbuf_slot_num;
    msg_out->sections[0].end_offset = net->rcvbuf_slot_len *
        net->rcvbuf_slot_num;
}

static void handle_revoke_rcv_buf(HvNetReq *req,
                                  struct nvsp1_msg_revoke_rcvbuf *msg_in,
                                  uint32_t len)
{
    if (len < sizeof(*msg_in)) {
        return;
    }

    destroy_rcvbuf(req->net);
}

static void handle_snd_buf(HvNetReq *req, struct nvsp1_msg_sndbuf *msg_in,
                           uint32_t len, struct nvsp_msg_header *hdr_out)
{
    HvNet *net = req->net;
    struct nvsp1_msg_sndbuf_complete *msg_out;

    if (len < sizeof(*msg_in) || len < sizeof(*msg_out)) {
        return;
    }
    if (!hdr_out) {
        return;
    }

    hdr_out->msg_type = NVSP_MSG1_TYPE_SEND_SEND_BUF_COMPLETE;
    msg_out = (struct nvsp1_msg_sndbuf_complete *)(hdr_out + 1);

    net->sndbuf_gpadl_id = msg_in->gpadl_handle;
    net->sndbuf_id = msg_in->id;
    net->sndbuf_gpadl = vmbus_get_gpadl(req->vmreq.chan, net->sndbuf_gpadl_id);
    if (!net->sndbuf_gpadl) {
        msg_out->status = NVSP_STAT_FAIL;
        return;
    }

    msg_out->status = NVSP_STAT_SUCCESS;
    /* FIXME: effectively prohibit guest from using send buffer for now */
    msg_out->section_size = 1; //vmbus_gpadl_len(net->sndbuf_gpadl);
}

static void handle_revoke_snd_buf(HvNetReq *req,
                                  struct nvsp1_msg_revoke_sndbuf *msg_in,
                                  uint32_t len)
{
    if (len < sizeof(*msg_in)) {
        return;
    }

    destroy_sndbuf(req->net);
}

static uint32_t handle_rndis_data(HvNetReq *req, struct rndis_msg_hdr *hdr)
{
    ssize_t ret;
    HvNet *net = req->net;
    uint32_t chan_idx = vmbus_channel_idx(req->vmreq.chan);
    struct iovec *iov = req->iov;
    uint32_t num = req->iov_cnt, i;
    uint32_t doff, dlen;
    uint32_t plen = hdr->msg_len - sizeof(*hdr);
    struct rndis_packet pkt;

    if (hdr->msg_type != RNDIS_MSG_PACKET) {
        return NVSP_STAT_INVALID_RNDIS_PKT;
    }

    if (plen < sizeof(pkt)) {
        return NVSP_STAT_INVALID_RNDIS_PKT;
    }

    if (iov_to_buf(iov, num, sizeof(*hdr), &pkt, sizeof(pkt)) != sizeof(pkt)) {
        return NVSP_STAT_INVALID_RNDIS_PKT;
    }

    doff = sizeof(*hdr) + pkt.data_offset;
    dlen = pkt.data_len;
    if (doff + dlen > hdr->msg_len) {
        return NVSP_STAT_INVALID_RNDIS_PKT;
    }
#if 0
    error_report("%s: doff:%u dlen:%u", __func__, doff, dlen);
#endif
#if 0
    iov_hexdump(iov, num, stderr, "original pkt", -1);
#endif

    /* patch iovec to contain only data */
    for (; num; iov++, num--) {
        if (iov->iov_len > doff) {
            iov->iov_len -= doff;
            iov->iov_base += doff;
            break;
        }
        doff -= iov->iov_len;
    }
    for (i = 0; i < num; i++) {
        if (iov[i].iov_len >= dlen) {
            iov[i].iov_len = dlen;
            break;
        }
        dlen -= iov[i].iov_len;
    }
    i = num;
#if 0
    iov_hexdump(iov, i, stderr, "sent pkt", -1);
#endif

    ret = qemu_sendv_packet(qemu_get_subqueue(net->nic, chan_idx), iov, i);
    if (ret == 0) {
        return NVSP_STAT_BUSY;
    }
    if (ret < 0) {
        return NVSP_STAT_FAIL;
    }
    net->tx_pkts++;
    return NVSP_STAT_SUCCESS;
}

static ssize_t receive_iov(HvNet *net, VMBusChannel *chan, bool is_ctrl,
                           const struct iovec *iov, int iovcnt)
{
    size_t len = iov_size(iov, iovcnt);
    HvNetRcvPkt *pkt;
    struct {
        vmbus_pkt_xferpg hdr;
        vmbus_xferpg_range range;
    } desc;
    struct nvsp_msg msg;

    if (!net->rcvbuf_gpadl) {
        return 0;
    }
    if (!len) {
        return 0;
    }

    pkt = get_rcv_pkt(net, len);
    if (!pkt) {
        return 0;
    }

    if (vmbus_channel_reserve(chan, sizeof(desc), sizeof(msg))) {
        goto err_reserve;
    }

    desc.hdr.buffer_id = net->rcvbuf_id;
    // desc.hdr.sender_owns_set = ?;
    desc.hdr.rangecount = 1;
    desc.range.byte_count = pkt->len;
    desc.range.byte_offset = pkt->off;
    msg.hdr.msg_type = NVSP_MSG1_TYPE_SEND_RNDIS_PKT;
    msg.msg.send_rndis_pkt.channel_type = is_ctrl;
    msg.msg.send_rndis_pkt.send_buf_section_index = 0;
    msg.msg.send_rndis_pkt.send_buf_section_size = net->rcvbuf_slot_len *
        net->rcvbuf_slot_num;

#if 0
    iov_hexdump(iov, iovcnt, stderr, __func__, -1);
#endif

    vmbus_iov_to_gpadl(chan, net->rcvbuf_gpadl, pkt->off, iov, iovcnt);

    vmbus_channel_send(chan, VMBUS_PACKET_DATA_USING_XFER_PAGES,
                       &desc, sizeof(desc),
                       &msg, sizeof(msg), true, pkt - net->rcvpkts);
    return len;

err_reserve:
    put_rcv_pkt(net, pkt);
    return 0;
}

static uint32_t send_rndis_init(HvNet *net,
                                struct rndis_initialize_complete *msg,
                                uint32_t len)
{
    int i;

    assert(len >= sizeof(*msg));
    msg->req_id = net->rndis_req_id;
    msg->status = RNDIS_STATUS_SUCCESS;
    msg->major_ver = net->rndis_maj;
    msg->minor_ver = net->rndis_min;
    msg->dev_flags = 1;
    msg->medium = RNDIS_MEDIUM_802_3;
    msg->max_pkt_per_msg = 8;
    msg->max_xfer_size = net->rcvbuf_slot_len * net->rcvbuf_slot_num;
    msg->pkt_alignment_factor = 0;
    msg->af_list_offset = 0;
    msg->af_list_size = 0;
    net->state = HV_NET_INITIALIZED;

    for (i = 0; i < net->conf.peers.queues; i++) {
        qemu_flush_queued_packets(qemu_get_subqueue(net->nic, i));
    }

    return sizeof(*msg);
}

static uint32_t query_hwstatus(HvNet *net, void *info, uint32_t len)
{
    uint32_t *ret = info;
    assert(len >= sizeof(*ret));
    *ret = 0;
    return sizeof(*ret);
}

static uint32_t query_media(HvNet *net, void *info, uint32_t len)
{
    uint32_t *ret = info;
    assert(len >= sizeof(*ret));
    *ret = RNDIS_MEDIUM_802_3;
    return sizeof(*ret);
}

static uint32_t query_mtu(HvNet *net, void *info, uint32_t len)
{
    uint32_t *ret = info;
    assert(len >= sizeof(*ret));
    /* FIXME: is this available anywhere generically? */
    *ret = 1500;
    return sizeof(*ret);
}

static uint32_t query_max_tot(HvNet *net, void *info, uint32_t len)
{
    uint32_t *ret = info;
    assert(len >= sizeof(*ret));
    /* FIXME: is this available anywhere generically? */
    *ret = 1540;
    return sizeof(*ret);
}

static uint32_t query_speed(HvNet *net, void *info, uint32_t len)
{
    uint32_t *ret = info;
    assert(len >= sizeof(*ret));
    if (net->link_down) {
        *ret = 0;
    } else {
        /* FIXME: is this available anywhere generically? */
        *ret = 100000000;       /* in 100bps units */
    }
    return sizeof(*ret);
}

static uint32_t query_link_status(HvNet *net, void *info, uint32_t len)
{
    uint32_t *ret = info;
    assert(len >= sizeof(*ret));
    *ret = net->link_down ? RNDIS_MEDIA_STATE_DISCONNECTED :
        RNDIS_MEDIA_STATE_CONNECTED;
    return sizeof(*ret);
}

static uint32_t query_drv_ver(HvNet *net, void *info, uint32_t len)
{
    uint32_t *ret = info;
    assert(len >= sizeof(*ret));
    /* FIXME: what's this? */
    *ret = 1;
    return sizeof(*ret);
}

static uint32_t query_mac(HvNet *net, void *info, uint32_t len)
{
    uint8_t *ret = info;
    assert(len >= sizeof(net->conf.macaddr));
    memcpy(ret, &net->conf.macaddr, sizeof(net->conf.macaddr));
    return sizeof(net->conf.macaddr);
}

static uint32_t query_maclist(HvNet *net, void *info, uint32_t len)
{
    uint32_t *ret = info;
    assert(len >= sizeof(*ret));
    /* FIXME: support multicast */
    *ret = 0;
    return sizeof(*ret);
}

static uint32_t query_offload_hw(HvNet *net, void *info, uint32_t len)
{
    struct ndis_offload *ret = info;
    assert(len >= sizeof(*ret));
    *ret = (struct ndis_offload) {
        .header = {
            .type = NDIS_OBJECT_TYPE_OFFLOAD,
            .revision = NDIS_OFFLOAD_PARAMETERS_REVISION_1,
            .size = NDIS_OFFLOAD_SIZE_6_0,
        },
    };
    return ret->header.size;
}

static uint32_t query_xmit_ok(HvNet *net, void *info, uint32_t len)
{
    uint32_t *ret = info;
    assert(len >= sizeof(*ret));
    *ret = net->tx_pkts;
    return sizeof(*ret);
}

static uint32_t query_rcv_ok(HvNet *net, void *info, uint32_t len)
{
    uint32_t *ret = info;
    assert(len >= sizeof(*ret));
    *ret = net->rx_pkts;
    return sizeof(*ret);
}

static uint32_t query_xmit_err(HvNet *net, void *info, uint32_t len)
{
    uint32_t *ret = info;
    assert(len >= sizeof(*ret));
    *ret = 0;
    return sizeof(*ret);
}

static uint32_t query_rcv_err(HvNet *net, void *info, uint32_t len)
{
    uint32_t *ret = info;
    assert(len >= sizeof(*ret));
    *ret = 0;
    return sizeof(*ret);
}

static uint32_t query_rcv_nobuff(HvNet *net, void *info, uint32_t len)
{
    uint32_t *ret = info;
    assert(len >= sizeof(*ret));
    *ret = 0;
    return sizeof(*ret);
}

static uint32_t set_packet_filter(HvNet *net, void *info, uint32_t len)
{
    uint32_t *val = info;
    if (len < sizeof(*val)) {
        return RNDIS_STATUS_INVALID_LENGTH;
    }
    /* FIXME: support rx filter */
    if (*val != RNDIS_PACKET_TYPE_PROMISCUOUS &&
        *val != (RNDIS_PACKET_TYPE_DIRECTED | RNDIS_PACKET_TYPE_ALL_MULTICAST |
                 RNDIS_PACKET_TYPE_BROADCAST)) {
        error_report("%s: %x unsupported", __func__, *val);
        // return RNDIS_STATUS_NOT_SUPPORTED;
    }
    error_report("%s: %x", __func__, *val);
    net->rndis_packet_filter = *val;
    return RNDIS_STATUS_SUCCESS;
}

#if 0
static uint32_t set_net_addr(HvNet *net, void *info, uint32_t len)
{
    qemu_hexdump(info, stderr, __func__, len);
    return RNDIS_STATUS_SUCCESS;
}
#endif

static uint32_t set_machine_name(HvNet *net, void *info, uint32_t len)
{
    char *n;
    n = g_utf16_to_utf8(info, len / 2, NULL, NULL, NULL);
    error_report("%s: \"%s\"", __func__, n);
    g_free(n);
    return RNDIS_STATUS_SUCCESS;
}

static uint32_t set_conf_param(HvNet *net, void *info, uint32_t len)
{
    struct rndis_config_parameter_info *val = info;
    char *n;
    if (len < sizeof(*val)) {
        return RNDIS_STATUS_INVALID_LENGTH;
    }
    if (val->parameter_name_offset + val->parameter_name_length > len ||
        val->parameter_value_offset + val->parameter_value_length > len ||
        (val->parameter_type == 0 &&
         val->parameter_value_length != sizeof(uint32_t))) {
        error_report("%s: malformed config parameter", __func__);
        return RNDIS_STATUS_INVALID_LENGTH;
    }

    n = g_utf16_to_utf8(info + val->parameter_name_offset,
                        val->parameter_name_length / 2, NULL, NULL, NULL);
    if (val->parameter_type == 2) {
        char *v = g_utf16_to_utf8(info + val->parameter_value_offset,
                                  val->parameter_value_length / 2,
                                  NULL, NULL, NULL);
        error_report("%s: \"%s\" = \"%s\"", __func__, n, v);
        g_free(v);
    } else {
        uint32_t v = *(uint32_t *)(info + val->parameter_value_offset);
        error_report("%s: \"%s\" = %u", __func__, n, v);
    }
    g_free(n);
    return RNDIS_STATUS_SUCCESS;
}
static uint32_t set_offload(HvNet *net, void *info, uint32_t len)
{
    /* FIXME */
    return RNDIS_STATUS_SUCCESS;
}

static uint32_t query_oids(HvNet *net, void *info, uint32_t len);

static const struct {
    uint32_t oid;
    uint32_t (*query)(HvNet *net, void *info, uint32_t len);
    uint32_t (*set)(HvNet *net, void *info, uint32_t len);
} rndis_oid_handlers[] = {
    {.oid = RNDIS_OID_GEN_SUPPORTED_LIST, .query = query_oids},
    {.oid = RNDIS_OID_GEN_HARDWARE_STATUS, .query = query_hwstatus},
    {.oid = RNDIS_OID_GEN_MEDIA_SUPPORTED, .query = query_media},
    {.oid = RNDIS_OID_GEN_MEDIA_IN_USE, .query = query_media},
    // {.oid = RNDIS_OID_GEN_MAXIMUM_LOOKAHEAD},
    {.oid = RNDIS_OID_GEN_MAXIMUM_FRAME_SIZE, .query = query_mtu},
    {.oid = RNDIS_OID_GEN_LINK_SPEED, .query = query_speed},
    {.oid = RNDIS_OID_GEN_TRANSMIT_BUFFER_SPACE},
    {.oid = RNDIS_OID_GEN_RECEIVE_BUFFER_SPACE},
    {.oid = RNDIS_OID_GEN_TRANSMIT_BLOCK_SIZE},
    {.oid = RNDIS_OID_GEN_RECEIVE_BLOCK_SIZE},
    {.oid = RNDIS_OID_GEN_VENDOR_ID},
    {.oid = RNDIS_OID_GEN_VENDOR_DESCRIPTION},
    {.oid = RNDIS_OID_GEN_CURRENT_PACKET_FILTER, .set = set_packet_filter},
    {.oid = RNDIS_OID_GEN_CURRENT_LOOKAHEAD},
    {.oid = RNDIS_OID_GEN_DRIVER_VERSION},
    {.oid = RNDIS_OID_GEN_MAXIMUM_TOTAL_SIZE, .query = query_max_tot},
    {.oid = RNDIS_OID_GEN_PROTOCOL_OPTIONS},
    {.oid = RNDIS_OID_GEN_MAC_OPTIONS},
    {.oid = RNDIS_OID_GEN_MEDIA_CONNECT_STATUS, .query = query_link_status},
    {.oid = RNDIS_OID_GEN_MAXIMUM_SEND_PACKETS},
    {.oid = RNDIS_OID_GEN_VENDOR_DRIVER_VERSION, .query = query_drv_ver},
    // {.oid = RNDIS_OID_GEN_SUPPORTED_GUIDS},
    // {.oid = RNDIS_OID_GEN_NETWORK_LAYER_ADDRESSES, .set = set_net_addr},
    {.oid = RNDIS_OID_GEN_TRANSPORT_HEADER_OFFSET},
    {.oid = RNDIS_OID_GEN_PHYSICAL_MEDIUM, .query = query_media},
    {.oid = RNDIS_OID_GEN_MACHINE_NAME, .set = set_machine_name},
    {.oid = RNDIS_OID_GEN_RNDIS_CONFIG_PARAMETER, .set = set_conf_param},
    {.oid = RNDIS_OID_GEN_VLAN_ID},
    {.oid = RNDIS_OID_802_3_PERMANENT_ADDRESS, .query = query_mac},
    {.oid = RNDIS_OID_802_3_CURRENT_ADDRESS, .query = query_mac},
    {.oid = RNDIS_OID_802_3_MAXIMUM_LIST_SIZE, .query = query_maclist},
    {.oid = OID_TCP_OFFLOAD_HARDWARE_CAPABILITIES, .query = query_offload_hw},
    {.oid = OID_TCP_OFFLOAD_PARAMETERS, .set = set_offload},
    {.oid = RNDIS_OID_GEN_XMIT_OK, .query = query_xmit_ok},
    {.oid = RNDIS_OID_GEN_RCV_OK, .query = query_rcv_ok},
    {.oid = RNDIS_OID_GEN_XMIT_ERROR, .query = query_xmit_err},
    {.oid = RNDIS_OID_GEN_RCV_ERROR, .query = query_rcv_err},
    {.oid = RNDIS_OID_GEN_RCV_NO_BUFFER, .query = query_rcv_nobuff},
};

static uint32_t query_oids(HvNet *net, void *info, uint32_t len)
{
    int i;
    uint32_t *ret = info;
    assert(len >= ARRAY_SIZE(rndis_oid_handlers) * sizeof(uint32_t));
    for (i = 0; i < ARRAY_SIZE(rndis_oid_handlers); i++) {
        ret[i] = rndis_oid_handlers[i].oid;
    }
    return i * sizeof(uint32_t);
}

static uint32_t send_rndis_query(HvNet *net, struct rndis_query_complete *msg,
                                 uint32_t len)
{
    int i;
    void *info;
    assert(len >= sizeof(*msg));

    msg->req_id = net->rndis_req_id;
    msg->info_buf_offset = sizeof(*msg);
    msg->info_buflen = 0;
    msg->status = RNDIS_STATUS_SUCCESS;
    info = msg + 1;
    len -= sizeof(*msg);

    for (i = 0; i < ARRAY_SIZE(rndis_oid_handlers); i++) {
        if (rndis_oid_handlers[i].oid == net->rndis_query_oid) {
            if (rndis_oid_handlers[i].query) {
                msg->info_buflen = rndis_oid_handlers[i].query(net, info, len);
                goto out;
            }
            break;
        }
    }

    error_report("%s: +++++++++ unknown query: %#x", __func__, net->rndis_query_oid);
    msg->status = RNDIS_STATUS_NOT_RECOGNIZED;
out:
#if 0
    error_report("%s: +++++++++ query complete: %#x %u", __func__,
                 net->rndis_query_oid, msg->info_buflen);
#endif
    return msg->info_buf_offset + msg->info_buflen;
}

static uint32_t send_rndis_set(HvNet *net, struct rndis_set_complete *msg,
                               uint32_t len)
{
    assert(len >= sizeof(*msg));
    msg->req_id = net->rndis_req_id;
    msg->status = net->rndis_set_status;
    return sizeof(*msg);
}

static uint32_t send_rndis_keepalive(HvNet *net,
                                     struct rndis_keepalive_complete *msg,
                                     uint32_t len)
{
    assert(len >= sizeof(*msg));
    msg->req_id = net->rndis_req_id;
    msg->status = RNDIS_STATUS_SUCCESS;
#if 0
    error_report("%s: %u", __func__, msg->req_id);
#endif
    return sizeof(*msg);
}

static bool send_rndis_ctrl(HvNet *net, VMBusChannel *chan)
{
    struct rndis_msg_hdr hdr;
    struct iovec iov[2];
    uint32_t len = 0;
    uint64_t msgbuf[32];
    void *msg = msgbuf;

#if 0
    error_report("%s >>>>: %x", __func__, net->rndis_ctl);
#endif

    memset(msgbuf, 0, sizeof(msgbuf));

    switch (net->rndis_ctl) {
    case 0:
        return true;
    case RNDIS_MSG_INIT_C:
        len = send_rndis_init(net, msg, sizeof(msgbuf));
        break;
    case RNDIS_MSG_QUERY_C:
        len = send_rndis_query(net, msg, sizeof(msgbuf));
        break;
    case RNDIS_MSG_SET_C:
        len = send_rndis_set(net, msg, sizeof(msgbuf));
        break;
    case RNDIS_MSG_RESET_C:
        error_report("%s: RESET unsupported", __func__);
        break;
    case RNDIS_MSG_KEEPALIVE_C:
        len = send_rndis_keepalive(net, msg, sizeof(msgbuf));
        break;
    default:
        error_report("%s: +++++ unrecognized rndis ctrl: %#x", __func__, net->rndis_ctl);
    }

    hdr.msg_type = net->rndis_ctl;
    hdr.msg_len = sizeof(hdr) + len;
    iov[0].iov_base = &hdr;
    iov[0].iov_len = sizeof(hdr);
    iov[1].iov_base = msg;
    iov[1].iov_len = len;
    if (receive_iov(net, chan, true, iov, 2) == hdr.msg_len) {
        /* message sent to guest, clear pending rndis response */
        net->rndis_ctl = 0;
    }
    return !net->rndis_ctl;
}

static uint32_t handle_rndis_init(HvNet *net,
                                  struct rndis_initialize_request *msg,
                                  uint32_t len)
{
    if (net->state != HV_NET_RESET) {
        return NVSP_STAT_INVALID_RNDIS_PKT;
    }
    if (len != sizeof(*msg)) {
        return NVSP_STAT_INVALID_RNDIS_PKT;
    }
    net->rndis_req_id = msg->req_id;
    net->rndis_maj = msg->major_ver;
    net->rndis_min = msg->minor_ver;
    return NVSP_STAT_SUCCESS;
}

static uint32_t handle_rndis_query(HvNet *net, struct rndis_query_request *msg,
                                   uint32_t len)
{
    if (net->state != HV_NET_INITIALIZED) {
        return NVSP_STAT_INVALID_RNDIS_PKT;
    }
    if (len < sizeof(*msg)) {
        return NVSP_STAT_INVALID_RNDIS_PKT;
    }
    if (msg->info_buflen > sizeof(net->rndis_query_info)) {
        error_report("%s: too big RNDIS query info: %u", __func__, msg->info_buflen);
        return NVSP_STAT_INVALID_RNDIS_PKT;
    }
    if (msg->info_buf_offset + msg->info_buflen > len) {
        error_report("%s: malformed RNDIS query: %u + %u > %u", __func__,
                     msg->info_buf_offset, msg->info_buflen, len);
        return NVSP_STAT_INVALID_RNDIS_PKT;
    }

    net->rndis_req_id = msg->req_id;
    net->rndis_query_oid = msg->oid;
    memcpy(net->rndis_query_info, (void *)msg + msg->info_buf_offset,
           msg->info_buflen);
    /* msg->dev_vc_handle */
    return NVSP_STAT_SUCCESS;
}

static uint32_t handle_rndis_set(HvNet *net, struct rndis_set_request *msg,
                                 uint32_t len)
{
    int i;
    void *info;

    if (net->state != HV_NET_INITIALIZED) {
        return NVSP_STAT_INVALID_RNDIS_PKT;
    }
    if (len < sizeof(*msg)) {
        return NVSP_STAT_INVALID_RNDIS_PKT;
    }
    if (msg->info_buf_offset + msg->info_buflen > len) {
        error_report("%s: malformed RNDIS set: %u + %u > %u", __func__,
                     msg->info_buf_offset, msg->info_buflen, len);
        return NVSP_STAT_INVALID_RNDIS_PKT;
    }

    net->rndis_req_id = msg->req_id;
    info = (void *)msg + msg->info_buf_offset;

    for (i = 0; i < ARRAY_SIZE(rndis_oid_handlers); i++) {
        if (rndis_oid_handlers[i].oid == msg->oid) {
            if (rndis_oid_handlers[i].set) {
                net->rndis_set_status =
                    rndis_oid_handlers[i].set(net, info, msg->info_buflen);
                return NVSP_STAT_SUCCESS;
            }
            break;
        }
    }

    error_report("%s: +++++ unknown set: %#x", __func__, msg->oid);
    net->rndis_set_status = RNDIS_STATUS_NOT_RECOGNIZED;
    return NVSP_STAT_SUCCESS;
}

static uint32_t handle_rndis_keepalive(HvNet *net,
                                       struct rndis_keepalive_request *msg,
                                       uint32_t len)
{
    if (net->state != HV_NET_INITIALIZED) {
        return NVSP_STAT_INVALID_RNDIS_PKT;
    }
    if (len < sizeof(*msg)) {
        return NVSP_STAT_INVALID_RNDIS_PKT;
    }

#if 0
    error_report("%s: %u", __func__, msg->req_id);
#endif
    net->rndis_req_id = msg->req_id;
    return NVSP_STAT_SUCCESS;
}

static uint32_t handle_rndis_ctrl(HvNetReq *req, struct rndis_msg_hdr *hdr)
{
    HvNet *net = req->net;
    uint32_t len = hdr->msg_len - sizeof(*hdr);
    uint64_t msgbuf[32];
    void *msg = msgbuf;
    uint32_t ret;

#if 0
    error_report("%s >>>>", __func__);
#endif

    if (!net->rcvbuf_gpadl) {
        return NVSP_STAT_INVALID_RNDIS_PKT;
    }

    if (len > sizeof(msgbuf)) {
        error_report("%s: msgbuf too small: %u", __func__, len);
        return NVSP_STAT_INVALID_RNDIS_PKT;
    }

    if (iov_to_buf(req->iov, req->iov_cnt, sizeof(*hdr), msg, len) != len) {
        return NVSP_STAT_INVALID_RNDIS_PKT;
    }

    switch(hdr->msg_type) {
    case RNDIS_MSG_INIT:
        ret = handle_rndis_init(net, msg, len);
        break;
    case RNDIS_MSG_QUERY:
        ret = handle_rndis_query(net, msg, len);
        break;
    case RNDIS_MSG_SET:
        ret = handle_rndis_set(net, msg, len);
        break;
    case RNDIS_MSG_HALT:
        error_report("%s: HALT unsupported", __func__);
        return NVSP_STAT_INVALID_RNDIS_PKT;
    case RNDIS_MSG_RESET:
        error_report("%s: RESET unsupported", __func__);
        return NVSP_STAT_INVALID_RNDIS_PKT;
    case RNDIS_MSG_KEEPALIVE:
        ret = handle_rndis_keepalive(net, msg, len);
        break;
    default:
        error_report("%s: +++++ unrecognised rndis msg: %#x", __func__,
                     hdr->msg_type);
        return NVSP_STAT_INVALID_RNDIS_PKT;
    }

    if (ret == NVSP_STAT_SUCCESS) {
        net->rndis_ctl = hdr->msg_type | RNDIS_MSG_COMPLETION;
    }
    return ret;
}

static void handle_rndis_pkt(HvNetReq *req, struct nvsp1_msg_rndis_pkt *msg_in,
                             uint32_t len, struct nvsp_msg_header *hdr_out)
{
    struct nvsp1_msg_rndis_pkt_complete *msg_out;
    struct rndis_msg_hdr rhdr;

#if 0
    error_report("%s >>>> iov:%p num:%u secidx:%u secsz:%u", __func__, vmreq->iov, vmreq->num, msg_in->send_buf_section_index, msg_in->send_buf_section_size);
#endif

    if (len < sizeof(*msg_in) || len < sizeof(*msg_out)) {
        return;
    }
    if (!hdr_out) {
        return;
    }

    hdr_out->msg_type = NVSP_MSG1_TYPE_SEND_RNDIS_PKT_COMPLETE;
    msg_out = (struct nvsp1_msg_rndis_pkt_complete *)(hdr_out + 1);
    msg_out->status = NVSP_STAT_INVALID_RNDIS_PKT;

    /* FIXME: support send buffer - is it worth while? */
    if (msg_in->send_buf_section_index != NETVSC_INVALID_INDEX) {
        return;
    }

    if (iov_to_buf(req->iov, req->iov_cnt, 0, &rhdr, sizeof(rhdr)) !=
        sizeof(rhdr)) {
        return;
    }

    if (rhdr.msg_len < sizeof(rhdr)) {
        return;
    }

    switch (msg_in->channel_type) {
    case 0:
        msg_out->status = handle_rndis_data(req, &rhdr);
        break;
    case 1:
        msg_out->status = handle_rndis_ctrl(req, &rhdr);
        break;
    default:
        error_report("%s: unexpected RNDIS packet channel: %u", __func__, msg_in->channel_type);
    }
}

static void hv_net_handle_req(HvNetReq *req)
{
    VMBusChanReq *vmreq = &req->vmreq;
    void *msg = vmreq->msg;
    void *reply = vmreq->comp;
    uint32_t len = vmreq->msglen;
    struct nvsp_msg_header *nhdr = msg;

#if 0
    error_report("%s >>>>", __func__);
#endif

    if (vmreq->pkt_type == VMBUS_PACKET_COMP) {
        put_rcv_pkt_by_tr_id(req->net, vmreq->transaction_id);
        goto complete;
    }

    if (len < sizeof(*nhdr)) {
        goto complete;
    }
    len -= sizeof(*nhdr);
    msg += sizeof(*nhdr);

#if 0
    error_report("%s >>>> msg: %u", __func__, nhdr->msg_type);
#endif
    switch (nhdr->msg_type) {
    case NVSP_MSG_TYPE_INIT:
        handle_init(req, msg, len, reply);
        break;
    case NVSP_MSG1_TYPE_SEND_NDIS_VER:
        handle_ndis_ver(req, msg, len);
        break;
    case NVSP_MSG1_TYPE_SEND_RECV_BUF:
        handle_rcv_buf(req, msg, len, reply);
        break;
    case NVSP_MSG1_TYPE_REVOKE_RECV_BUF:
        handle_revoke_rcv_buf(req, msg, len);
        break;
    case NVSP_MSG1_TYPE_SEND_SEND_BUF:
        handle_snd_buf(req, msg, len, reply);
        break;
    case NVSP_MSG1_TYPE_REVOKE_SEND_BUF:
        handle_revoke_snd_buf(req, msg, len);
        break;
    case NVSP_MSG1_TYPE_SEND_RNDIS_PKT:
        handle_rndis_pkt(req, msg, len, reply);
        break;
    case NVSP_MSG2_TYPE_SEND_NDIS_CONFIG:
        break;
    default:
        error_report("%s: unexpected message type: %u", __func__,
                     nhdr->msg_type);
        break;
    }

complete:
    complete_req(req);
}

static bool send_link_status(HvNet *net, VMBusChannel *chan)
{
    NetClientState *nc = net->nic->ncs;
    struct rndis_msg_hdr hdr;
    struct rndis_indicate_status msg = {0};
    struct iovec iov[2];

#if 0
    error_report("%s >>>>", __func__);
#endif

    if (net->state != HV_NET_INITIALIZED) {
        return true;
    }
    if (net->link_down == nc->link_down) {
        return true;
    }

    msg.status = nc->link_down ?  RNDIS_STATUS_MEDIA_DISCONNECT :
        RNDIS_STATUS_MEDIA_CONNECT;
    hdr.msg_type = RNDIS_MSG_INDICATE;
    hdr.msg_len = sizeof(hdr) + sizeof(msg);
    iov[0].iov_base = &hdr;
    iov[0].iov_len = sizeof(hdr);
    iov[1].iov_base = &msg;
    iov[1].iov_len = sizeof(msg);
    if (receive_iov(net, chan, true, iov, 2) == hdr.msg_len) {
        /* message sent to guest, clear pending status update */
        net->link_down = nc->link_down;
    }

    return net->link_down == nc->link_down;
}

static void hv_net_notify_cb(VMBusChannel *chan)
{
    HvNet *net = HV_NET(vmbus_channel_device(chan));

    for (;;) {
        HvNetReq *req;

        if (!send_rndis_ctrl(net, chan)) {
            return;
        }

        if (!send_link_status(net, chan)) {
            return;
        }

        req = vmbus_channel_recv(chan, sizeof(*req));
        if (!req) {
            return;
        }

        if (hv_net_init_req(net, req) < 0) {
            vmbus_release_req(req);
            return;
        }
        hv_net_handle_req(req);
    }
}

static int hv_net_can_receive(NetClientState *nc)
{
    HvNet *n = qemu_get_nic_opaque(nc);
    return n->state == HV_NET_INITIALIZED && n->rcvbuf_gpadl;
}

static ssize_t hv_net_receive(NetClientState *nc,
                              const struct iovec *iov, int iovcnt)
{
    HvNet *n = qemu_get_nic_opaque(nc);
    VMBusChannel *chan = vmbus_device_channel(VMBUS_DEVICE(n),
                                              nc->queue_index);
    struct rndis_packet pkt = {
	.data_offset = sizeof(pkt),
	.data_len = iov_size(iov, iovcnt),
    };
    struct rndis_msg_hdr hdr = {
        .msg_type = RNDIS_MSG_PACKET,
	.msg_len = sizeof(hdr) + pkt.data_offset + pkt.data_len,
    };
    struct iovec rndis_iov[64];
    unsigned hiovcnt, piovcnt;

    rndis_iov[0].iov_base = &hdr;
    rndis_iov[0].iov_len = sizeof(hdr);
    rndis_iov[1].iov_base = &pkt;
    rndis_iov[1].iov_len = sizeof(pkt);
    hiovcnt = 2;

    piovcnt = iov_copy(rndis_iov + hiovcnt, ARRAY_SIZE(rndis_iov) - hiovcnt,
                       iov, iovcnt, 0, -1);
    if (piovcnt != iovcnt) {
        error_report("%s: packet too big: %u", __func__, iovcnt);
    }

    n->rx_pkts++;
    return receive_iov(n, chan, false, rndis_iov, hiovcnt + piovcnt);
}

static void hv_net_set_link_status(NetClientState *nc)
{
    HvNet *n = qemu_get_nic_opaque(nc);
    VMBusChannel *chan = vmbus_device_channel(VMBUS_DEVICE(n),
                                              nc->queue_index);
    if (n->link_down != nc->link_down) {
        vmbus_notify_channel(chan);
    }
}

static NetClientInfo hv_net_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = hv_net_can_receive,
    .receive_iov = hv_net_receive,
    .link_status_changed = hv_net_set_link_status,
#if 0
    .query_rx_filter = hv_net_query_rxfilter,
#endif
};

static void hv_net_dev_realize(VMBusDevice *vdev, Error **errp)
{
    HvNet *net = HV_NET(vdev);

    net->nic = qemu_new_nic(&hv_net_info, &net->conf,
                            object_get_typename(OBJECT(net)),
                            DEVICE(net)->id, net);
    return;
}

static void hv_net_reset(HvNet *net)
{
    destroy_rcvbuf(net);
    destroy_sndbuf(net);
    net->state = HV_NET_RESET;

    memset(&net->reset_start, 0, sizeof(HvNet) - offsetof(HvNet, reset_start));
}

static void hv_net_dev_reset(VMBusDevice *vdev)
{
    HvNet *net = HV_NET(vdev);
    hv_net_reset(net);
}

static uint16_t hv_net_num_channels(VMBusDevice *dev)
{
    return HV_NET(dev)->conf.peers.queues;
}

static void hv_net_close_channel(VMBusDevice *vdev)
{
    HvNet *net = HV_NET(vdev);
    hv_net_reset(net);
}

static void hv_net_dev_unrealize(VMBusDevice *vdev, Error **errp)
{
    HvNet *net = HV_NET(vdev);
    hv_net_reset(net);
    qemu_del_nic(net->nic);
}

static int hv_net_dev_post_load(void *opaque, int version_id)
{
    HvNet *net = opaque;
    VMBusDevice *vdev = VMBUS_DEVICE(net);
    VMBusChannel *chan = vdev->channels;        /* channel #0 is ok */

    if (net->rcvbuf_gpadl_id) {
        uint32_t gpadl_len;
        int i;

        net->rcvbuf_gpadl = vmbus_get_gpadl(chan, net->rcvbuf_gpadl_id);
        if (!net->rcvbuf_gpadl) {
            error_report("%s: rcv gpadl %u not found", __func__,
                         net->rcvbuf_gpadl_id);
            return -1;
        }

        gpadl_len = vmbus_gpadl_len(net->rcvbuf_gpadl);
        if (net->rcvbuf_slot_num != gpadl_len / net->rcvbuf_slot_len) {
            error_report("%s: rcv gpadl %u: unexpected length %u != %u / %u",
                         __func__, net->rcvbuf_gpadl_id, net->rcvbuf_slot_num,
                         gpadl_len, net->rcvbuf_slot_len);
            return -1;
        }
        net->rcvbuf_slot_map = bitmap_new(net->rcvbuf_slot_num);
        QTAILQ_INIT(&net->rcvpkts_free);
        for (i = 0; i < net->rcvbuf_slot_num; i++) {
            HvNetRcvPkt *pkt = &net->rcvpkts[i];
            if (!pkt->len) {
                QTAILQ_INSERT_TAIL(&net->rcvpkts_free, pkt, link);
            } else {
                uint32_t start = pkt->off / net->rcvbuf_slot_len;
                uint32_t nr = DIV_ROUND_UP(pkt->len, net->rcvbuf_slot_len);
                bitmap_set(net->rcvbuf_slot_map, start, nr);
            }
        }
    }

    if (net->sndbuf_gpadl_id) {
        net->sndbuf_gpadl = vmbus_get_gpadl(chan, net->sndbuf_gpadl_id);
        if (!net->sndbuf_gpadl) {
            error_report("%s: snd gpadl %u not found", __func__,
                         net->rcvbuf_gpadl_id);
            return -1;
        }
    }

    return 0;
}

static const VMStateDescription vmstate_hv_net_rcvpkt = {
    .name = TYPE_HV_NET "/rcvpkt",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(off, HvNetRcvPkt),
        VMSTATE_UINT32(len, HvNetRcvPkt),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_hv_net = {
    .name = TYPE_HV_NET,
    .version_id = 0,
    .minimum_version_id = 0,
    .post_load = hv_net_dev_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(parent, HvNet, 0, vmstate_vmbus_dev, VMBusDevice),
        VMSTATE_UINT32(state, HvNet),
        VMSTATE_UINT16(sndbuf_id, HvNet),
        VMSTATE_UINT32(sndbuf_gpadl_id, HvNet),
        VMSTATE_UINT16(rcvbuf_id, HvNet),
        VMSTATE_UINT32(rcvbuf_gpadl_id, HvNet),
        VMSTATE_INT32(rcvbuf_slot_num, HvNet),
        VMSTATE_UINT16(rcvbuf_slot_len, HvNet),
        VMSTATE_STRUCT_VARRAY_ALLOC(rcvpkts, HvNet, rcvbuf_slot_num, 0,
                                    vmstate_hv_net_rcvpkt, HvNetRcvPkt),
        VMSTATE_UINT32(protocol_ver, HvNet),
        VMSTATE_UINT32(ndis_maj_ver, HvNet),
        VMSTATE_UINT32(ndis_min_ver, HvNet),
        VMSTATE_UINT32(rndis_ctl, HvNet),
        VMSTATE_UINT32(rndis_req_id, HvNet),
        VMSTATE_UINT32(rndis_maj, HvNet),
        VMSTATE_UINT32(rndis_min, HvNet),
        VMSTATE_UINT32(max_xfer_size, HvNet),
        VMSTATE_UINT32(rndis_query_oid, HvNet),
        VMSTATE_UINT64_ARRAY(rndis_query_info, HvNet, RNDIS_QUERY_INFO_LEN),
        VMSTATE_UINT32(rndis_query_info_len, HvNet),
        VMSTATE_UINT32(rndis_set_status, HvNet),
        VMSTATE_UINT32(rndis_packet_filter, HvNet),
        VMSTATE_BOOL(link_down, HvNet),
        VMSTATE_UINT32(rx_pkts, HvNet),
        VMSTATE_UINT32(tx_pkts, HvNet),
        VMSTATE_END_OF_LIST()
    }
};

static Property hv_net_properties[] = {
    DEFINE_PROP_UUID("instanceid", HvNet, parent.instanceid),
    DEFINE_NIC_PROPERTIES(HvNet, conf),
    DEFINE_PROP_UINT16("rcvbuf-slot-len", HvNet, rcvbuf_slot_len, 0x6c0),
    DEFINE_PROP_END_OF_LIST(),
};

static void hv_net_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VMBusDeviceClass *vdc = VMBUS_DEVICE_CLASS(klass);

    qemu_uuid_parse(HV_NET_GUID, &vdc->classid);
    dc->props = hv_net_properties;
    dc->fw_name = "ethernet";
    dc->vmsd = &vmstate_hv_net;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
    vdc->channel_flags = VMBUS_CHANNEL_SERVER_SUPPORTS_TRANSFER_PAGES |
        VMBUS_CHANNEL_SERVER_SUPPORTS_GPADLS;
    vdc->vmdev_realize = hv_net_dev_realize;
    vdc->vmdev_unrealize = hv_net_dev_unrealize;
    vdc->vmdev_reset = hv_net_dev_reset;
    vdc->num_channels = hv_net_num_channels;
    vdc->close_channel = hv_net_close_channel;
    vdc->chan_notify_cb = hv_net_notify_cb;
}

static void hv_net_instance_init(Object *obj)
{
    HvNet *s = HV_NET(obj);
    device_add_bootindex_property(obj, &s->conf.bootindex,
                                  "bootindex", "/ethernet-phy@0",
                                  DEVICE(obj), NULL);
}

static const TypeInfo hv_net_type_info = {
    .name = TYPE_HV_NET,
    .parent = TYPE_VMBUS_DEVICE,
    .instance_size = sizeof(HvNet),
    .class_init = hv_net_class_init,
    .instance_init = hv_net_instance_init,
};

static void hv_net_register_types(void)
{
    type_register_static(&hv_net_type_info);
}

type_init(hv_net_register_types)
