/*
 * CAN socketcan support to connect to the Linux host SocketCAN interfaces
 *
 * Copyright (c) 2013-2014 Jin Yang
 * Copyright (c) 2014-2018 Pavel Pisa
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
#include "chardev/char.h"
#include "qemu/sockets.h"
#include "qemu/error-report.h"
#include "hw/hw.h"
#include "can/can_emu.h"

#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>

#ifndef DEBUG_CAN
#define DEBUG_CAN 0
#endif /*DEBUG_CAN*/

#define CAN_READ_BUF_LEN  5
typedef struct {
    CanBusClientState  bus_client;
    qemu_can_filter    *rfilter;
    int                rfilter_num;
    can_err_mask_t     err_mask;

    qemu_can_frame     buf[CAN_READ_BUF_LEN];
    int                bufcnt;
    int                bufptr;

    int                fd;
} CanBusSocketcanConnectState;

static void can_display_msg(struct qemu_can_frame *msg)
{
    int i;

    /* Check that QEMU and Linux kernel flags encoding matches */
    assert(QEMU_CAN_EFF_FLAG == CAN_EFF_FLAG);
    assert(QEMU_CAN_RTR_FLAG == CAN_RTR_FLAG);
    assert(QEMU_CAN_ERR_FLAG == CAN_ERR_FLAG);

    assert(QEMU_CAN_INV_FILTER == CAN_INV_FILTER);

    fprintf(stderr, "%03X [%01d]:", (msg->can_id & 0x1fffffff), msg->can_dlc);
    for (i = 0; i < msg->can_dlc; i++) {
        fprintf(stderr, "  %02X", msg->data[i]);
    }
    fprintf(stderr, "\n");
}

static void can_bus_socketcan_read(void *opaque)
{
    CanBusSocketcanConnectState *c;
    c = (CanBusSocketcanConnectState *)opaque;



    /* CAN_READ_BUF_LEN for multiple messages syscall is possible for future */
    c->bufcnt = read(c->fd, c->buf, sizeof(qemu_can_frame));
    if (c->bufcnt < 0) {
        perror("CAN bus host read");
        return;
    }

    can_bus_client_send(&c->bus_client, c->buf, 1);

    if (DEBUG_CAN) {
        can_display_msg(c->buf); /* Just display the first one. */
    }
}

static int can_bus_socketcan_can_receive(CanBusClientState *client)
{
    CanBusSocketcanConnectState *c;
    c = container_of(client, CanBusSocketcanConnectState, bus_client);

    if (c->fd < 0) {
        return -1;
    }

    return 1;
}

static ssize_t can_bus_socketcan_receive(CanBusClientState *client,
                            const qemu_can_frame *frames, size_t frames_cnt)
{
    CanBusSocketcanConnectState *c;
    c = container_of(client, CanBusSocketcanConnectState, bus_client);
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

static void can_bus_socketcan_cleanup(CanBusClientState *client)
{
    CanBusSocketcanConnectState *c;
    c = container_of(client, CanBusSocketcanConnectState, bus_client);

    if (c->fd >= 0) {
        qemu_set_fd_handler(c->fd, NULL, NULL, c);
        close(c->fd);
        c->fd = -1;
    }

    c->rfilter_num = 0;
    if (c->rfilter != NULL) {
        g_free(c->rfilter);
    }
}

static int can_bus_socketcan_set_filters(CanBusClientState *client,
                   const struct qemu_can_filter *filters, size_t filters_cnt)
{
    CanBusSocketcanConnectState *c;
    c = container_of(client, CanBusSocketcanConnectState, bus_client);

    int i;

    if (filters_cnt > 4) {
        return -1;
    }

    if (DEBUG_CAN) {
        for (i = 0; i < filters_cnt; i++) {
            fprintf(stderr, "[%i]  id=0x%08x maks=0x%08x\n",
                   i, filters[i].can_id, filters[i].can_mask);
        }
    }

    setsockopt(c->fd, SOL_CAN_RAW, CAN_RAW_FILTER,
               filters, filters_cnt * sizeof(qemu_can_filter));

    return 0;
}

static
void can_bus_socketcan_update_read_handler(CanBusSocketcanConnectState *c)
{
    if (c->fd >= 0) {
        qemu_set_fd_handler(c->fd, can_bus_socketcan_read, NULL, c);
    }
}

static CanBusClientInfo can_bus_socketcan_bus_client_info = {
    .can_receive = can_bus_socketcan_can_receive,
    .receive = can_bus_socketcan_receive,
    .cleanup = can_bus_socketcan_cleanup,
    .poll = NULL
};

static
CanBusSocketcanConnectState *can_bus_socketcan_connect_new(const char *host_dev_name)
{
    int s; /* can raw socket */
    CanBusSocketcanConnectState    *c;
    struct sockaddr_can addr;
    struct ifreq ifr;

    c = g_malloc0(sizeof(CanBusSocketcanConnectState));
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

    c->rfilter_num = 1;
    c->rfilter = g_malloc0(c->rfilter_num * sizeof(struct qemu_can_filter));
    if (c->rfilter == NULL) {
        goto fail;
    }

    /* Receive all data frame. If |= CAN_INV_FILTER no data. */
    c->rfilter[0].can_id = 0;
    c->rfilter[0].can_mask = 0;
    c->rfilter[0].can_mask &= ~CAN_ERR_FLAG;

    setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, c->rfilter,
               c->rfilter_num * sizeof(struct qemu_can_filter));

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        goto fail;
    }

    c->fd = s;

    c->bus_client.info = &can_bus_socketcan_bus_client_info;

    can_bus_socketcan_update_read_handler(c);

    return c;

fail:
    can_bus_socketcan_cleanup(&c->bus_client);
    g_free(c);
fail1:

    return NULL;
}

static
int can_bus_connect_to_host_socketcan(CanBusState *bus, const char *host_dev_name)
{
    CanBusSocketcanConnectState *c;

    c = can_bus_socketcan_connect_new(host_dev_name);
    if (c == NULL) {
        error_report("CAN bus setup of host connect to \"%s\" failed",
                      host_dev_name);
        exit(1);
    }

    if (can_bus_insert_client(bus, &c->bus_client) < 0) {
        error_report("CAN host device \"%s\" connect to bus \"%s\" failed",
                      host_dev_name, bus->name);
        exit(1);
    }

    if (0) {
        /*
         * Not used there or as a CanBusSocketcanConnectState method
         * for now but included there for documentation purposes
         * and to suppress warning.
         */
        can_bus_socketcan_set_filters(&c->bus_client, NULL, 0);
    }

    return 0;
}

int (*can_bus_connect_to_host_variant)(CanBusState *bus, const char *name) =
        can_bus_connect_to_host_socketcan;
