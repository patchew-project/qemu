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

#ifndef PROXY_LINK_H
#define PROXY_LINK_H

#include <stddef.h>
#include <stdint.h>
#include <glib.h>
#include <pthread.h>

#include "qemu/osdep.h"
#include "qom/object.h"

typedef struct ProxyLinkState ProxyLinkState;

#define TYPE_PROXY_LINK "proxy-link"
#define PROXY_LINK(obj) \
    OBJECT_CHECK(ProxyLinkState, (obj), TYPE_PROXY_LINK)

#include "exec/hwaddr.h"

#define MAX_FDS 8

#define PROC_HDR_SIZE offsetof(ProcMsg, data1.u64)

typedef enum {
    INIT = 0,
    CONF_READ,
    CONF_WRITE,
    SYNC_SYSMEM,
    MAX,
} proc_cmd_t;

typedef struct {
    hwaddr gpas[MAX_FDS];
    uint64_t sizes[MAX_FDS];
} sync_sysmem_msg_t;

typedef struct {
    proc_cmd_t cmd;
    int bytestream;
    size_t size;

    union {
        uint64_t u64;
        sync_sysmem_msg_t sync_sysmem;
    } data1;

    int fds[MAX_FDS];
    int num_fds;

    uint8_t *data2;
} ProcMsg;

struct conf_data_msg {
    uint32_t addr;
    uint32_t val;
    int l;
};

typedef void (*proxy_link_callback)(GIOCondition cond);

typedef struct ProxySrc {
    GSource gsrc;
    GPollFD gpfd;
} ProxySrc;

struct ProxyLinkState {
    Object obj;

    GMainContext *ctx;
    GMainLoop *loop;
    ProxySrc *src;

    int sock;
    pthread_mutex_t lock;

    proxy_link_callback callback;
};

ProxyLinkState *proxy_link_create(void);
void proxy_link_finalize(ProxyLinkState *s);

void proxy_proc_send(ProxyLinkState *s, ProcMsg *msg);
int proxy_proc_recv(ProxyLinkState *s, ProcMsg *msg);
void proxy_link_set_sock(ProxyLinkState *s, int fd);
void proxy_link_set_callback(ProxyLinkState *s, proxy_link_callback callback);
void start_handler(ProxyLinkState *s);

#endif
