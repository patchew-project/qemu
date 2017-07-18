/*
 * QEMU System Emulator
 *
 * Copyright (c) 2015-2017 Cambridge Greys Limited
 * Copyright (c) 2012-2014 Cisco Systems
 * Copyright (c) 2003-2008 Fabrice Bellard
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

static void net_unified_send(void *opaque);
static void unified_writable(void *opaque);

static void unified_update_fd_handler(NetUnifiedState *s)
{
    qemu_set_fd_handler(s->fd,
                        s->read_poll ? net_unified_send : NULL,
                        s->write_poll ? unified_writable : NULL,
                        s);
}

static void unified_read_poll(NetUnifiedState *s, bool enable)
{
    if (s->read_poll != enable) {
        s->read_poll = enable;
        unified_update_fd_handler(s);
    }
}

static void unified_write_poll(NetUnifiedState *s, bool enable)
{
    if (s->write_poll != enable) {
        s->write_poll = enable;
        unified_update_fd_handler(s);
    }
}

static void unified_writable(void *opaque)
{
    NetUnifiedState *s = opaque;
    unified_write_poll(s, false);
    qemu_flush_queued_packets(&s->nc);
}

static void unified_send_completed(NetClientState *nc, ssize_t len)
{
    NetUnifiedState *s = DO_UPCAST(NetUnifiedState, nc, nc);
    unified_read_poll(s, true);
}

static void unified_poll(NetClientState *nc, bool enable)
{
    NetUnifiedState *s = DO_UPCAST(NetUnifiedState, nc, nc);
    unified_write_poll(s, enable);
    unified_read_poll(s, enable);
}

static ssize_t net_unified_receive_dgram_iov(NetClientState *nc,
                    const struct iovec *iov,
                    int iovcnt)
{
    NetUnifiedState *s = DO_UPCAST(NetUnifiedState, nc, nc);

    struct msghdr message;
    int ret;

    if (iovcnt > MAX_UNIFIED_IOVCNT - 1) {
        error_report(
            "iovec too long %d > %d, change unified.h",
            iovcnt, MAX_UNIFIED_IOVCNT
        );
        return -1;
    }
    if (s->offset > 0) {
        s->form_header(s);
        memcpy(s->vec + 1, iov, iovcnt * sizeof(struct iovec));
        s->vec->iov_base = s->header_buf;
        s->vec->iov_len = s->offset;
        message.msg_iovlen = iovcnt + 1;
    } else {
        memcpy(s->vec, iov, iovcnt * sizeof(struct iovec));
        message.msg_iovlen = iovcnt;
    }
    message.msg_name = s->dgram_dst;
    message.msg_namelen = s->dst_size;
    message.msg_iov = s->vec;
    message.msg_control = NULL;
    message.msg_controllen = 0;
    message.msg_flags = 0;
    do {
        ret = sendmsg(s->fd, &message, 0);
    } while ((ret == -1) && (errno == EINTR));
    if (ret > 0) {
        ret -= s->offset;
    } else if (ret == 0) {
        /* belt and braces - should not occur on DGRAM
        * we should get an error and never a 0 send
        */
        ret = iov_size(iov, iovcnt);
    } else {
        /* signal upper layer that socket buffer is full */
        ret = -errno;
        if (ret == -EAGAIN || ret == -ENOBUFS) {
            unified_write_poll(s, true);
            ret = 0;
        }
    }
    return ret;
}

static ssize_t net_unified_receive_dgram(NetClientState *nc,
                    const uint8_t *buf,
                    size_t size)
{
    NetUnifiedState *s = DO_UPCAST(NetUnifiedState, nc, nc);

    struct iovec *vec;
    struct msghdr message;
    ssize_t ret = 0;

    vec = s->vec;
    if (s->offset > 0) {
        s->form_header(s);
        vec->iov_base = s->header_buf;
        vec->iov_len = s->offset;
        message.msg_iovlen = 2;
        vec++;
    } else {
        message.msg_iovlen = 1;
    }
    vec->iov_base = (void *) buf;
    vec->iov_len = size;
    message.msg_name = s->dgram_dst;
    message.msg_namelen = s->dst_size;
    message.msg_iov = s->vec;
    message.msg_control = NULL;
    message.msg_controllen = 0;
    message.msg_flags = 0;
    do {
        ret = sendmsg(s->fd, &message, 0);
    } while ((ret == -1) && (errno == EINTR));
    if (ret > 0) {
        ret -= s->offset;
    } else if (ret == 0) {
        /* belt and braces - should not occur on DGRAM
        * we should get an error and never a 0 send
        */
        ret = size;
    } else {
        ret = -errno;
        if (ret == -EAGAIN || ret == -ENOBUFS) {
            /* signal upper layer that socket buffer is full */
            unified_write_poll(s, true);
            ret = 0;
        }
    }
    return ret;
}


static void net_unified_process_queue(NetUnifiedState *s)
{
    int size = 0;
    struct iovec *vec;
    bool bad_read;
    int data_size;
    struct mmsghdr *msgvec;

    /* go into ring mode only if there is a "pending" tail */
    if (s->queue_depth > 0) {
        do {
            msgvec = s->msgvec + s->queue_tail;
            if (msgvec->msg_len > 0) {
                data_size = msgvec->msg_len - s->header_size;
                vec = msgvec->msg_hdr.msg_iov;
                if ((data_size > 0) &&
                    (s->verify_header(s, vec->iov_base) == 0)) {
                    if (s->header_size > 0) {
                        vec++;
                    }
                    /* Use the legacy delivery for now, we will
                     * switch to using our own ring as a queueing mechanism
                     * at a later date
                     */
                    size = qemu_send_packet_async(
                            &s->nc,
                            vec->iov_base,
                            data_size,
                            unified_send_completed
                        );
                    if (size == 0) {
                        unified_read_poll(s, false);
                    }
                    bad_read = false;
                } else {
                    bad_read = true;
                    if (!s->header_mismatch) {
                        /* report error only once */
                        error_report("unified header verification failed");
                        s->header_mismatch = true;
                    }
                }
            } else {
                bad_read = true;
            }
            s->queue_tail = (s->queue_tail + 1) % MAX_UNIFIED_MSGCNT;
            s->queue_depth--;
        } while (
                (s->queue_depth > 0) &&
                 qemu_can_send_packet(&s->nc) &&
                ((size > 0) || bad_read)
            );
    }
}

static void net_unified_send(void *opaque)
{
    NetUnifiedState *s = opaque;
    int target_count, count;
    struct mmsghdr *msgvec;

    /* go into ring mode only if there is a "pending" tail */

    if (s->queue_depth) {

        /* The ring buffer we use has variable intake
         * count of how much we can read varies - adjust accordingly
         */

        target_count = MAX_UNIFIED_MSGCNT - s->queue_depth;

        /* Ensure we do not overrun the ring when we have
         * a lot of enqueued packets
         */

        if (s->queue_head + target_count > MAX_UNIFIED_MSGCNT) {
            target_count = MAX_UNIFIED_MSGCNT - s->queue_head;
        }
    } else {

        /* we do not have any pending packets - we can use
        * the whole message vector linearly instead of using
        * it as a ring
        */

        s->queue_head = 0;
        s->queue_tail = 0;
        target_count = MAX_UNIFIED_MSGCNT;
    }

    msgvec = s->msgvec + s->queue_head;
    if (target_count > 0) {
        do {
            count = recvmmsg(
                s->fd,
                msgvec,
                target_count, MSG_DONTWAIT, NULL);
        } while ((count == -1) && (errno == EINTR));
        if (count < 0) {
            /* Recv error - we still need to flush packets here,
             * (re)set queue head to current position
             */
            count = 0;
        }
        s->queue_head = (s->queue_head + count) % MAX_UNIFIED_MSGCNT;
        s->queue_depth += count;
    }
    net_unified_process_queue(s);
}

static void destroy_vector(struct mmsghdr *msgvec, int count, int iovcount)
{
    int i, j;
    struct iovec *iov;
    struct mmsghdr *cleanup = msgvec;
    if (cleanup) {
        for (i = 0; i < count; i++) {
            if (cleanup->msg_hdr.msg_iov) {
                iov = cleanup->msg_hdr.msg_iov;
                for (j = 0; j < iovcount; j++) {
                    g_free(iov->iov_base);
                    iov++;
                }
                g_free(cleanup->msg_hdr.msg_iov);
            }
            cleanup++;
        }
        g_free(msgvec);
    }
}



static struct mmsghdr *build_unified_vector(NetUnifiedState *s, int count)
{
    int i;
    struct iovec *iov;
    struct mmsghdr *msgvec, *result;

    msgvec = g_new(struct mmsghdr, count);
    result = msgvec;
    for (i = 0; i < count ; i++) {
        msgvec->msg_hdr.msg_name = NULL;
        msgvec->msg_hdr.msg_namelen = 0;
        iov =  g_new(struct iovec, IOVSIZE);
        msgvec->msg_hdr.msg_iov = iov;
        if (s->header_size > 0) {
            iov->iov_base = g_malloc(s->header_size);
            iov->iov_len = s->header_size;
            iov++ ;
        }
        iov->iov_base = qemu_memalign(BUFFER_ALIGN, BUFFER_SIZE);
        iov->iov_len = BUFFER_SIZE;
        msgvec->msg_hdr.msg_iovlen = 2;
        msgvec->msg_hdr.msg_control = NULL;
        msgvec->msg_hdr.msg_controllen = 0;
        msgvec->msg_hdr.msg_flags = 0;
        msgvec++;
    }
    return result;
}

static void net_unified_cleanup(NetClientState *nc)
{
    NetUnifiedState *s = DO_UPCAST(NetUnifiedState, nc, nc);
    qemu_purge_queued_packets(nc);
    unified_read_poll(s, false);
    unified_write_poll(s, false);
    if (s->fd >= 0) {
        close(s->fd);
    }
    if (s->header_size > 0) {
        destroy_vector(s->msgvec, MAX_UNIFIED_MSGCNT, IOVSIZE);
    } else {
        destroy_vector(s->msgvec, MAX_UNIFIED_MSGCNT, 1);
    }
    g_free(s->vec);
    if (s->header_buf != NULL) {
        g_free(s->header_buf);
    }
    if (s->dgram_dst != NULL) {
        g_free(s->dgram_dst);
    }
}

static NetClientInfo net_unified_info = {
    /* we share this one for all types for now, wrong I know :) */
    .type = NET_CLIENT_DRIVER_L2TPV3,
    .size = sizeof(NetUnifiedState),
    .receive = net_unified_receive_dgram,
    .receive_iov = net_unified_receive_dgram_iov,
    .poll = unified_poll,
    .cleanup = net_unified_cleanup,
};

NetClientState *qemu_new_unified_net_client(const char *name,
                    NetClientState *peer) {
    return qemu_new_net_client(&net_unified_info, peer, "unified", name);
}

void qemu_net_finalize_unified_init(NetUnifiedState *s, int fd)
{

    s->msgvec = build_unified_vector(s, MAX_UNIFIED_MSGCNT);
    s->vec = g_new(struct iovec, MAX_UNIFIED_IOVCNT);
    if (s->header_size > 0) {
        s->header_buf = g_malloc(s->header_size);
    } else {
        s->header_buf = NULL;
    }
    qemu_set_nonblock(fd);

    s->fd = fd;
    unified_read_poll(s, true);

}

