/*
 * Communication channel between QEMU and remote device process
 *
 * Copyright 2018, Oracle and/or its affiliates. All rights reserved.
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

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "qemu/module.h"
#include "io/proxy-link.h"

static void proxy_link_inst_init(Object *obj)
{
    ProxyLinkState *s = PROXY_LINK(obj);

    pthread_mutex_init(&s->lock, NULL);

    s->sock = STDIN_FILENO;
    s->ctx = g_main_context_new();
    s->loop = g_main_loop_new(s->ctx, FALSE);
}

static const TypeInfo proxy_link_info = {
    .name = TYPE_PROXY_LINK,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(ProxyLinkState),
    .instance_init = proxy_link_inst_init,
};

static void proxy_link_register_types(void)
{
    type_register_static(&proxy_link_info);
}

type_init(proxy_link_register_types)

ProxyLinkState *proxy_link_create(void)
{
    return PROXY_LINK(object_new(TYPE_PROXY_LINK));
}

void proxy_link_finalize(ProxyLinkState *s)
{
    g_source_unref(&s->src->gsrc);
    g_main_loop_unref(s->loop);
    g_main_context_unref(s->ctx);
    g_main_loop_quit(s->loop);

    close(s->sock);

    object_unref(OBJECT(s));
}

void proxy_proc_send(ProxyLinkState *s, ProcMsg *msg)
{
    int rc;
    uint8_t *data;
    char control[CMSG_SPACE(MAX_FDS * sizeof(int))] = { };
    struct msghdr hdr;
    struct cmsghdr *chdr;

    struct iovec iov = {
        .iov_base = (char *) msg,
        .iov_len = PROC_HDR_SIZE,
    };

    memset(&hdr, 0, sizeof(hdr));
    memset(control, 0, sizeof(control));

    hdr.msg_iov = &iov;
    hdr.msg_iovlen = 1;

    assert(msg->num_fds <= MAX_FDS);

    if (msg->num_fds > 0) {
        size_t fdsize = msg->num_fds * sizeof(int);

        hdr.msg_control = control;
        hdr.msg_controllen = sizeof(control);

        chdr = CMSG_FIRSTHDR(&hdr);
        chdr->cmsg_len = CMSG_LEN(fdsize);
        chdr->cmsg_level = SOL_SOCKET;
        chdr->cmsg_type = SCM_RIGHTS;
        memcpy(CMSG_DATA(chdr), msg->fds, fdsize);
        hdr.msg_controllen = chdr->cmsg_len;
    }

    pthread_mutex_lock(&s->lock);

    do {
        rc = sendmsg(s->sock, &hdr, 0);
    } while (rc < 0 && (errno == EINTR || errno == EAGAIN));

    if (rc < 0) {
        pthread_mutex_unlock(&s->lock);
        return;
    }

    if (msg->bytestream) {
        data = msg->data2;
    } else {
        data = (uint8_t *)msg + PROC_HDR_SIZE;
    }

    do {
        rc = write(s->sock, data, msg->size);
    } while (rc < 0 && (errno == EINTR || errno == EAGAIN));

    pthread_mutex_unlock(&s->lock);
}


int proxy_proc_recv(ProxyLinkState *s, ProcMsg *msg)
{
    int rc;
    uint8_t *data;
    char control[CMSG_SPACE(MAX_FDS * sizeof(int))] = { };
    struct msghdr hdr;
    struct cmsghdr *chdr;
    size_t fdsize;

    struct iovec iov = {
        .iov_base = (char *) msg,
        .iov_len = PROC_HDR_SIZE,
    };

    memset(&hdr, 0, sizeof(hdr));
    memset(control, 0, sizeof(control));

    hdr.msg_iov = &iov;
    hdr.msg_iovlen = 1;
    hdr.msg_control = control;
    hdr.msg_controllen = sizeof(control);

    pthread_mutex_lock(&s->lock);

    do {
        rc = recvmsg(s->sock, &hdr, 0);
    } while (rc < 0 && (errno == EINTR || errno == EAGAIN));

    if (rc < 0) {
        pthread_mutex_unlock(&s->lock);
        return rc;
    }

    msg->num_fds = 0;
    for (chdr = CMSG_FIRSTHDR(&hdr); chdr != NULL;
         chdr = CMSG_NXTHDR(&hdr, chdr)) {
        if ((chdr->cmsg_level == SOL_SOCKET) &&
            (chdr->cmsg_type == SCM_RIGHTS)) {
            fdsize = chdr->cmsg_len - CMSG_LEN(0);
            msg->num_fds = fdsize / sizeof(int);
            memcpy(msg->fds, CMSG_DATA(chdr), fdsize);
            break;
        }
    }

    if (msg->size && msg->bytestream) {
        msg->data2 = calloc(1, msg->size);
        data = msg->data2;
    } else {
        data = (uint8_t *)&msg->data1;
    }

    if (msg->size) {
        do {
            rc = read(s->sock, data, msg->size);
        } while (rc < 0 && (errno == EINTR || errno == EAGAIN));
    }

    pthread_mutex_unlock(&s->lock);

    return rc;
}

static gboolean proxy_link_handler_prepare(GSource *gsrc, gint *timeout)
{
    g_assert(timeout);

    *timeout = -1;

    return FALSE;
}

static gboolean proxy_link_handler_check(GSource *gsrc)
{
    ProxySrc *src = (ProxySrc *)gsrc;

    return src->gpfd.events & src->gpfd.revents;
}

static gboolean proxy_link_handler_dispatch(GSource *gsrc, GSourceFunc func,
                                            gpointer data)
{
    ProxySrc *src = (ProxySrc *)gsrc;
    ProxyLinkState *s = (ProxyLinkState *)data;

    s->callback(src->gpfd.revents);

    if ((src->gpfd.revents & G_IO_HUP) || (src->gpfd.revents & G_IO_ERR)) {
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}
GSourceFuncs gsrc_funcs = (GSourceFuncs){
    .prepare = proxy_link_handler_prepare,
    .check = proxy_link_handler_check,
    .dispatch = proxy_link_handler_dispatch,
    .finalize = NULL,
};

void proxy_link_set_sock(ProxyLinkState *s, int fd)
{
    s->sock = fd;
}

void proxy_link_set_callback(ProxyLinkState *s, proxy_link_callback callback)
{
    s->callback = callback;
}

void start_handler(ProxyLinkState *s)
{
    ProxySrc *src = (ProxySrc *)g_source_new(&gsrc_funcs, sizeof(ProxySrc));

    g_source_set_callback(&src->gsrc, NULL, (gpointer)s, NULL);

    src->gpfd.fd = s->sock;
    src->gpfd.events = G_IO_IN | G_IO_HUP | G_IO_ERR;
    g_source_add_poll(&src->gsrc, &src->gpfd);
    g_assert(g_source_attach(&src->gsrc, s->ctx));
    s->src = src;

    g_main_loop_run(s->loop);
}
