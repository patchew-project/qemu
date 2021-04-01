/*
 * VIRTIO I2C Emulation via vhost-user
 *
 * Copyright (c) 2021 Viresh Kumar <viresh.kumar@linaro.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define G_LOG_DOMAIN "vhost-user-i2c"
#define G_LOG_USE_STRUCTURED 1

#include <assert.h>
#include <endian.h>
#include <fcntl.h>
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>
#include <glib.h>
#include <glib-unix.h>
#include <glib/gstdio.h>
#include <inttypes.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "qemu/compiler.h"
#include "qemu/cutils.h"
#include "standard-headers/linux/virtio_i2c.h"
#include "subprojects/libvhost-user/libvhost-user-glib.h"
#include "subprojects/libvhost-user/libvhost-user.h"

#define VHOST_USER_I2C_MAX_QUEUES       1

/* vhost-user-i2c definitions */

#define MAX_I2C_VDEV                    (1 << 7)
#define MAX_I2C_ADAPTER                 16

typedef struct {
    int32_t fd;
    int32_t bus;
    bool smbus;
    bool clients[MAX_I2C_VDEV];
} VI2cAdapter;

typedef struct {
    VugDev dev;
    GMainLoop *loop;
    VI2cAdapter *adapter[MAX_I2C_ADAPTER];
    uint16_t adapter_map[MAX_I2C_VDEV];
    uint32_t adapter_num;
} VuI2c;

static gboolean print_cap, verbose;
static gchar *socket_path, *device_list;
static gint socket_fd = -1;

static GOptionEntry options[] = {
    { "socket-path", 's', 0, G_OPTION_ARG_FILENAME, &socket_path,
      "Location of vhost-user Unix domain socket, incompatible with --fd",
      "PATH" },
    { "fd", 'f', 0, G_OPTION_ARG_INT, &socket_fd,
      "Specify file-descriptor of the backend, don't use with --socket-path",
      "FD" },
    { "device-list", 'l', 0, G_OPTION_ARG_STRING, &device_list,
      "List of i2c-dev bus and attached devices", "I2C Devices" },
    { "print-capabilities", 'c', 0, G_OPTION_ARG_NONE, &print_cap,
      "Output to stdout the backend capabilities in JSON format and exit",
      NULL},
    { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
      "Be more verbose in output", NULL},
    { NULL }
};


/* I2c helpers */
static void fmt_bytes(GString *s, uint8_t *bytes, int len)
{
    int32_t i;
    for (i = 0; i < len; i++) {
        if (i && i % 16 == 0) {
            g_string_append_c(s, '\n');
        }
        g_string_append_printf(s, "%x ", bytes[i]);
    }
}

static void vi2c_dump_msg(struct i2c_msg *msgs, size_t count)
{
    int32_t i;

    for (i = 0; i < count; i++) {
        g_autoptr(GString) s = g_string_new("\nI2c request: ");

        g_string_append_printf(s, "addr: %x\n", msgs[i].addr);
        g_string_append_printf(s, "transfer len: %x\n", msgs[i].len);

        g_string_append_printf(s, "%s: ", msgs[i].flags & I2C_M_RD ?
                               "Data read" : "Data Written");
        fmt_bytes(s, (uint8_t *)msgs[i].buf, msgs[i].len);
        g_string_append_printf(s, "\n");

        g_debug("%s: %s", __func__, s->str);
    }
}

static int vi2c_map_adapters(VuI2c *i2c)
{
    VI2cAdapter *adapter;
    int32_t i, client_addr;

    /*
     * Flatten the map for client address and adapter to the array:
     *
     * adapter_map[MAX_I2C_VDEV]:
     *
     * Adapter        | adapter2 | none  | adapter1 | adapter3 | none| (val)
     *                |----------|-------|----------|----------|-----|
     * Slave Address  | addr 1   | none  | addr 2   | addr 3   | none| (idx)
     *                |<-----------------------MAX_I2C_VDEV---------------->|
     */
    for (i = 0; i < i2c->adapter_num; i++) {
        adapter = i2c->adapter[i];

        for (client_addr = 0; client_addr < MAX_I2C_VDEV; client_addr++) {
            if (adapter->clients[client_addr]) {
                if (i2c->adapter_map[client_addr]) {
                    g_printerr("client addr %x repeated, not supported!\n",
                               client_addr);
                    return -1;
                }

                /* The array is initialized to 0, + 1 for index */
                i2c->adapter_map[client_addr] = i + 1;
                if (verbose) {
                    g_print("client: 0x%x -> i2c adapter: %d\n", client_addr,
                            adapter->bus);
                }
            }
        }
    }
    return 0;
}

static VI2cAdapter *vi2c_find_adapter(VuI2c *i2c, uint16_t addr)
{
    if (addr < MAX_I2C_VDEV && (i2c->adapter_map[addr] != 0)) {
        return i2c->adapter[i2c->adapter_map[addr] - 1];
    }

    return NULL;
}

static bool vi2c_set_client_addr(VI2cAdapter *adapter, uint16_t addr)
{
    if (ioctl(adapter->fd, I2C_SLAVE, addr) < 0) {
        if (errno == EBUSY) {
            g_printerr("client device %x is busy!\n", addr);
        } else {
            g_printerr("client device %d does not exist!\n", addr);
        }
        return false;
    }
    return true;
}

static void vi2c_remove_adapters(VuI2c *i2c)
{
    VI2cAdapter *adapter;
    int32_t i;

    for (i = 0; i < MAX_I2C_ADAPTER; i++) {
        adapter = i2c->adapter[i];
        if (!adapter) {
            break;
        }

        if (adapter->fd > 0) {
            close(adapter->fd);
        }

        g_free(adapter);
        i2c->adapter[i] = NULL;
    }
}

static VI2cAdapter *vi2c_create_adapter(int32_t bus, uint16_t client_addr[],
                                        int32_t n_client)
{
    VI2cAdapter *adapter;
    char path[20];
    uint64_t funcs;
    int32_t fd, i;

    if (bus < 0) {
        return NULL;
    }

    adapter = g_malloc0(sizeof(*adapter));
    if (!adapter) {
        g_printerr("failed to alloc adapter");
        return NULL;
    }

    snprintf(path, sizeof(path), "/dev/i2c-%d", bus);
    path[sizeof(path) - 1] = '\0';

    fd = open(path, O_RDWR);
    if (fd < 0) {
        g_printerr("virtio_i2c: failed to open %s\n", path);
        goto fail;
    }

    adapter->fd = fd;
    adapter->bus = bus;

    if (ioctl(fd, I2C_FUNCS, &funcs) < 0) {
        g_printerr("virtio_i2c: failed to get functionality %s: %d\n", path,
                   errno);
        goto close_fd;
    }

    if (funcs & I2C_FUNC_I2C) {
        adapter->smbus = false;
    } else if (funcs & I2C_FUNC_SMBUS_WORD_DATA) {
        adapter->smbus = true;
    } else {
        g_printerr("virtio_i2c: invalid functionality %lx\n", funcs);
        goto close_fd;
    }

    for (i = 0; i < n_client; i++) {
        if (client_addr[i]) {
            if (!vi2c_set_client_addr(adapter, client_addr[i])) {
                goto close_fd;
            }

            if (adapter->clients[client_addr[i]]) {
                g_printerr("client addr 0x%x repeat, not allowed.\n",
                           client_addr[i]);
                goto close_fd;
            }

            adapter->clients[client_addr[i]] = true;
            if (verbose) {
                g_print("Added client 0x%x to bus %u\n", client_addr[i], bus);
            }
        }
    }

    if (verbose) {
        g_print("Added adapter: bus: %d, func %s\n", bus,
                adapter->smbus ? "smbus" : "i2c");
    }
    return adapter;

close_fd:
    close(fd);
fail:
    g_free(adapter);
    return NULL;
}

/*
 * Virtio I2C device list format.
 *
 * <bus>:<client_addr>[:<client_addr>],
 * [<bus>:<client_addr>[:<client_addr>]]
 *
 * bus (dec): adatper bus number.
 *     e.g. 2 for /dev/i2c-2
 * client_addr (hex): address for client device
 *     e.g. 0x1C or 1C
 *
 * Example: --device-list="2:0x1c:0x20,3:0x10:0x2c"
 *
 * Note: client address can not repeat.
 */
static int32_t vi2c_parse(VuI2c *i2c)
{
    uint16_t client_addr[MAX_I2C_VDEV];
    int32_t n_adapter = 0, n_client;
    int64_t addr, bus;
    const char *cp, *t;

    while (device_list) {
        /* Read <bus>:<client_addr>[:<client_addr>] entries one by one */
        cp = strsep(&device_list, ",");

        if (!cp || *cp == '\0') {
            break;
        }

        if (n_adapter == MAX_I2C_ADAPTER) {
            g_printerr("too many adapter (%d), only support %d\n", n_adapter,
                       MAX_I2C_ADAPTER);
            goto out;
        }

        if (qemu_strtol(cp, &t, 10, &bus) || bus < 0) {
            g_printerr("Invalid bus number %s\n", cp);
            goto out;
        }

        cp = t;
        n_client = 0;

        /* Parse clients <client_addr>[:<client_addr>] entries one by one */
        while (cp != NULL && *cp != '\0') {
            if (*cp == ':') {
                cp++;
            }

            if (n_client == MAX_I2C_VDEV) {
                g_printerr("too many devices (%d), only support %d\n",
                           n_client, MAX_I2C_VDEV);
                goto out;
            }

            if (qemu_strtol(cp, &t, 16, &addr) ||
                addr < 0 || addr > MAX_I2C_VDEV) {
                g_printerr("Invalid address %s : %lx\n", cp, addr);
                goto out;
            }

            client_addr[n_client++] = addr;
            cp = t;
            if (verbose) {
                g_print("i2c adapter %ld:0x%lx\n", bus, addr);
            }
        }

        i2c->adapter[n_adapter] = vi2c_create_adapter(bus, client_addr,
                                                      n_client);
        if (!i2c->adapter[n_adapter]) {
            goto out;
        }

        n_adapter++;
    }

    if (!n_adapter) {
        g_printerr("Failed to add any adapters\n");
        return -1;
    }

    i2c->adapter_num = n_adapter;

    if (!vi2c_map_adapters(i2c)) {
        return 0;
    }

out:
    vi2c_remove_adapters(i2c);
    return -1;
}

static int32_t i2c_xfer(VI2cAdapter *adapter, struct i2c_msg *msgs,
                        size_t count)
{
    struct i2c_rdwr_ioctl_data data;

    data.nmsgs = count;
    data.msgs = msgs;

    return ioctl(adapter->fd, I2C_RDWR, &data);
}

/*
 * Based on Linux's drivers/i2c/i2c-core-smbus.c:i2c_smbus_xfer_emulated(). This
 * function tries to reverse what Linux does, only support basic modes (up to
 * word transfer).
 */
static int32_t smbus_xfer(VI2cAdapter *adapter, struct i2c_msg *msgs,
                          size_t count)
{
    union i2c_smbus_data data;
    struct i2c_smbus_ioctl_data smbus_data = { .data = &data };
    int32_t ret;

    smbus_data.command = msgs[0].buf[0];

    switch (count) {
    case 1:
        if (msgs[0].flags & I2C_M_RD) {
            if (msgs[0].len > 1) {
                g_printerr("Incorrect message length for read operation: %d\n",
                           msgs[0].len);
                return -1;
            }
            smbus_data.read_write = I2C_SMBUS_READ;
        } else {
            smbus_data.read_write = I2C_SMBUS_WRITE;
        }

        if (!msgs[0].len) {
            smbus_data.size = I2C_SMBUS_QUICK;
            smbus_data.data = NULL;
        } else if (msgs[0].len == 1) {
            smbus_data.size = I2C_SMBUS_BYTE;
            if (smbus_data.read_write == I2C_SMBUS_WRITE) {
                smbus_data.data = NULL;
            }
        } else if (msgs[0].len == 2) {
            smbus_data.size = I2C_SMBUS_BYTE_DATA;
            data.byte = msgs[0].buf[1];
        } else if (msgs[0].len == 3) {
            smbus_data.size = I2C_SMBUS_WORD_DATA;
            data.word = msgs[0].buf[1] | (msgs[0].buf[2] << 8);
        } else {
            g_printerr("Message length not supported for write operation: %d\n",
                       msgs[0].len);
            return -1;
        }

        break;
    case 2:
        if ((msgs[0].flags & I2C_M_RD) || !(msgs[1].flags & I2C_M_RD) ||
            (msgs[0].len != 1) || (msgs[1].len > 2)) {
            g_printerr("Expecting a valid read smbus transfer: %ld: %d: %d\n",
                       count, msgs[0].len, msgs[1].len);
            return -1;
        }

        smbus_data.read_write = I2C_SMBUS_READ;
        smbus_data.size = msgs[1].len == 1 ?
                          I2C_SMBUS_BYTE_DATA : I2C_SMBUS_WORD_DATA;
        break;
    default:
        g_printerr("Invalid number of messages for smbus xfer: %ld\n", count);
        return -1;
    }

    if (verbose) {
        g_print("SMBUS command: %x: %x: %x\n", smbus_data.read_write,
                smbus_data.command, smbus_data.size);
    }

    ret = ioctl(adapter->fd, I2C_SMBUS, &smbus_data);
    if (ret < 0) {
        return ret;
    }

    if (smbus_data.read_write == I2C_SMBUS_WRITE) {
        return 0;
    }

    switch (smbus_data.size) {
    case I2C_SMBUS_BYTE:
        msgs[0].buf[0] = data.byte;
        break;
    case I2C_SMBUS_BYTE_DATA:
        msgs[1].buf[0] = data.byte;
        break;
    case I2C_SMBUS_WORD_DATA:
        msgs[1].buf[0] = data.word & 0xff;
        msgs[1].buf[1] = data.word >> 8;
        break;
    }

    return 0;
}

static uint8_t vi2c_xfer(VuDev *dev, struct i2c_msg *msgs, size_t count)
{
    VuI2c *i2c = container_of(dev, VuI2c, dev.parent);
    VI2cAdapter *adapter;
    int8_t ret;

    /* Assume that adapter should be same for all messages sent together */
    adapter = vi2c_find_adapter(i2c, msgs[0].addr);
    if (!adapter) {
        g_printerr("Failed to find adapter for address: %x\n", msgs[0].addr);
        return VIRTIO_I2C_MSG_ERR;
    }

    /* Set client's address */
    if (!vi2c_set_client_addr(adapter, msgs[0].addr)) {
        return VIRTIO_I2C_MSG_ERR;
    }

    if (adapter->smbus) {
        ret = smbus_xfer(adapter, msgs, count);
    } else {
        ret = i2c_xfer(adapter, msgs, count);
    }

    if (ret < 0) {
        g_printerr("Failed to transfer data to address %x : %d\n",
                   msgs[0].addr, errno);
        return VIRTIO_I2C_MSG_ERR;
    }

    if (verbose) {
        vi2c_dump_msg(msgs, count);
    }

    return VIRTIO_I2C_MSG_OK;
}

/* Virtio helpers */
static uint64_t vi2c_get_features(VuDev *dev)
{
    if (verbose) {
        g_info("%s: replying", __func__);
    }
    return 0;
}

static void vi2c_set_features(VuDev *dev, uint64_t features)
{
    if (verbose && features) {
        g_autoptr(GString) s = g_string_new("Requested un-handled feature");
        g_string_append_printf(s, " 0x%" PRIx64 "", features);
        g_info("%s: %s", __func__, s->str);
    }
}

static void vi2c_handle_ctrl(VuDev *dev, int qidx)
{
    VuVirtq *vq = vu_get_queue(dev, qidx);
    struct virtio_i2c_out_hdr *out_hdr;
    size_t i, count = 0, len, in_hdr_len;
    struct i2c_msg *msgs;
    VuVirtqElement *elem;
    uint8_t status;
    struct {
        struct virtio_i2c_in_hdr *in_hdr;
        VuVirtqElement *elem;
        size_t size;
    } *info;

    /* Count number of messages */
    do {
        elem = vu_queue_pop(dev, vq, sizeof(VuVirtqElement));
    } while (elem && ++count);

    if (!count) {
        if (verbose) {
            g_printerr("Virtqueue can't have 0 elements\n");
        }
        return;
    }

    /* Rewind everything, now that we have counted the number of messages */
    vu_queue_rewind(dev, vq, count);

    if (verbose) {
        g_print("Received %ld messages in virtqueue\n", count);
    }

    /* Allocate memory for msgs and info */
    msgs = g_malloc0_n(count, sizeof(*msgs) + sizeof(*info));
    if (!msgs) {
        g_printerr("failed to allocate space for messages\n");
        return;
    }
    info = (void *)(msgs + count);

    for (i = 0; i < count; i++) {
        elem = vu_queue_pop(dev, vq, sizeof(VuVirtqElement));
        if (!elem) {
            g_printerr("Failed to pop element: %ld : %ld\n", i, count);
            goto out;
        }
        info[i].elem = elem;
        info[i].size = sizeof(*info[i].in_hdr);

        g_debug("%s: got queue (in %d, out %d)", __func__, elem->in_num,
                elem->out_num);

        /* Validate size of "out" header */
        if (elem->out_sg[0].iov_len != sizeof(*out_hdr)) {
            g_warning("%s: Invalid out hdr %zu : %zu\n", __func__,
                      elem->out_sg[0].iov_len, sizeof(*out_hdr));
            goto out;
        }

        out_hdr = elem->out_sg[0].iov_base;

        /* Bit 0 is reserved in virtio spec */
        msgs[i].addr = le16toh(out_hdr->addr) >> 1;

        /* Read Operation */
        if (elem->out_num == 1 && elem->in_num == 2) {
            len = elem->in_sg[0].iov_len;
            if (!len) {
                g_warning("%s: Read buffer length can't be zero\n", __func__);
                goto out;
            }

            msgs[i].buf = elem->in_sg[0].iov_base;
            msgs[i].flags = I2C_M_RD;
            msgs[i].len = len;

            info[i].in_hdr = elem->in_sg[1].iov_base;
            info[i].size += len;
            in_hdr_len = elem->in_sg[1].iov_len;
        } else if (elem->out_num == 2 && elem->in_num == 1) {
            /* Write Operation */
            len = elem->out_sg[1].iov_len;
            if (!len) {
                g_warning("%s: Write buffer length can't be zero\n", __func__);
                goto out;
            }

            msgs[i].buf = elem->out_sg[1].iov_base;
            msgs[i].flags = 0;
            msgs[i].len = len;

            info[i].in_hdr = elem->in_sg[0].iov_base;
            in_hdr_len = elem->in_sg[0].iov_len;
        } else {
            g_warning("%s: Transfer type not supported (in %d, out %d)\n",
                      __func__, elem->in_num, elem->out_num);
            goto out;
        }

        /* Validate size of "in" header */
        if (in_hdr_len != sizeof(*(info[i].in_hdr))) {
            g_warning("%s: Invalid in hdr %zu : %zu\n", __func__, in_hdr_len,
                      sizeof(*(info[i].in_hdr)));
            goto out;
        }
    }

    status = vi2c_xfer(dev, msgs, count);

    for (i = 0; i < count; i++) {
        info[i].in_hdr->status = status;
        vu_queue_push(dev, vq, info[i].elem, info[i].size);
    }

    vu_queue_notify(dev, vq);

out:
    g_free(msgs);
}

static void
vi2c_queue_set_started(VuDev *dev, int qidx, bool started)
{
    VuVirtq *vq = vu_get_queue(dev, qidx);

    g_debug("queue started %d:%d\n", qidx, started);

    if (!qidx) {
        vu_set_queue_handler(dev, vq, started ? vi2c_handle_ctrl : NULL);
    }
}

/*
 * vi2c_process_msg: process messages of vhost-user interface
 *
 * Any that are not handled here are processed by the libvhost library
 * itself.
 */
static int vi2c_process_msg(VuDev *dev, VhostUserMsg *msg, int *do_reply)
{
    VuI2c *i2c = container_of(dev, VuI2c, dev.parent);

    if (msg->request == VHOST_USER_NONE) {
        g_main_loop_quit(i2c->loop);
        return 1;
    }

    return 0;
}

static const VuDevIface vuiface = {
    .set_features = vi2c_set_features,
    .get_features = vi2c_get_features,
    .queue_set_started = vi2c_queue_set_started,
    .process_msg = vi2c_process_msg,
};

static gboolean hangup(gpointer user_data)
{
    GMainLoop *loop = (GMainLoop *) user_data;
    g_info("%s: caught hangup/quit signal, quitting main loop", __func__);
    g_main_loop_quit(loop);
    return true;
}

static void vi2c_panic(VuDev *dev, const char *msg)
{
    g_critical("%s\n", msg);
    exit(EXIT_FAILURE);
}

/* Print vhost-user.json backend program capabilities */
static void print_capabilities(void)
{
    printf("{\n");
    printf("  \"type\": \"i2c\"\n");
    printf("  \"device-list\"\n");
    printf("}\n");
}

static void vi2c_destroy(VuI2c *i2c)
{
    vi2c_remove_adapters(i2c);
    vug_deinit(&i2c->dev);
    if (socket_path) {
        unlink(socket_path);
    }
}

int main(int argc, char *argv[])
{
    GError *error = NULL;
    GOptionContext *context;
    g_autoptr(GSocket) socket = NULL;
    VuI2c i2c = {0};

    context = g_option_context_new("vhost-user emulation of I2C device");
    g_option_context_add_main_entries(context, options, "vhost-user-i2c");
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_printerr("option parsing failed: %s\n", error->message);
        exit(1);
    }

    if (print_cap) {
        print_capabilities();
        exit(0);
    }

    if (!socket_path && socket_fd < 0) {
        g_printerr("Please specify either --fd or --socket-path\n");
        exit(EXIT_FAILURE);
    }

    if (verbose) {
        g_log_set_handler(NULL, G_LOG_LEVEL_MASK, g_log_default_handler, NULL);
        g_setenv("G_MESSAGES_DEBUG", "all", true);
    } else {
        g_log_set_handler(NULL, G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL |
                          G_LOG_LEVEL_ERROR, g_log_default_handler, NULL);
    }

    /*
     * Now create a vhost-user socket that we will receive messages
     * on. Once we have our handler set up we can enter the glib main
     * loop.
     */
    if (socket_path) {
        g_autoptr(GSocketAddress) addr = g_unix_socket_address_new(socket_path);
        g_autoptr(GSocket) bind_socket = g_socket_new(G_SOCKET_FAMILY_UNIX,
                    G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT, &error);

        if (!g_socket_bind(bind_socket, addr, false, &error)) {
            g_printerr("Failed to bind to socket at %s (%s).\n",
                       socket_path, error->message);
            exit(EXIT_FAILURE);
        }
        if (!g_socket_listen(bind_socket, &error)) {
            g_printerr("Failed to listen on socket %s (%s).\n",
                       socket_path, error->message);
        }
        g_message("awaiting connection to %s", socket_path);
        socket = g_socket_accept(bind_socket, NULL, &error);
        if (!socket) {
            g_printerr("Failed to accept on socket %s (%s).\n",
                       socket_path, error->message);
        }
    } else {
        socket = g_socket_new_from_fd(socket_fd, &error);
        if (!socket) {
            g_printerr("Failed to connect to FD %d (%s).\n",
                       socket_fd, error->message);
            exit(EXIT_FAILURE);
        }
    }

    if (vi2c_parse(&i2c)) {
        exit(EXIT_FAILURE);
    }

    /*
     * Create the main loop first so all the various sources can be
     * added. As well as catching signals we need to ensure vug_init
     * can add it's GSource watches.
     */

    i2c.loop = g_main_loop_new(NULL, FALSE);
    /* catch exit signals */
    g_unix_signal_add(SIGHUP, hangup, i2c.loop);
    g_unix_signal_add(SIGINT, hangup, i2c.loop);

    if (!vug_init(&i2c.dev, VHOST_USER_I2C_MAX_QUEUES, g_socket_get_fd(socket),
                  vi2c_panic, &vuiface)) {
        g_printerr("Failed to initialize libvhost-user-glib.\n");
        exit(EXIT_FAILURE);
    }


    g_message("entering main loop, awaiting messages");
    g_main_loop_run(i2c.loop);
    g_message("finished main loop, cleaning up");

    g_main_loop_unref(i2c.loop);
    vi2c_destroy(&i2c);
}
