/*
 * CAN common CAN bus emulation support
 *
 * Copyright (c) 2013-2014 Jin Yang
 * Copyright (c) 2014 Pavel Pisa
 *
 * Initial development supported by Google GSoC 2013 from RTEMS project slot
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
#include "sysemu/char.h"
#include "qemu/sockets.h"
#include "qemu/error-report.h"
#include "hw/hw.h"
#include "can/can_emu.h"

#ifdef __linux__
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>

#define NUM_FILTER        4
#define CAN_READ_BUF_LEN  5
typedef struct {
    CanBusClientState  bus_client;
    qemu_can_filter    rfilter[NUM_FILTER];
    can_err_mask_t     err_mask;

    qemu_can_frame     buf[CAN_READ_BUF_LEN];
    int                bufcnt;
    int                bufptr;

    int                fd;
} CanBusHostConnectState;

#endif /*__linux__*/

static QTAILQ_HEAD(, CanBusState) can_buses = QTAILQ_HEAD_INITIALIZER(can_buses);

CanBusState *can_bus_find_by_name(const char *name, bool create_missing)
{
    CanBusState *bus;

    if (name == NULL) {
        name = "canbus0";
    }

    QTAILQ_FOREACH(bus, &can_buses, next) {
        if (!strcmp(bus->name, name)) {
            return bus;
        }
    }

    if (!create_missing) {
        return 0;
    }

    bus = g_malloc0(sizeof(*bus));
    if (bus == NULL) {
        return NULL;
    }

    QTAILQ_INIT(&bus->clients);

    bus->name = g_strdup(name);

    QTAILQ_INSERT_TAIL(&can_buses, bus, next);
    return bus;
}

int can_bus_insert_client(CanBusState *bus, CanBusClientState *client)
{
    client->bus = bus;
    QTAILQ_INSERT_TAIL(&bus->clients, client, next);
    return 0;
}

int can_bus_remove_client(CanBusClientState *client)
{
    CanBusState *bus = client->bus;
    if (bus == NULL) {
        return 0;
    }

    QTAILQ_REMOVE(&bus->clients, client, next);
    client->bus = NULL;
    return 1;
}

ssize_t can_bus_client_send(CanBusClientState *client,
             const struct qemu_can_frame *frames, size_t frames_cnt)
{
    int ret = 0;
    CanBusState *bus = client->bus;
    CanBusClientState *peer;
    if (bus == NULL) {
        return -1;
    }

    QTAILQ_FOREACH(peer, &bus->clients, next) {
        if (peer->info->can_receive(peer)) {
            if (peer == client) {
                /* No loopback support for now */
                continue;
            }
            if (peer->info->receive(peer, frames, frames_cnt) > 0) {
                ret = 1;
            }
        }
    }

    return ret;
}

int can_bus_client_set_filters(CanBusClientState *client,
             const struct qemu_can_filter *filters, size_t filters_cnt)
{
    return 0;
}

#ifdef DEBUG_CAN
static void can_display_msg(struct qemu_can_frame *msg)
{
    int i;

    printf("%03X [%01d]:", (msg->can_id & 0x1fffffff), msg->can_dlc);
    for (i = 0; i < msg->can_dlc; i++) {
        printf("  %02X", msg->data[i]);
    }
    printf("\n");
}
#endif

#ifdef __linux__

static void can_bus_host_read(void *opaque)
{
    CanBusHostConnectState *c = opaque;

    /* CAN_READ_BUF_LEN for multiple messages syscall is possible for future */
    c->bufcnt = read(c->fd, c->buf, sizeof(qemu_can_frame));
    if (c->bufcnt < 0) {
        perror("CAN bus host read");
        return;
    }

    can_bus_client_send(&c->bus_client, c->buf, 1);

#ifdef DEBUG_CAN
    can_display_msg(c->buf);/* Just display the first one. */
#endif
}

static int can_bus_host_can_receive(CanBusClientState *client)
{
    CanBusHostConnectState *c = container_of(client, CanBusHostConnectState, bus_client);

    if (c->fd < 0) {
        return -1;
    }

    return 1;
}

static ssize_t can_bus_host_receive(CanBusClientState *client,
                            const qemu_can_frame *frames, size_t frames_cnt)
{
    CanBusHostConnectState *c = container_of(client, CanBusHostConnectState, bus_client);
    size_t len = sizeof(qemu_can_frame);
    int res;

    if (c->fd < 0) {
        return -1;
    }

    res = write(c->fd, frames, len);

    if (!res) {
        fprintf(stderr, "CAN bus write to host device zero length\n");
        return -1;
    }

    /* send frame */
    if (res != len) {
        perror("CAN bus write to host device error");
        return -1;
    }

    return 1;
}

static void can_bus_host_cleanup(CanBusClientState *client)
{
    CanBusHostConnectState *c = container_of(client, CanBusHostConnectState, bus_client);

    if (c->fd >= 0) {
        qemu_set_fd_handler(c->fd, NULL, NULL, c);
        close(c->fd);
        c->fd = -1;
    }
}

int can_bus_host_set_filters(CanBusClientState *, const struct qemu_can_filter *filters, size_t filters_cnt);

int can_bus_host_set_filters(CanBusClientState *client, const struct qemu_can_filter *filters, size_t filters_cnt)
{
    CanBusHostConnectState *c = container_of(client, CanBusHostConnectState, bus_client);

#ifdef DEBUG_CAN
    int i;
#endif

    if (filters_cnt > 4) {
        return -1;
    }

#ifdef DEBUG_CAN
    for (i = 0; i < filters_cnt; i++) {
        printf("[%i]  id=0x%08x maks=0x%08x\n", i, filters[i].can_id, filters[i].can_mask);
    }
#endif

    setsockopt(c->fd, SOL_CAN_RAW, CAN_RAW_FILTER,
               filters, filters_cnt * sizeof(qemu_can_filter));

    return 0;
}

static void can_bus_host_update_read_handler(CanBusHostConnectState *c)
{
    if (c->fd >= 0) {
        qemu_set_fd_handler(c->fd, can_bus_host_read, NULL, c);
    }
}

static CanBusClientInfo can_bus_host_bus_client_info = {
    .can_receive = can_bus_host_can_receive,
    .receive = can_bus_host_receive,
    .cleanup = can_bus_host_cleanup,
    .poll = NULL
};

static
CanBusHostConnectState *can_bus_host_connect_new(const char *host_dev_name)
{
    int s; /* can raw socket */
    CanBusHostConnectState    *c;
    struct sockaddr_can addr;
    struct ifreq ifr;

    c = g_malloc0(sizeof(CanBusHostConnectState));
    if (c == NULL) {
        goto fail1;
    }

    c->fd = -1;

    /* open socket */
    s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) {
        perror("socket");
        goto fail;
    }

    addr.can_family = AF_CAN;
    memset(&ifr.ifr_name, 0, sizeof(ifr.ifr_name));
    strcpy(ifr.ifr_name, host_dev_name);
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
        perror("SIOCGIFINDEX");
        goto fail;
    }
    addr.can_ifindex = ifr.ifr_ifindex;

    c->err_mask = 0xffffffff; /* Receive error frame. */
    setsockopt(s, SOL_CAN_RAW, CAN_RAW_ERR_FILTER,
                   &c->err_mask, sizeof(c->err_mask));

    c->rfilter[0].can_id = 0; /* Receive all data frame. If |= CAN_INV_FILTER no data. */
    c->rfilter[0].can_mask = 0;
    c->rfilter[0].can_mask &= ~CAN_ERR_FLAG;
    setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER,
                   c->rfilter, sizeof(struct qemu_can_filter));

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        goto fail;
    }

    c->fd = s;

    c->bus_client.info = &can_bus_host_bus_client_info;

    can_bus_host_update_read_handler(c);

    return c;

fail:
    can_bus_host_cleanup(&c->bus_client);
    g_free(c);
fail1:

    return NULL;
}

int can_bus_connect_to_host_device(CanBusState *bus, const char *host_dev_name)
{
    CanBusHostConnectState *c;

    c = can_bus_host_connect_new(host_dev_name);
    if (c == NULL) {
        error_report("CAN bus setup of host connect to \"%s\" failed\n",
                      host_dev_name);
        exit(1);
    }

    if (can_bus_insert_client(bus, &c->bus_client) < 0) {
        error_report("CAN host device \"%s\" connect to bus \"%s\" failed\n",
                      host_dev_name, bus->name);
        exit(1);
    }

    return 0;
}

#else /*__linux__*/
int can_bus_connect_to_host_device(CanBusState *bus, const char *name)
{
    error_report("CAN bus connect to host device not supported on this system\n");
    exit(1);
}
#endif /*__linux__*/
