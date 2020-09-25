/*
 * VIRTIO RPMB Emulation via vhost-user
 *
 * Copyright (c) 2020 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define G_LOG_DOMAIN "vhost-user-rpmb"
#define G_LOG_USE_STRUCTURED 1

#include <glib.h>
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>
#include <glib-unix.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <endian.h>
#include <assert.h>

#include "contrib/libvhost-user/libvhost-user-glib.h"
#include "contrib/libvhost-user/libvhost-user.h"

#include "hmac_sha256.h"

#ifndef container_of
#define container_of(ptr, type, member) ({                      \
        const typeof(((type *) 0)->member) *__mptr = (ptr);     \
        (type *) ((char *) __mptr - offsetof(type, member));})
#endif

static gchar *socket_path;
static char *flash_path;
static gint socket_fd = -1;
static gboolean print_cap;
static gboolean verbose;
static gboolean debug;

static GOptionEntry options[] =
{
    { "socket-path", 0, 0, G_OPTION_ARG_FILENAME, &socket_path, "Location of vhost-user Unix domain socket, incompatible with --fd", "PATH" },
    { "flash-path", 0, 0, G_OPTION_ARG_FILENAME, &flash_path, "Location of raw flash image file", "PATH" },
    { "fd", 0, 0, G_OPTION_ARG_INT, &socket_fd, "Specify the file-descriptor of the backend, incompatible with --socket-path", "FD" },
    { "print-capabilities", 0, 0, G_OPTION_ARG_NONE, &print_cap, "Output to stdout the backend capabilities in JSON format and exit", NULL},
    { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose, "Be more verbose in output", NULL},
    { "debug", 0, 0, G_OPTION_ARG_NONE, &debug, "Include debug output", NULL},
    { NULL }
};

enum {
    VHOST_USER_RPMB_MAX_QUEUES = 1,
};

/* These structures are defined in the specification */
#define KiB     (1UL << 10)
#define MAX_RPMB_SIZE (KiB * 128 * 256)
#define RPMB_KEY_MAC_SIZE 32
#define RPMB_BLOCK_SIZE 256

/* RPMB Request Types */
#define VIRTIO_RPMB_REQ_PROGRAM_KEY        0x0001
#define VIRTIO_RPMB_REQ_GET_WRITE_COUNTER  0x0002
#define VIRTIO_RPMB_REQ_DATA_WRITE         0x0003
#define VIRTIO_RPMB_REQ_DATA_READ          0x0004
#define VIRTIO_RPMB_REQ_RESULT_READ        0x0005

/* RPMB Response Types */
#define VIRTIO_RPMB_RESP_PROGRAM_KEY       0x0100
#define VIRTIO_RPMB_RESP_GET_COUNTER       0x0200
#define VIRTIO_RPMB_RESP_DATA_WRITE        0x0300
#define VIRTIO_RPMB_RESP_DATA_READ         0x0400

/* RPMB Operation Results */
#define VIRTIO_RPMB_RES_OK                     0x0000
#define VIRTIO_RPMB_RES_GENERAL_FAILURE        0x0001
#define VIRTIO_RPMB_RES_AUTH_FAILURE           0x0002
#define VIRTIO_RPMB_RES_COUNT_FAILURE          0x0003
#define VIRTIO_RPMB_RES_ADDR_FAILURE           0x0004
#define VIRTIO_RPMB_RES_WRITE_FAILURE          0x0005
#define VIRTIO_RPMB_RES_READ_FAILURE           0x0006
#define VIRTIO_RPMB_RES_NO_AUTH_KEY            0x0007
#define VIRTIO_RPMB_RES_WRITE_COUNTER_EXPIRED  0x0080

struct virtio_rpmb_config {
    uint8_t capacity;
    uint8_t max_wr_cnt;
    uint8_t max_rd_cnt;
};

/*
 * This is based on the JDEC standard and not the currently not
 * up-streamed NVME standard.
 */
struct virtio_rpmb_frame {
    uint8_t stuff[196];
    uint8_t key_mac[RPMB_KEY_MAC_SIZE];
    uint8_t data[RPMB_BLOCK_SIZE];
    uint8_t nonce[16];
    /* remaining fields are big-endian */
    uint32_t write_counter;
    uint16_t address;
    uint16_t block_count;
    uint16_t result;
    uint16_t req_resp;
} __attribute__((packed));

/*
 * Structure to track internal state of RPMB Device
 */

typedef struct VuRpmb {
    VugDev dev;
    struct virtio_rpmb_config virtio_config;
    GMainLoop *loop;
    int flash_fd;
    void *flash_map;
    uint8_t *key;
    uint8_t  last_nonce[16];
    uint16_t last_result;
    uint16_t last_reqresp;
    uint16_t last_address;
    uint32_t write_count;
} VuRpmb;

/* refer to util/iov.c */
static size_t vrpmb_iov_size(const struct iovec *iov,
                             const unsigned int iov_cnt)
{
    size_t len;
    unsigned int i;

    len = 0;
    for (i = 0; i < iov_cnt; i++) {
        len += iov[i].iov_len;
    }
    return len;
}


static size_t vrpmb_iov_to_buf(const struct iovec *iov, const unsigned int iov_cnt,
                               size_t offset, void *buf, size_t bytes)
{
    size_t done;
    unsigned int i;
    for (i = 0, done = 0; (offset || done < bytes) && i < iov_cnt; i++) {
        if (offset < iov[i].iov_len) {
            size_t len = MIN(iov[i].iov_len - offset, bytes - done);
            memcpy(buf + done, iov[i].iov_base + offset, len);
            done += len;
            offset = 0;
        } else {
            offset -= iov[i].iov_len;
        }
    }
    assert(offset == 0);
    return done;
}

static size_t vrpmb_iov_from_buf(const struct iovec *iov, unsigned int iov_cnt,
                                 size_t offset, const void *buf, size_t bytes)
{
    size_t done;
    unsigned int i;
    for (i = 0, done = 0; (offset || done < bytes) && i < iov_cnt; i++) {
        if (offset < iov[i].iov_len) {
            size_t len = MIN(iov[i].iov_len - offset, bytes - done);
            memcpy(iov[i].iov_base + offset, buf + done, len);
            done += len;
            offset = 0;
        } else {
            offset -= iov[i].iov_len;
        }
    }
    assert(offset == 0);
    return done;
}

static void vrpmb_panic(VuDev *dev, const char *msg)
{
    g_critical("%s\n", msg);
    exit(EXIT_FAILURE);
}

static uint64_t vrpmb_get_features(VuDev *dev)
{
    g_info("%s: replying", __func__);
    return 0;
}

static void vrpmb_set_features(VuDev *dev, uint64_t features)
{
    if (features) {
        g_autoptr(GString) s = g_string_new("Requested un-handled feature");
        g_string_append_printf(s, " 0x%" PRIx64 "", features);
        g_info("%s: %s", __func__, s->str);
    }
}

/*
 * The configuration of the device is static and set when we start the
 * daemon.
 */
static int
vrpmb_get_config(VuDev *dev, uint8_t *config, uint32_t len)
{
    VuRpmb *r = container_of(dev, VuRpmb, dev.parent);
    g_return_val_if_fail(len <= sizeof(struct virtio_rpmb_config), -1);
    memcpy(config, &r->virtio_config, len);

    g_info("%s: done", __func__);
    return 0;
}

static int
vrpmb_set_config(VuDev *dev, const uint8_t *data,
                 uint32_t offset, uint32_t size,
                 uint32_t flags)
{
    /* ignore */
    return 0;
}

/*
 * vrpmb_update_mac_in_frame:
 *
 * From the spec:
 *   The MAC is calculated using HMAC SHA-256. It takes
 *   as input a key and a message. The key used for the MAC calculation
 *   is always the 256-bit RPMB authentication key. The message used as
 *   input to the MAC calculation is the concatenation of the fields in
 *   the RPMB frames excluding stuff bytes and the MAC itself.
 *
 * The code to do this has been lifted from the optee supplicant code
 * which itself uses a 3 clause BSD chunk of code.
 */

static const int rpmb_frame_dlen = (sizeof(struct virtio_rpmb_frame) -
                                    offsetof(struct virtio_rpmb_frame, data));

static void vrpmb_update_mac_in_frame(VuRpmb *r, struct virtio_rpmb_frame *frm)
{
    hmac_sha256_ctx ctx;

    hmac_sha256_init(&ctx, r->key, RPMB_KEY_MAC_SIZE);
    hmac_sha256_update(&ctx, frm->data, rpmb_frame_dlen);
    hmac_sha256_final(&ctx, &frm->key_mac[0], 32);
}

static bool vrpmb_verify_mac_in_frame(VuRpmb *r, struct virtio_rpmb_frame *frm)
{
    hmac_sha256_ctx ctx;
    uint8_t calculated_mac[RPMB_KEY_MAC_SIZE];

    hmac_sha256_init(&ctx, r->key, RPMB_KEY_MAC_SIZE);
    hmac_sha256_update(&ctx, frm->data, rpmb_frame_dlen);
    hmac_sha256_final(&ctx, calculated_mac, RPMB_KEY_MAC_SIZE);

    return memcmp(calculated_mac, frm->key_mac, RPMB_KEY_MAC_SIZE) == 0;
}

/*
 * Handlers for individual control messages
 */

/*
 * vrpmb_handle_program_key:
 *
 * Program the device with our key. The spec is a little hazzy on if
 * we respond straight away or we wait for the user to send a
 * VIRTIO_RPMB_REQ_RESULT_READ request.
 */
static void vrpmb_handle_program_key(VuDev *dev, struct virtio_rpmb_frame *frame)
{
    VuRpmb *r = container_of(dev, VuRpmb, dev.parent);

    /*
     * Run the checks from:
     * 5.12.6.1.1 Device Requirements: Device Operation: Program Key
     */
    r->last_reqresp = VIRTIO_RPMB_RESP_PROGRAM_KEY;

    /* Fail if already programmed */
    if (r->key) {
        g_debug("key already programmed");
        r->last_result = VIRTIO_RPMB_RES_WRITE_FAILURE;
    } else if (be16toh(frame->block_count) != 1) {
        g_debug("weird block counts (%d)", frame->block_count);
        r->last_result = VIRTIO_RPMB_RES_GENERAL_FAILURE;
    } else {
        r->key = g_memdup(&frame->key_mac[0], RPMB_KEY_MAC_SIZE);
        r->last_result = VIRTIO_RPMB_RES_OK;
    }

    g_info("%s: req_resp = %x, result = %x", __func__,
           r->last_reqresp, r->last_result);
    return;
}

/*
 * vrpmb_handle_get_write_counter:
 *
 * We respond straight away with re-using the frame as sent.
 */
static struct virtio_rpmb_frame *
vrpmb_handle_get_write_counter(VuDev *dev, struct virtio_rpmb_frame *frame)
{
    VuRpmb *r = container_of(dev, VuRpmb, dev.parent);
    struct virtio_rpmb_frame *resp = g_new0(struct virtio_rpmb_frame, 1);

    /*
     * Run the checks from:
     * 5.12.6.1.2 Device Requirements: Device Operation: Get Write Counter
     */

    resp->req_resp = htobe16(VIRTIO_RPMB_RESP_GET_COUNTER);
    if (!r->key) {
        g_debug("no key programmed");
        resp->result = htobe16(VIRTIO_RPMB_RES_NO_AUTH_KEY);
        return resp;
    } else if (be16toh(frame->block_count) > 1) { /* allow 0 (NONCONF) */
        g_debug("invalid block count (%d)", be16toh(frame->block_count));
        resp->result = htobe16(VIRTIO_RPMB_RES_GENERAL_FAILURE);
    } else {
        resp->write_counter = htobe32(r->write_count);
    }
    /* copy nonce */
    memcpy(&resp->nonce, &frame->nonce, sizeof(frame->nonce));

    /* calculate MAC */
    vrpmb_update_mac_in_frame(r, resp);

    return resp;
}

/*
 * vrpmb_handle_write:
 *
 * We will report the success/fail on receipt of
 * VIRTIO_RPMB_REQ_RESULT_READ. Returns the number of extra frames
 * processed in the request.
 */
static int vrpmb_handle_write(VuDev *dev, struct virtio_rpmb_frame *frame)
{
    VuRpmb *r = container_of(dev, VuRpmb, dev.parent);
    int extra_frames = 0;
    uint16_t block_count = be16toh(frame->block_count);
    uint32_t write_counter = be32toh(frame->write_counter);
    size_t offset;

    r->last_reqresp = VIRTIO_RPMB_RESP_DATA_WRITE;
    r->last_address = be16toh(frame->address);
    offset =  r->last_address * RPMB_BLOCK_SIZE;

    /*
     * Run the checks from:
     * 5.12.6.1.3 Device Requirements: Device Operation: Data Write
     */
    if (!r->key) {
        g_warning("no key programmed");
        r->last_result = VIRTIO_RPMB_RES_NO_AUTH_KEY;
    } else if (block_count == 0 ||
               block_count > r->virtio_config.max_wr_cnt) {
        r->last_result = VIRTIO_RPMB_RES_GENERAL_FAILURE;
    } else if (false /* what does an expired write counter mean? */) {
        r->last_result = VIRTIO_RPMB_RES_WRITE_COUNTER_EXPIRED;
    } else if (offset > (r->virtio_config.capacity * (128 * KiB))) {
        r->last_result = VIRTIO_RPMB_RES_ADDR_FAILURE;
    } else if (!vrpmb_verify_mac_in_frame(r, frame)) {
        r->last_result = VIRTIO_RPMB_RES_AUTH_FAILURE;
    } else if (write_counter != r->write_count) {
        r->last_result = VIRTIO_RPMB_RES_COUNT_FAILURE;
    } else {
        int i;
        /* At this point we have a valid authenticated write request
         * so the counter can incremented and we can attempt to
         * update the backing device.
         */
        r->write_count++;
        for (i = 0; i < block_count; i++) {
            void *blk = r->flash_map + offset;
            g_debug("%s: writing block %d", __func__, i);
            if (mprotect(blk, RPMB_BLOCK_SIZE, PROT_WRITE) != 0) {
                r->last_result =  VIRTIO_RPMB_RES_WRITE_FAILURE;
                break;
            }
            memcpy(blk, frame[i].data, RPMB_BLOCK_SIZE);
            if (msync(blk, RPMB_BLOCK_SIZE, MS_SYNC) != 0) {
                g_warning("%s: failed to sync update", __func__);
                r->last_result = VIRTIO_RPMB_RES_WRITE_FAILURE;
                break;
            }
            if (mprotect(blk, RPMB_BLOCK_SIZE, PROT_READ) != 0) {
                g_warning("%s: failed to re-apply read protection", __func__);
                r->last_result = VIRTIO_RPMB_RES_GENERAL_FAILURE;
                break;
            }
            offset += RPMB_BLOCK_SIZE;
        }
        r->last_result = VIRTIO_RPMB_RES_OK;
        extra_frames = i - 1;
    }

    g_info("%s: %s (%x, %d extra frames processed), write_count=%d", __func__,
           r->last_result == VIRTIO_RPMB_RES_OK ? "successful":"failed",
           r->last_result, extra_frames, r->write_count);

    return extra_frames;
}


/*
 * Return the result of the last message. This is only valid if the
 * previous message was VIRTIO_RPMB_REQ_PROGRAM_KEY or
 * VIRTIO_RPMB_REQ_DATA_WRITE.
 *
 * The frame should be freed once sent.
 */
static struct virtio_rpmb_frame * vrpmb_handle_result_read(VuDev *dev)
{
    VuRpmb *r = container_of(dev, VuRpmb, dev.parent);
    struct virtio_rpmb_frame *resp = g_new0(struct virtio_rpmb_frame, 1);

    g_info("%s: for request:%x result:%x", __func__,
           r->last_reqresp, r->last_result);

    if (r->last_reqresp == VIRTIO_RPMB_RESP_PROGRAM_KEY) {
        resp->result = htobe16(r->last_result);
        resp->req_resp = htobe16(r->last_reqresp);
    } else if (r->last_reqresp == VIRTIO_RPMB_RESP_DATA_WRITE) {
        resp->result = htobe16(r->last_result);
        resp->req_resp = htobe16(r->last_reqresp);
        resp->write_counter = htobe32(r->write_count);
        resp->address = htobe16(r->last_address);
    } else {
        resp->result = htobe16(VIRTIO_RPMB_RES_GENERAL_FAILURE);
    }

    /* calculate HMAC */
    if (!r->key) {
        resp->result = htobe16(VIRTIO_RPMB_RES_GENERAL_FAILURE);
    } else {
        vrpmb_update_mac_in_frame(r, resp);
    }

    g_info("%s: result = %x req_resp = %x", __func__,
           be16toh(resp->result),
           be16toh(resp->req_resp));
    return resp;
}

static void fmt_bytes(GString *s, uint8_t *bytes, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        if (i % 16 == 0) {
            g_string_append_c(s, '\n');
        }
        g_string_append_printf(s, "%x ", bytes[i]);
    }
}

static void vrpmb_dump_frame(struct virtio_rpmb_frame *frame)
{
    g_autoptr(GString) s = g_string_new("frame: ");

    g_string_append_printf(s, " %p\n", frame);
    g_string_append_printf(s, "key_mac:");
    fmt_bytes(s, (uint8_t *) &frame->key_mac[0], 32);
    g_string_append_printf(s, "\ndata:");
    fmt_bytes(s, (uint8_t *) &frame->data, 256);
    g_string_append_printf(s, "\nnonce:");
    fmt_bytes(s, (uint8_t *) &frame->nonce, 16);
    g_string_append_printf(s, "\nwrite_counter: %d\n",
                           be32toh(frame->write_counter));
    g_string_append_printf(s, "address: %#04x\n", be16toh(frame->address));
    g_string_append_printf(s, "block_count: %d\n", be16toh(frame->block_count));
    g_string_append_printf(s, "result: %d\n", be16toh(frame->result));
    g_string_append_printf(s, "req_resp: %d\n", be16toh(frame->req_resp));

    g_debug("%s: %s\n", __func__, s->str);
}

static void
vrpmb_handle_ctrl(VuDev *dev, int qidx)
{
    VuVirtq *vq = vu_get_queue(dev, qidx);
    struct virtio_rpmb_frame *frames = NULL;

    for (;;) {
        VuVirtqElement *elem;
        size_t len, frame_sz = sizeof(struct virtio_rpmb_frame);
        int n;

        elem = vu_queue_pop(dev, vq, sizeof(VuVirtqElement));
        if (!elem) {
            break;
        }
        g_debug("%s: got queue (in %d, out %d)", __func__,
                elem->in_num, elem->out_num);

        len = vrpmb_iov_size(elem->out_sg, elem->out_num);
        frames = g_realloc(frames, len);
        vrpmb_iov_to_buf(elem->out_sg, elem->out_num, 0, frames, len);

        if (len % frame_sz != 0) {
            g_warning("%s: incomplete frames %zu/%zu != 0\n",
                      __func__, len, frame_sz);
        }

        for (n = 0; n < len / frame_sz; n++) {
            struct virtio_rpmb_frame *f = &frames[n];
            struct virtio_rpmb_frame *resp = NULL;
            uint16_t req_resp = be16toh(f->req_resp);
            bool responded = false;

            if (debug) {
                g_info("req_resp=%x", req_resp);
                vrpmb_dump_frame(f);
            }

            switch (req_resp) {
            case VIRTIO_RPMB_REQ_PROGRAM_KEY:
                vrpmb_handle_program_key(dev, f);
                break;
            case VIRTIO_RPMB_REQ_GET_WRITE_COUNTER:
                resp = vrpmb_handle_get_write_counter(dev, f);
                break;
            case VIRTIO_RPMB_REQ_RESULT_READ:
                if (!responded) {
                    resp = vrpmb_handle_result_read(dev);
                } else {
                    g_warning("%s: already sent a response in this set of frames",
                              __func__);
                }
                break;
            case VIRTIO_RPMB_REQ_DATA_WRITE:
                /* we can have multiple blocks handled */
                n += vrpmb_handle_write(dev, f);
                break;
            default:
                g_debug("un-handled request: %x", f->req_resp);
                break;
            }

            /*
             * Do we have a frame to send back?
             */
            if (resp) {
                g_debug("sending response frame: %p", resp);
                if (debug) {
                    vrpmb_dump_frame(resp);
                }
                len = vrpmb_iov_from_buf(elem->in_sg,
                                         elem->in_num, 0, resp, sizeof(*resp));
                if (len != sizeof(*resp)) {
                    g_critical("%s: response size incorrect %zu vs %zu",
                               __func__, len, sizeof(*resp));
                } else {
                    vu_queue_push(dev, vq, elem, len);
                    vu_queue_notify(dev, vq);
                    responded = true;
                }

                g_free(resp);
            }
        }
    }
}

static void
vrpmb_queue_set_started(VuDev *dev, int qidx, bool started)
{
    VuVirtq *vq = vu_get_queue(dev, qidx);

    g_debug("queue started %d:%d\n", qidx, started);

    switch (qidx) {
    case 0:
        vu_set_queue_handler(dev, vq, started ? vrpmb_handle_ctrl : NULL);
        break;
    default:
        break;
    }
}

/*
 * vrpmb_process_msg: process messages of vhost-user interface
 *
 * Any that are not handled here are processed by the libvhost library
 * itself.
 */
static int vrpmb_process_msg(VuDev *dev, VhostUserMsg *msg, int *do_reply)
{
    VuRpmb *r = container_of(dev, VuRpmb, dev.parent);

    g_info("%s: msg %d", __func__, msg->request);

    switch (msg->request) {
    case VHOST_USER_NONE:
        g_main_loop_quit(r->loop);
        return 1;
    default:
        return 0;
    }

    return 0;
}

static const VuDevIface vuiface = {
    .set_features = vrpmb_set_features,
    .get_features = vrpmb_get_features,
    .queue_set_started = vrpmb_queue_set_started,
    .process_msg = vrpmb_process_msg,
    .get_config = vrpmb_get_config,
    .set_config = vrpmb_set_config,
};

static bool vrpmb_load_flash_image(VuRpmb *r, char *img_path)
{
    GStatBuf statbuf;
    size_t map_size;

    if (g_stat(img_path, &statbuf) < 0) {
        g_error("couldn't stat %s", img_path);
        return false;
    }

    r->flash_fd = g_open(img_path, O_RDWR, 0);
    if (r->flash_fd < 0) {
        g_error("couldn't open %s (%s)", img_path, strerror(errno));
        return false;
    }

    if (statbuf.st_size > MAX_RPMB_SIZE) {
        g_warning("%s larger than maximum size supported", img_path);
        map_size = MAX_RPMB_SIZE;
    } else {
        map_size = statbuf.st_size;
    }
    r->virtio_config.capacity = map_size / (128 *KiB);
    r->virtio_config.max_wr_cnt = 1;
    r->virtio_config.max_rd_cnt = 1;

    r->flash_map = mmap(NULL, map_size, PROT_READ, MAP_SHARED, r->flash_fd, 0);
    if (r->flash_map == MAP_FAILED) {
        g_error("failed to mmap file");
        return false;
    }

    return true;
}

static void vrpmb_destroy(VuRpmb *r)
{
    vug_deinit(&r->dev);
    if (socket_path) {
        unlink(socket_path);
    }
}

/* Print vhost-user.json backend program capabilities */
static void print_capabilities(void)
{
    printf("{\n");
    printf("  \"type\": \"block\"\n");
    printf("}\n");
}

static gboolean hangup(gpointer user_data)
{
    GMainLoop *loop = (GMainLoop *) user_data;
    g_info("%s: caught hangup/quit signal, quitting main loop", __func__);
    g_main_loop_quit(loop);
    return true;
}

int main(int argc, char *argv[])
{
    GError *error = NULL;
    GOptionContext *context;
    g_autoptr(GSocket) socket = NULL;
    VuRpmb rpmb = {  };

    context = g_option_context_new ("vhost-user emulation of RPBM device");
    g_option_context_add_main_entries (context, options, "vhost-user-rpmb");
    if (!g_option_context_parse (context, &argc, &argv, &error))
    {
        g_print ("option parsing failed: %s\n", error->message);
        exit (1);
    }

    if (print_cap) {
        print_capabilities();
        exit(0);
    }

    if (!flash_path || !g_file_test(flash_path, G_FILE_TEST_EXISTS)) {
        g_printerr("Please specify a valid --flash-path for the flash image\n");
        exit(EXIT_FAILURE);
    } else {
        vrpmb_load_flash_image(&rpmb, flash_path);
    }

    if (!socket_path && socket_fd < 0) {
        g_printerr("Please specify either --fd or --socket-path\n");
        exit(EXIT_FAILURE);
    }

    if (verbose || debug) {
        g_log_set_handler(NULL, G_LOG_LEVEL_MASK, g_log_default_handler, NULL);
        if (debug) {
            g_setenv("G_MESSAGES_DEBUG", "all", true);
        }
    } else {
        g_log_set_handler(NULL,
                          G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR,
                          g_log_default_handler, NULL);
    }

    /*
     * Now create a vhost-user socket that we will receive messages
     * on. Once we have our handler set up we can enter the glib main
     * loop.
     */
    if (socket_path) {
        g_autoptr(GSocketAddress) addr = g_unix_socket_address_new(socket_path);
        g_autoptr(GSocket) bind_socket = g_socket_new(G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM,
                                                      G_SOCKET_PROTOCOL_DEFAULT, &error);

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

    /*
     * Create the main loop first so all the various sources can be
     * added. As well as catching signals we need to ensure vug_init
     * can add it's GSource watches.
     */

    rpmb.loop = g_main_loop_new(NULL, FALSE);
    /* catch exit signals */
    g_unix_signal_add(SIGHUP, hangup, rpmb.loop);
    g_unix_signal_add(SIGINT, hangup, rpmb.loop);

    if (!vug_init(&rpmb.dev, VHOST_USER_RPMB_MAX_QUEUES, g_socket_get_fd(socket),
                  vrpmb_panic, &vuiface)) {
        g_printerr("Failed to initialize libvhost-user-glib.\n");
        exit(EXIT_FAILURE);
    }


    g_message("entering main loop, awaiting messages");
    g_main_loop_run(rpmb.loop);
    g_message("finished main loop, cleaning up");

    g_main_loop_unref(rpmb.loop);
    vrpmb_destroy(&rpmb);
}
