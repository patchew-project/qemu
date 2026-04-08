/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Minimal MCTP-over-SMBus/I2C responder (I2C slave).
 * - Replies via SMBus block-read (repeated-start)
 *   and (optionally) reverse-master push.
 * - Implements a tiny subset of MCTP Control.
 */

#include "qemu/osdep.h"
#include "hw/i2c/i2c.h"
#include "hw/qdev-properties.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include <glib.h>
#include "hw/qdev-properties-system.h"
#include "qemu/uuid.h"

#include "hw/misc/mctp-i2c-responder.h"

#define MCTP_I2C_COMMANDCODE 0x0f
#define MCTP_I2C_MINLEN 8

#define MCTP_HDR_FLAG_SOM BIT(7)
#define MCTP_HDR_FLAG_EOM BIT(6)
#define MCTP_HDR_FLAG_TO BIT(3)
#define MCTP_HDR_FLAGS 0x38
#define MCTP_HDR_SEQ_SHIFT 4
#define MCTP_HDR_SEQ_MASK 0x03
#define MCTP_HDR_TAG_SHIFT 0
#define MCTP_HDR_TAG_MASK 0x07

#define MCTP_CC_SUCCESS 0x00
#define MCTP_CC_INVALID_LENGTH 0x03
#define MCTP_CC_UNSUPPORTED 0x05

struct mctp_i2c_hdr {
    uint8_t dest_slave;
    uint8_t command;
    /* Count of bytes following byte_count, excluding PEC */
    uint8_t byte_count;
    uint8_t source_slave;
};

/* MCTP packet definitions */
struct mctp_hdr {
    uint8_t ver;
    uint8_t dest;
    uint8_t src;
    uint8_t flags_seq_tag;
};

struct mctp_payload {
    uint8_t msg_type;
    uint8_t data[];
};

static void mctp_i2c_init_handlers_default(MctpI2cResponder *s);

void mctp_i2c_responder_register_msg_handler(
    MctpI2cResponder *s, uint8_t msg_type,
    MctpI2cResponderMsgHandler handler)
{
    g_hash_table_replace(s->handlers,
                         GINT_TO_POINTER((int)msg_type),
                         (gpointer)handler);
}

const uint8_t *mctp_i2c_responder_get_rx_buf(MctpI2cResponder *s, size_t *len)
{
    if (!s->mctp_package.rx_buf) {
        if (len) {
            *len = 0;
        }
        return NULL;
    }

    if (len) {
        *len = s->mctp_package.rx_buf->len;
    }
    return (const uint8_t *)s->mctp_package.rx_buf->data;
}

bool mctp_i2c_responder_tx_begin(MctpI2cResponder *s, uint8_t msg_type)
{
    if (!s->mctp_package.tx_buf) {
        return false;
    }

    g_array_set_size(s->mctp_package.tx_buf, 0);
    g_array_append_val(s->mctp_package.tx_buf, msg_type);
    return true;
}

bool mctp_i2c_responder_tx_append(MctpI2cResponder *s,
                                  const void *buf, size_t len)
{
    if (!s->mctp_package.tx_buf || !buf || len == 0) {
        return false;
    }

    g_array_append_vals(s->mctp_package.tx_buf, buf, len);
    return true;
}

static void mctp_i2c_responder_cleanup_after_handler(MctpI2cResponder *s)
{
    if (s->mctp_package.rx_buf) {
        g_array_free(s->mctp_package.rx_buf, true);
        s->mctp_package.rx_buf = NULL;
    }

    if (s->mctp_package.tx_buf && s->mctp_package.tx_buf->len == 0) {
        g_array_free(s->mctp_package.tx_buf, true);
        s->mctp_package.tx_buf = NULL;
    }
}

static uint8_t crc8_pec(const uint8_t *buf, size_t len)
{
    uint8_t crc = 0;

    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];

        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ?
                (uint8_t)((crc << 1) ^ 0x07) :
                (uint8_t)(crc << 1);
        }
    }

    return crc;
}

static void mctp_ctrl_handler(MctpI2cResponder *s)
{
    assert(s->mctp_package.rx_buf != NULL);
    assert(s->mctp_package.tx_buf != NULL);

#define MCTP_CTRL_HDR_FLAG_REQUEST 0x80
#define MCTP_CTRL_CMD_SET_ENDPOINT_ID 0x01
#define MCTP_CTRL_CMD_GET_ENDPOINT_ID 0x02
#define MCTP_CTRL_CMD_GET_ENDPOINT_UUID 0x03
#define MCTP_CTRL_CMD_GET_VERSION_SUPPORT 0x04
#define MCTP_CTRL_CMD_GET_MESSAGE_TYPE_SUPPORT 0x05
#define MCTP_PHYS_BINDING_SMBUS 0x01

    /* Parse rx payload */
    struct mctp_payload *rx_payload =
        (struct mctp_payload *)s->mctp_package.rx_buf->data;
    size_t rx_payload_len = s->mctp_package.rx_buf->len;

    if (rx_payload_len < 1 + 2) {
        return;
    }

    const uint8_t *rx_ctrl = (const uint8_t *)rx_payload->data;
    uint8_t rq_dgram_inst = rx_ctrl[0];
    uint8_t cmd = rx_ctrl[1];

    if (!mctp_i2c_responder_tx_begin(s, MCTP_MSG_TYPE_CTRL)) {
        return;
    }

    /* Response header: clear request bit, keep instance id. */
    uint8_t resp_hdr[2] = {
        (uint8_t)(rq_dgram_inst & ~MCTP_CTRL_HDR_FLAG_REQUEST),
        cmd,
    };
    (void)mctp_i2c_responder_tx_append(s, resp_hdr, sizeof(resp_hdr));

    switch (cmd) {
    case MCTP_CTRL_CMD_GET_ENDPOINT_ID: {
        uint8_t cc = MCTP_CC_SUCCESS;
        uint8_t eid = s->eid;
        uint8_t eid_type = MCTP_PHYS_BINDING_SMBUS;
        uint8_t medium_data = 0;

        (void)mctp_i2c_responder_tx_append(s, &cc, sizeof(cc));
        (void)mctp_i2c_responder_tx_append(s, &eid, sizeof(eid));
        (void)mctp_i2c_responder_tx_append(s, &eid_type, sizeof(eid_type));
        (void)mctp_i2c_responder_tx_append(s, &medium_data,
                                           sizeof(medium_data));
        break;
    }
    case MCTP_CTRL_CMD_SET_ENDPOINT_ID: {
        /* ctrl_hdr(2) + operation(1) + eid(1) */
        if (rx_payload_len < 1 + 4) {
            uint8_t cc = MCTP_CC_INVALID_LENGTH;
            (void)mctp_i2c_responder_tx_append(s, &cc, sizeof(cc));
            break;
        }

        uint8_t operation = rx_ctrl[2];
        uint8_t new_eid = rx_ctrl[3];
        (void)operation; /* framework only */

        s->eid = new_eid;

        uint8_t cc = MCTP_CC_SUCCESS;
        uint8_t status = 0;
        uint8_t eid_set = s->eid;
        uint8_t eid_pool_size = 0;

        (void)mctp_i2c_responder_tx_append(s, &cc, sizeof(cc));
        (void)mctp_i2c_responder_tx_append(s, &status, sizeof(status));
        (void)mctp_i2c_responder_tx_append(s, &eid_set, sizeof(eid_set));
        (void)mctp_i2c_responder_tx_append(s, &eid_pool_size,
                                           sizeof(eid_pool_size));
        break;
    }
    case MCTP_CTRL_CMD_GET_ENDPOINT_UUID: {
        uint8_t cc = MCTP_CC_SUCCESS;
        (void)mctp_i2c_responder_tx_append(s, &cc, sizeof(cc));
        (void)mctp_i2c_responder_tx_append(s, s->uuid.data,
                                           sizeof(s->uuid.data));
        break;
    }
    case MCTP_CTRL_CMD_GET_VERSION_SUPPORT: {
        uint8_t cc = MCTP_CC_SUCCESS;
        uint8_t number_of_entries = 1;
        (void)mctp_i2c_responder_tx_append(s, &cc, sizeof(cc));
        (void)mctp_i2c_responder_tx_append(s, &number_of_entries,
                                           sizeof(number_of_entries));
        break;
    }
    case MCTP_CTRL_CMD_GET_MESSAGE_TYPE_SUPPORT: {
        uint8_t cc = MCTP_CC_SUCCESS;
        (void)mctp_i2c_responder_tx_append(s, &cc, sizeof(cc));

        uint8_t cnt = 0;
        uint8_t types[UINT8_MAX];

        if (s->handlers) {
            GHashTableIter iter;
            gpointer key, value;
            g_hash_table_iter_init(&iter, s->handlers);
            while (g_hash_table_iter_next(&iter, &key, &value)) {
                if (cnt == UINT8_MAX) {
                    break;
                }
                types[cnt++] = (uint8_t)GPOINTER_TO_INT(key);
            }
        }

        (void)mctp_i2c_responder_tx_append(s, &cnt, sizeof(cnt));
        for (int i = 0; i < cnt; i++) {
            (void)mctp_i2c_responder_tx_append(s, &types[i], sizeof(types[i]));
        }
        break;
    }
    default: {
        uint8_t cc = MCTP_CC_UNSUPPORTED;
        (void)mctp_i2c_responder_tx_append(s, &cc, sizeof(cc));
        break;
    }
    }
}

static bool mctp_i2c_build_tx(MctpI2cResponder *s)
{
    struct mctp_per_tx *pkt;
    size_t tx_payload_len = s->mctp_package.tx_buf->len;
    size_t tx_offset = 0;
    struct mctp_i2c_hdr i2c_hdr;
    struct mctp_hdr mctp_hdr;

    uint8_t seq = 0;
    uint8_t pec = 0;

    /*
     * A package should have:
     * * mctp i2c header ==> 4 bytes, but byte_count only add 1 byte
     * * mctp header ==> 4 bytes
     * * payload ==> per_tx_len bytes
     * * PEC ==> do not add to byte_count
     */
    while (tx_offset < tx_payload_len) {
        size_t per_tx_len = 0;

        pkt = g_malloc0(sizeof(struct mctp_per_tx));
        memset(&i2c_hdr, 0, sizeof(i2c_hdr));
        memset(&mctp_hdr, 0, sizeof(mctp_hdr));

        /* mctp i2c header */
        i2c_hdr.dest_slave = s->mctp_package.metadata.source_slave & 0xfe;
        i2c_hdr.command = MCTP_I2C_COMMANDCODE;
        i2c_hdr.byte_count = 0; /* fill later */
        i2c_hdr.source_slave = s->mctp_package.metadata.dest_slave >> 1;

        /* mctp header */
        if (tx_offset == 0) {
            mctp_hdr.flags_seq_tag |= MCTP_HDR_FLAG_SOM;
        }

        if (tx_offset + s->mtu >= tx_payload_len) {
            mctp_hdr.flags_seq_tag |= MCTP_HDR_FLAG_EOM;
        }

        mctp_hdr.flags_seq_tag |= s->mctp_package.metadata.msg_tag;
        mctp_hdr.flags_seq_tag |=
            ((seq & MCTP_HDR_SEQ_MASK) << MCTP_HDR_SEQ_SHIFT);
        mctp_hdr.ver = 1;
        mctp_hdr.dest = s->mctp_package.metadata.src;
        mctp_hdr.src = s->eid;

        if (tx_offset + s->mtu >= tx_payload_len) {
            per_tx_len = tx_payload_len - tx_offset;
        } else {
            per_tx_len = s->mtu;
        }

        /*
         * fill byte_count
         * mctp i2c header ==> 1 bytes
         * (exclude dest_slave, command, byte_count self)
         * mctp header ==> 4 bytes,
         */
        i2c_hdr.byte_count = per_tx_len + 1 + 4;

        /* Fill payload. */
        memcpy(pkt->buf, &i2c_hdr, sizeof(i2c_hdr));
        pkt->len += sizeof(i2c_hdr);
        memcpy(pkt->buf + pkt->len, &mctp_hdr, sizeof(mctp_hdr));
        pkt->len += sizeof(mctp_hdr);
        memcpy(pkt->buf + pkt->len,
               s->mctp_package.tx_buf->data + tx_offset,
               per_tx_len);
        pkt->len += per_tx_len;

        pec = crc8_pec(pkt->buf, pkt->len);
        pkt->buf[pkt->len++] = pec;

        g_queue_push_tail(s->tx_queue, pkt);
        seq = (seq + 1) % 0x4;
        tx_offset += per_tx_len;
    }

    /* Free tx payload. */
    g_array_free(s->mctp_package.tx_buf, true);
    s->mctp_package.tx_buf = NULL;

    return true;
}

static void mctp_i2c_master_bh(void *opaque)
{
    MctpI2cResponder *s = opaque;
    I2CBus *bus = I2C_BUS(qdev_get_parent_bus(&s->parent_obj.qdev));
    struct mctp_per_tx *pkt = s->active_tx;

    assert(bus->bh == s->bh);

    if (pkt->pos == 0) {
        if (i2c_start_send_async(bus, pkt->buf[pkt->pos++] >> 1) != 0) {
            goto out_done;
        }

        return;
    }

    if (pkt->pos >= pkt->len) {
        goto out_done;
    }

    if (i2c_send_async(bus, pkt->buf[pkt->pos++]) != 0) {
        goto out_done;
    }

    return;

out_done:
    s->active_tx = NULL;
    g_free(pkt);
    i2c_end_transfer(bus);
    i2c_bus_release(bus);

    if (!g_queue_is_empty(s->tx_queue)) {
        s->active_tx = g_queue_pop_head(s->tx_queue);
        i2c_bus_master(bus, s->bh);
        i2c_schedule_pending_master(bus);
    }
}

static void mctp_package_response(MctpI2cResponder *s)
{
    I2CBus *bus = I2C_BUS(qdev_get_parent_bus(&s->parent_obj.qdev));

    /*
     * No response payload prepared
     * (e.g. unknown msg_type / placeholder handler).
     */
    if (!s->mctp_package.tx_buf || s->mctp_package.tx_buf->len == 0) {
        if (s->mctp_package.tx_buf) {
            g_array_free(s->mctp_package.tx_buf, true);
            s->mctp_package.tx_buf = NULL;
        }
        return;
    }

    if (!s->bh) {
        s->bh = qemu_bh_new(mctp_i2c_master_bh, s);
    }

    mctp_i2c_build_tx(s);

    if (!g_queue_is_empty(s->tx_queue)) {
        s->active_tx = g_queue_pop_head(s->tx_queue);
        i2c_bus_master(bus, s->bh);
        i2c_schedule_pending_master(bus);
    }
}

static int mctp_rx_package_verify(MctpI2cResponder *s)
{
    const uint8_t *rx = s->rx;
    struct mctp_i2c_hdr *hdr;
    size_t len = s->rx_len;
    size_t recvlen;
    uint8_t pec, calc_pec;

    qemu_log_mask(LOG_GUEST_ERROR, "mctp: rx package verify, len %zu\n",
                  len);

    if (!len) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "mctp: rx package verify failed, len %zu\n",
                      len);
        return -EINVAL;
    }

    /* recvlen excludes PEC */
    recvlen = len - 1;
    hdr = (struct mctp_i2c_hdr *)rx;
    if (hdr->command != MCTP_I2C_COMMANDCODE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "mctp: rx package verify failed, command %d\n",
                      hdr->command);
        return -EINVAL;
    }

    if (hdr->byte_count + offsetof(struct mctp_i2c_hdr, source_slave) !=
        recvlen) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "mctp: rx package verify failed, byte_count %d, recvlen %zu\n",
                      hdr->byte_count, recvlen);
        return -EINVAL;
    }

    pec = rx[recvlen];
    calc_pec = crc8_pec(rx, recvlen);

    if (pec != calc_pec) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "mctp: rx package verify failed, pec %d, calc_pec %d\n",
                      pec, calc_pec);
        return -EINVAL;
    }

    /* debug, remove later */
    qemu_log_mask(LOG_GUEST_ERROR,
                  "mctp: rx package verify passed, len %zu\n",
                  len);
    return 0;
}

static int mctp_package_do_frame(MctpI2cResponder *s)
{
    struct mctp_i2c_hdr *i2c_hdr;
    struct mctp_hdr *hdr;
    uint8_t payload_offset;
    uint8_t payload_len;

    i2c_hdr = (struct mctp_i2c_hdr *)s->rx;
    hdr = (struct mctp_hdr *)(s->rx + sizeof(struct mctp_i2c_hdr));
    qemu_log_mask(LOG_GUEST_ERROR,
                  "mctp: rx package decode, ver %d, dest %d, src %d, "
                  "flags_seq_tag %d\n",
                  hdr->ver, hdr->dest, hdr->src, hdr->flags_seq_tag);

    if (hdr->ver != 1) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "mctp: rx package decode failed, ver %d\n",
                      hdr->ver);
        return -EINVAL;
    }

    if (hdr->flags_seq_tag & MCTP_HDR_FLAG_SOM) {
        if (s->mctp_package.rx_buf == NULL) {
            s->mctp_package.rx_buf = g_array_new(false, false, sizeof(uint8_t));
        }

        if (s->mctp_package.tx_buf == NULL) {
            s->mctp_package.tx_buf = g_array_new(false, false, sizeof(uint8_t));
        }

        s->mctp_package.rx_ready = false;
        s->mctp_package.metadata.dest_slave = i2c_hdr->dest_slave;
        s->mctp_package.metadata.source_slave = i2c_hdr->source_slave;
        s->mctp_package.metadata.dest = hdr->dest;
        s->mctp_package.metadata.src = hdr->src;
        s->mctp_package.metadata.msg_tag =
            hdr->flags_seq_tag & MCTP_HDR_TAG_MASK;
    }

    if (hdr->flags_seq_tag & MCTP_HDR_FLAG_EOM) {
        s->mctp_package.rx_ready = true;
    }

    payload_offset = sizeof(struct mctp_i2c_hdr) + sizeof(struct mctp_hdr);
    payload_len = s->rx_len - payload_offset; /* exclude PEC */

    g_array_append_vals(s->mctp_package.rx_buf,
                        s->rx + payload_offset, payload_len);
    qemu_log_mask(LOG_GUEST_ERROR, "rx_buf len %d\n",
                  s->mctp_package.rx_buf->len);

    return 0;
}

static void mctp_package_handler(MctpI2cResponder *s)
{
    struct mctp_payload *payload =
        (struct mctp_payload *)s->mctp_package.rx_buf->data;
    MctpI2cResponderMsgHandler handler = NULL;

    if (s->handlers) {
        handler = (MctpI2cResponderMsgHandler)
            g_hash_table_lookup(s->handlers,
                                GINT_TO_POINTER((int)payload->msg_type));
    }

    if (handler) {
        handler(s);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "mctp: unknown msg_type %d\n",
                      payload->msg_type);
    }

    /* Ensure rx/tx buffers don't leak if handler is missing or placeholder. */
    mctp_i2c_responder_cleanup_after_handler(s);
}

static void mctp_rx_handler(MctpI2cResponder *s)
{
    if (s->rx_len < MCTP_I2C_MINLEN + 1) {
        goto exit;
    }

    if (mctp_rx_package_verify(s) != 0) {
        goto exit;
    }

    if (mctp_package_do_frame(s) != 0) {
        goto exit;
    }

    if (!s->mctp_package.rx_ready) {
        goto exit;
    }

    mctp_package_handler(s);

    mctp_package_response(s);

exit:
    s->rx_len = 0;
}

static int mctp_i2c_event(I2CSlave *i2c, enum i2c_event event)
{
    MctpI2cResponder *s = MCTP_I2C_RESPONDER(i2c);

    switch (event) {
    case I2C_START_SEND:
        s->rx[0] = i2c->address << 1 | 0;
        s->rx_len = 1;
        return 0;
    case I2C_FINISH:
        mctp_rx_handler(s);
        return 0;
    default:
        return 0;
    }
}

static int mctp_i2c_send(I2CSlave *i2c, uint8_t data)
{
    MctpI2cResponder *s = MCTP_I2C_RESPONDER(i2c);

    if (s->rx_len >= BUF_SZ) {
        return -1;
    }

    s->rx[s->rx_len++] = data;
    return 0;
}

static uint8_t mctp_i2c_recv(I2CSlave *i2c)
{
    MctpI2cResponder *s = MCTP_I2C_RESPONDER(i2c);
    return (s->tx_pos < s->tx_len) ? s->tx[s->tx_pos++] : 0xff;
}

static const Property mctp_i2c_props[] = {
    DEFINE_PROP_UINT8("eid", MctpI2cResponder, eid, 0x00),
    DEFINE_PROP_UINT16("mtu", MctpI2cResponder, mtu, 64),
    DEFINE_PROP_UUID("uuid", MctpI2cResponder, uuid),
};

static void mctp_i2c_class_init(ObjectClass *oc, const void *data)
{
    I2CSlaveClass *sc = I2C_SLAVE_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);
    MctpI2cResponderClass *mc = MCTP_I2C_RESPONDER_CLASS(oc);
    device_class_set_props(dc, mctp_i2c_props);
    sc->event = mctp_i2c_event;
    sc->send = mctp_i2c_send;
    sc->recv = mctp_i2c_recv;

    /* Base class default: only MCTP Control handler is registered. */
    mc->init_handlers = mctp_i2c_init_handlers_default;
}

static void mctp_i2c_init_handlers_default(MctpI2cResponder *s)
{
    mctp_i2c_responder_register_msg_handler(s, MCTP_MSG_TYPE_CTRL,
                                            mctp_ctrl_handler);
}

static void mctp_i2c_instance_init(Object *obj)
{
    MctpI2cResponder *s = MCTP_I2C_RESPONDER(obj);
    MctpI2cResponderClass *mc = MCTP_I2C_RESPONDER_GET_CLASS(s);

    s->handlers = g_hash_table_new(g_direct_hash, g_direct_equal);
    s->tx_queue = g_queue_new();
    /* Allow QOM sub-types to extend supported message types via class hook. */
    mc->init_handlers(s);
}

static void mctp_i2c_instance_finalize(Object *obj)
{
    MctpI2cResponder *s = MCTP_I2C_RESPONDER(obj);

    if (s->bh) {
        qemu_bh_delete(s->bh);
        s->bh = NULL;
    }

    g_free(s->active_tx);
    s->active_tx = NULL;

    if (s->tx_queue) {
        while (!g_queue_is_empty(s->tx_queue)) {
            g_free(g_queue_pop_head(s->tx_queue));
        }
        g_queue_free(s->tx_queue);
        s->tx_queue = NULL;
    }

    if (s->mctp_package.rx_buf) {
        g_array_free(s->mctp_package.rx_buf, true);
        s->mctp_package.rx_buf = NULL;
    }

    if (s->mctp_package.tx_buf) {
        g_array_free(s->mctp_package.tx_buf, true);
        s->mctp_package.tx_buf = NULL;
    }

    if (s->handlers) {
        g_hash_table_destroy(s->handlers);
        s->handlers = NULL;
    }
}

static const TypeInfo mctp_i2c_type_info = {
    .name = TYPE_MCTP_I2C_RESPONDER,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(MctpI2cResponder),
    .class_size = sizeof(MctpI2cResponderClass),
    .class_init = mctp_i2c_class_init,
    .instance_init = mctp_i2c_instance_init,
    .instance_finalize = mctp_i2c_instance_finalize,
};

static void mctp_i2c_register_types(void)
{
    type_register_static(&mctp_i2c_type_info);
}
type_init(mctp_i2c_register_types)
