/*
 * vhost-user simtime device application
 *
 * Copyright (c) 2017 Intel Corporation. All rights reserved.
 * Copyright (c) 2019 Intel Corporation. All rights reserved.
 *
 * Author:
 *  Johannes Berg <johannes.berg@intel.com>
 *
 * This work is based on the "vhost-user-blk" sample code by
 * Changpeng Liu <changpeng.liu@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 only.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "standard-headers/linux/um_timetravel.h"
#include "cal.h"
#include "main.h"

#define DEBUG 1
#define DPRINT(...) do {              \
    if (DEBUG) {                      \
        fprintf(stderr, __VA_ARGS__); \
        fflush(stderr);               \
    }                                 \
} while (0)

typedef struct SimTimeConnection {
    GMutex lock;
    __u64 offset;
    GIOChannel *chan;
    GMainLoop *loop;
    int idx;
    SimCalendarEntry entry;
    unsigned long long num_requests, num_waits, num_updates;
    bool waiting;
} SimTimeConnection;

static int clients;

static const char *simtime_op_str(uint64_t op)
{
#define OPSTR(x) case UM_TIMETRAVEL_##x: return "UM_TIMETRAVEL_" #x
    switch (op) {
    OPSTR(ACK);
    OPSTR(REQUEST);
    OPSTR(WAIT);
    OPSTR(GET);
    OPSTR(UPDATE);
    OPSTR(RUN);
    OPSTR(FREE_UNTIL);
    default:
        return "unknown";
    }
}

static int full_read(int fd, void *_buf, size_t len)
{
    unsigned char *buf = _buf;
    ssize_t ret;

    do {
        ret = read(fd, buf, len);
        if (ret > 0) {
            buf += ret;
            len -= ret;
        } else if (ret == 0) {
            return 0;
        } else {
            return -errno;
        }
    } while (len > 0);

    return buf - (unsigned char *)_buf;
}

static int full_write(int fd, const void *_buf, size_t len)
{
    const unsigned char *buf = _buf;
    ssize_t ret;

    do {
        ret = write(fd, buf, len);
        if (ret > 0) {
            buf += ret;
            len -= ret;
        } else if (ret == 0) {
            return 0;
        } else {
            return -errno;
        }
    } while (len > 0);

    return buf - (const unsigned char *)_buf;
}

static void simtime_handle_message(SimTimeConnection *conn,
                                   struct um_timetravel_msg *msg)
{
    struct um_timetravel_msg resp = {
        .op = UM_TIMETRAVEL_ACK,
    };

    DPRINT(" %d | message %s (%lld, time=%lld)\n",
           conn->idx, simtime_op_str(msg->op), msg->op, msg->time);

    switch (msg->op) {
    case UM_TIMETRAVEL_REQUEST:
        if (calendar_entry_remove(&conn->entry)) {
            conn->entry.time = conn->offset + msg->time;
            calendar_entry_add(&conn->entry);
            DPRINT(" %d | calendar entry added for %lld\n", conn->idx, msg->time);
        } else {
            conn->entry.time = conn->offset + msg->time;
            DPRINT(" %d | calendar entry time updated for %lld\n", conn->idx, msg->time);
        }
        conn->num_requests++;
        break;
    case UM_TIMETRAVEL_WAIT:
        conn->num_waits++;
        calendar_entry_add(&conn->entry);
        calendar_run_done(&conn->entry);
        break;
    case UM_TIMETRAVEL_GET:
        resp.time = calendar_get_time() - conn->offset;
        DPRINT(" %d | returning time %lld\n", conn->idx, resp.time);
        break;
    case UM_TIMETRAVEL_UPDATE:
        if (conn->offset + msg->time > conn->entry.time) {
            calendar_entry_remove(&conn->entry);
        }
        calendar_set_time(conn->offset + msg->time);
        conn->num_updates++;
        break;
    default:
        printf("ignoring invalid message %llu (time %llu)\n",
               msg->op, msg->time);
        break;
    }

    g_assert(full_write(g_io_channel_unix_get_fd(conn->chan), &resp, sizeof(resp)) == sizeof(resp));
    DPRINT(" %d | sent ACK for message %s (%lld, time %lld)\n", conn->idx, simtime_op_str(msg->op), msg->op, msg->time);
}

static void simtime_send_message(SimTimeConnection *conn,
                                 struct um_timetravel_msg *msg)
{
    g_mutex_lock(&conn->lock);
    DPRINT(" %d | send %s (%lld, time=%lld)\n",
           conn->idx, simtime_op_str(msg->op), msg->op, msg->time);
    g_io_channel_set_flags(conn->chan, 0, NULL);
    g_assert(full_write(g_io_channel_unix_get_fd(conn->chan), msg, sizeof(*msg)) == sizeof(*msg));
    do {
        g_assert(full_read(g_io_channel_unix_get_fd(conn->chan), msg, sizeof(*msg)) == sizeof(*msg));
        DPRINT(" %d | read %s (%lld, time=%lld), expecting ACK (0)\n",
               conn->idx, simtime_op_str(msg->op), msg->op, msg->time);
        if (msg->op == UM_TIMETRAVEL_ACK)
            break;
        simtime_handle_message(conn, msg);
    } while (1);
    g_io_channel_set_flags(conn->chan, G_IO_FLAG_NONBLOCK, NULL);
    g_mutex_unlock(&conn->lock);
}

static void simtime_calendar_cb(SimCalendarEntry *entry)
{
    SimTimeConnection *conn = container_of(entry, SimTimeConnection, entry);
    struct um_timetravel_msg msg = {
        .op = UM_TIMETRAVEL_RUN,
        .time = entry->time - conn->offset,
    };

    simtime_send_message(conn, &msg);
}

static void __attribute__((used))
simtime_update_until_cb(SimCalendarEntry *entry, unsigned long long time)
{
    SimTimeConnection *conn = container_of(entry, SimTimeConnection, entry);
    struct um_timetravel_msg msg = {
        .op = UM_TIMETRAVEL_FREE_UNTIL,
        .time = time - conn->offset,
    };

    simtime_send_message(conn, &msg);
}

static gboolean simtime_read_cb(GIOChannel *src, GIOCondition c, gpointer data)
{
    SimTimeConnection *conn = data;
    int fd = g_io_channel_unix_get_fd(src);
    struct um_timetravel_msg msg;
    int bytes;

    g_mutex_lock(&conn->lock);
    DPRINT(" %d | locked connection for reading\n", conn->idx);
    bytes = full_read(fd, &msg, sizeof(msg));
    if (bytes < 0 && bytes == -EAGAIN) {
        g_mutex_unlock(&conn->lock);
        return TRUE;
    }
    if (bytes <= 0) {
        clients--;
        printf("client disconnected, made %lld requests and waited %lld times, sent %lld updates\n",
               conn->num_requests, conn->num_waits, conn->num_updates);
        printf("we now have %d clients left\n", clients);

        calendar_entry_destroy(&conn->entry);
        // leak for now ... source is still attached
        //g_free(conn);
        g_mutex_unlock(&conn->lock);
        return FALSE;
    }
    g_assert(bytes == sizeof(msg));

    simtime_handle_message(conn, &msg);
    g_mutex_unlock(&conn->lock);
    DPRINT(" %d | unlocked connection\n", conn->idx);
    return TRUE;
}

gboolean simtime_client_connected(GIOChannel *listen_src,
                                  GIOCondition cond,
                                  gpointer data)
{
    int lsock = g_io_channel_unix_get_fd(listen_src);
    int csock = accept(lsock, NULL, NULL);
    SimTimeConnection *conn;
    GSource *src;

    if (csock < 0) {
        fprintf(stderr, "Accept error %s\n", strerror(errno));
        return TRUE;
    }

    conn = g_new0(SimTimeConnection, 1);
    if (!conn) {
        return TRUE;
    }

    g_mutex_init(&conn->lock);

    conn->chan = g_io_channel_unix_new(csock);
    g_io_channel_set_flags(conn->chan, G_IO_FLAG_NONBLOCK, NULL);
    src = g_io_create_watch(conn->chan, G_IO_IN);
    g_source_set_callback(src, G_SOURCE_FUNC(simtime_read_cb), conn, NULL);
    g_source_attach(src, g_main_context_get_thread_default());

    conn->entry.callback = simtime_calendar_cb;
    conn->entry.update_until = simtime_update_until_cb;
    /*
     * Mark this as a real scheduling client for purposes of
     * tracking the number of them connected to the sim.
     */
    conn->entry.client = true;

    clients++;
    printf("client connected (now have %d)\n", clients);

    conn->offset = calendar_get_time();
    conn->idx = clients;
    conn->entry.name = g_strdup_printf("time %d", clients);

    return TRUE;
}
