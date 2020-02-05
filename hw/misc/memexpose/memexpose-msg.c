/*
 *  Memexpose core
 *
 *  Copyright (C) 2020 Samsung Electronics Co Ltd.
 *    Igor Kotrasinski, <i.kotrasinsk@partner.samsung.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "memexpose-msg.h"
#include "memexpose-core.h"

#define MIN_MSG_SIZE (sizeof(struct memexpose_op_head))
#define MAX_MSG_SIZE (sizeof(struct memexpose_op))

int memexpose_ep_msg_prio(MemexposeEp *ep, enum memexpose_op_type ot)
{
    int ot_prio;
    switch (ot) {
    case MOP_READ:
    case MOP_READ_RET:
    case MOP_WRITE:
    case MOP_WRITE_RET:
        ot_prio = 2;
        break;
    default:
        ot_prio = 0;
    }
    return ot_prio + ep->prio;
}

static int mep_can_receive(void *opaque)
{
    int sz;
    MemexposeEp *ep = opaque;
    MemexposeMsg *msg = &ep->msg;

    switch (msg->read_state) {
    case MEMEXPOSE_MSG_BROKEN:
        return 0;
    case MEMEXPOSE_MSG_READ_SIZE:
        return sizeof(msg->buf.head.size) - msg->bytes;
    case MEMEXPOSE_MSG_READ_BODY:
        sz = msg->buf.head.size - msg->bytes;
        if (sz > MAX_MSG_SIZE) {
            return MAX_MSG_SIZE;  /* We'll handle this as an error later */
        }
        return sz;
    default:
        MEMEXPOSE_DPRINTF("Invalid read state %d\n", msg->read_state);
        return 0;
    }
}

static int mep_do_receive(MemexposeMsg *msg,
                          const uint8_t *buf, int size)
{
    switch (msg->read_state) {
    case MEMEXPOSE_MSG_BROKEN:
        return -1;
    case MEMEXPOSE_MSG_READ_SIZE:
        memcpy((unsigned char *)&msg->buf + msg->bytes, buf, size);
        msg->bytes += size;
        if (msg->bytes == sizeof(msg->buf.head.size)) {
            msg->read_state = MEMEXPOSE_MSG_READ_BODY;
        }
        return 0;
    case MEMEXPOSE_MSG_READ_BODY:
        if (msg->buf.head.size < MIN_MSG_SIZE ||
            msg->buf.head.size > MAX_MSG_SIZE) {
            MEMEXPOSE_DPRINTF("Invalid message size %d, protocol broken!\n",
                              msg->buf.head.size);
            msg->read_state = MEMEXPOSE_MSG_BROKEN;
            return -1;
        }
        memcpy((unsigned char *)&msg->buf + msg->bytes, buf, size);
        msg->bytes += size;
        if (msg->bytes < msg->buf.head.size) {
            return 0;
        }
        msg->bytes = 0;
        msg->read_state = MEMEXPOSE_MSG_READ_SIZE;
        return 1;
    default:
        MEMEXPOSE_DPRINTF("Invalid read state %d\n", msg->read_state);
        return -1;
    }
}

static void mep_receive(void *opaque, const uint8_t *buf, int size)
{
    MemexposeEp *ep = opaque;
    Error *err = NULL;
    int new_msg = mep_do_receive(&ep->msg, buf, size);
    if (new_msg) {
        ep->handle_msg(ep->data, &ep->msg.buf, &err);
        if (err) {
            error_report_err(err);
        }
    } else if (new_msg < 0) {
        error_setg(&err, "Failed to receive memexpose message"); /* FIXME */
        error_report_err(err);
    }
}

static int mep_receive_sync(MemexposeEp *ep, struct memexpose_op *op)
{
    int ret = 0;
    MemexposeMsg *msg = &ep->msg;
    assert(!ep->is_async);

    while (!ret) {
        int can_receive = mep_can_receive(ep);
        unsigned char *msgbuf = (unsigned char *)&msg->buf + msg->bytes;
        qemu_chr_fe_read_all(ep->chr, msgbuf, can_receive);
        ret = mep_do_receive(msg, msgbuf, can_receive);
        if (ret == -1) {
            return -1;
        }
    }
    *op = msg->buf;
    return 0;
}

void memexpose_ep_write_async(MemexposeEp *ep, struct memexpose_op *op)
{
    qemu_chr_fe_write_all(ep->chr, (unsigned char *) op, op->head.size);
}

static void mep_queue_msg(MemexposeEp *ep, struct memexpose_op *op)
{
    ep->queued_op = *op;
    qemu_bh_schedule(ep->queue_msg_bh);
}

static void mep_queue_msg_bh(void *epp)
{
    Error *err = NULL;
    MemexposeEp *ep = epp;
    if (!ep->queued_op.head.size) {
        return;
    }
    ep->handle_msg(ep->data, &ep->queued_op, &err); /* FIXME - handle */
    ep->queued_op.head.size = 0;
}

/*
 * Synchronously write a message to another QEMU and receive a response.
 * To avoid deadlocks, each message type has its priority and no more than one
 * message of each priority is in flight.
 *
 * After we send a message, we await a response while handling all messages of
 * higher priority and deferring messages of lower priority. This way each side
 * will have its requests handled until they have time to handle ours.
 *
 * The above means that a handler for a message must be able to run while an
 * operation that sends any other lower priority message is in progress. Make
 * sure to order operations in an order that does not upset QEMU!
 */
void memexpose_ep_write_sync(MemexposeEp *ep, struct memexpose_op *op)
{
    assert(!ep->is_async);
    qemu_chr_fe_write_all(ep->chr, (unsigned char *) op, op->head.size);

    struct memexpose_op resp;
    int prio = op->head.prio;

    /* FIXME - handle errors */
    while (true) {
        Error *err = NULL;
        mep_receive_sync(ep, &resp);
        int resp_prio = resp.head.prio;
        if (resp_prio > prio) {
            ep->handle_msg(ep->data, &resp, &err);
        } else if (resp_prio < prio) {
            mep_queue_msg(ep, &resp);
        } else {
            *op = resp;
            return;
        }
    }
}

void memexpose_ep_init(MemexposeEp *ep, CharBackend *chr, void *data, int prio,
                       void (*handle_msg)(void *data, struct memexpose_op *op,
                                          Error **errp))
{
    ep->queue_msg_bh = qemu_bh_new(mep_queue_msg_bh, ep);
    ep->queued_op.head.size = 0;
    ep->handle_msg = handle_msg;
    ep->msg.bytes = 0;
    ep->msg.read_state = MEMEXPOSE_MSG_READ_SIZE;
    ep->chr = chr;
    ep->data = data;
    ep->prio = prio;
    ep->connected = 0;

    if (handle_msg)
        qemu_chr_fe_set_handlers(ep->chr, mep_can_receive,
                                 mep_receive, NULL, NULL, ep, NULL, true);
    Chardev *chrd = qemu_chr_fe_get_driver(ep->chr);
    assert(chrd);
    MEMEXPOSE_DPRINTF("Memexpose endpoint at %s\n",
                      chrd->filename);

}

/* TODO - protocol for synchronously ending connection */
void memexpose_ep_destroy(MemexposeEp *ep)
{
    qemu_chr_fe_set_handlers(ep->chr, NULL, NULL, NULL, NULL, NULL, NULL, true);
}

void memexpose_ep_send_fd(MemexposeEp *ep, int fd)
{
    qemu_chr_fe_set_msgfds(ep->chr, &fd, 1);
}

int memexpose_ep_recv_fd(MemexposeEp *ep)
{
    return qemu_chr_fe_get_msgfd(ep->chr);
}

int memexpose_ep_connect(MemexposeEp *ep)
{
    /* FIXME - report errors */
    Error *err = NULL;
    if (ep->connected) {
        return 0;
    }

    int ret = qemu_chr_fe_wait_connected(ep->chr, &err);
    if (ret) {
        return ret;
    }

    ep->connected = 1;
    return 0;
}

void memexpose_ep_disconnect(MemexposeEp *ep)
{
    if (ep->connected) {
        qemu_chr_fe_disconnect(ep->chr);
    }
    ep->connected = 0;
}
