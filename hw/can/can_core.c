/*
 * CAN common CAN bus emulation support
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

static QTAILQ_HEAD(, CanBusState) can_buses =
    QTAILQ_HEAD_INITIALIZER(can_buses);

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

int can_bus_filter_match(struct qemu_can_filter *filter, qemu_canid_t can_id)
{
    int m;
    if (((can_id | filter->can_mask) & QEMU_CAN_ERR_FLAG)) {
        return (filter->can_mask & QEMU_CAN_ERR_FLAG) != 0;
    }
    m = (can_id & filter->can_mask) == (filter->can_id & filter->can_mask);
    return filter->can_id & QEMU_CAN_INV_FILTER ? !m : m;
}

int can_bus_client_set_filters(CanBusClientState *client,
             const struct qemu_can_filter *filters, size_t filters_cnt)
{
    return 0;
}

int can_bus_connect_to_host_device(CanBusState *bus, const char *name)
{
    if (can_bus_connect_to_host_variant == NULL) {
        error_report("CAN bus connect to host device is not "
                     "supported on this system");
        exit(1);
    }
    return can_bus_connect_to_host_variant(bus, name);
}
