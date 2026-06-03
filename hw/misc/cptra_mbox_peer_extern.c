/*
 * Caliptra mailbox external peer (backend).
 *
 * Copyright (C) 2026 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "block/thread-pool.h"
#include "chardev/char-fe.h"
#include "hw/misc/aspeed_cptra_mbox.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "migration/blocker.h"
#include "qapi/error.h"
#include "qemu/bswap.h"
#include "qemu/error-report.h"

#define CPTRA_MBOX_PROTO_MAGIC      0x4D424F58u   /* "MBOX" */
#define CPTRA_MBOX_PROTO_VERSION    1u
#define CPTRA_MBOX_CMD_EXECUTE      1u
#define CPTRA_MBOX_CMD_RESPONSE     2u
#define CPTRA_MBOX_PROTO_MAX_PAYLOAD (8 + CPTRA_MBOX0_SRAM_SIZE)

typedef struct QEMU_PACKED CptraMboxProtoHdr {
    uint32_t magic;
    uint16_t version;
    uint16_t command;
    uint32_t payload_len;
} CptraMboxProtoHdr;

OBJECT_DECLARE_SIMPLE_TYPE(CptraMboxPeerExtern, CPTRA_MBOX_PEER_EXTERN)

struct CptraMboxPeerExtern {
    CptraMboxPeer parent;

    CharFrontend chr;
    Error *migration_blocker;
};

typedef struct CptraMboxExternReq {
    CptraMboxPeerExtern *p;

    uint32_t cmd;
    uint32_t dlen;
    uint8_t *req_data;
    size_t req_len;

    uint32_t rsp_status;
    uint32_t rsp_dlen;
    uint8_t *rsp_data;
    size_t rsp_len;

    char *error;
} CptraMboxExternReq;

static size_t cptra_padded_len(uint32_t dlen)
{
    return (size_t)((dlen + 3) / 4) * 4;
}

static int G_GNUC_PRINTF(2, 3)
cptra_req_fail(CptraMboxExternReq *req, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    g_free(req->error);
    req->error = g_strdup_vprintf(fmt, ap);
    va_end(ap);

    return -1;
}

static int cptra_write_all(CptraMboxExternReq *req, const void *buf, size_t len)
{
    int ret;

    if (len > INT_MAX) {
        return cptra_req_fail(req, "write too large: %zu", len);
    }
    ret = qemu_chr_fe_write_all(&req->p->chr, buf, len);
    if (ret < 0 || (size_t)ret != len) {
        return cptra_req_fail(req, "backend write failed");
    }
    return 0;
}

static int cptra_read_all(CptraMboxExternReq *req, void *buf, size_t len)
{
    int ret;

    if (len > INT_MAX) {
        return cptra_req_fail(req, "read too large: %zu", len);
    }
    ret = qemu_chr_fe_read_all(&req->p->chr, buf, len);
    if (ret < 0 || (size_t)ret != len) {
        return cptra_req_fail(req, "backend read failed");
    }
    return 0;
}

/*
 * Wire protocol (magic 0x4D424F58 "MBOX"):
 *
 *   Header (12 bytes, little-endian):
 *     u32 magic       = 0x4D424F58
 *     u16 version     = 1
 *     u16 command
 *     u32 payload_len
 *
 *   MBOX_EXECUTE (1)  QEMU -> backend
 *     payload: u32 cmd, u32 dlen, u8 sram[ROUND_UP(dlen, 4)]
 *
 *   MBOX_RESPONSE (2) backend -> QEMU
 *     payload: u32 status, u32 dlen, u8 sram[ROUND_UP(dlen, 4)]
 */
static int cptra_extern_worker(gpointer data)
{
    CptraMboxExternReq *req = data;
    CptraMboxProtoHdr hdr;
    size_t tx_payload_len = 8 + req->req_len;
    g_autofree uint8_t *payload = NULL;
    size_t rsp_data_len;

    if (!qemu_chr_fe_backend_open(&req->p->chr)) {
        return cptra_req_fail(req, "backend is not connected");
    }

    payload = g_malloc0(tx_payload_len);
    stl_le_p(payload, req->cmd);
    stl_le_p(payload + 4, req->dlen);
    if (req->req_len) {
        memcpy(payload + 8, req->req_data, req->req_len);
    }

    hdr.magic = cpu_to_le32(CPTRA_MBOX_PROTO_MAGIC);
    hdr.version = cpu_to_le16(CPTRA_MBOX_PROTO_VERSION);
    hdr.command = cpu_to_le16(CPTRA_MBOX_CMD_EXECUTE);
    hdr.payload_len = cpu_to_le32(tx_payload_len);

    if (cptra_write_all(req, &hdr, sizeof(hdr)) < 0 ||
        cptra_write_all(req, payload, tx_payload_len) < 0) {
        return -1;
    }

    if (cptra_read_all(req, &hdr, sizeof(hdr)) < 0) {
        return -1;
    }

    hdr.magic = le32_to_cpu(hdr.magic);
    hdr.version = le16_to_cpu(hdr.version);
    hdr.command = le16_to_cpu(hdr.command);
    hdr.payload_len = le32_to_cpu(hdr.payload_len);

    if (hdr.magic != CPTRA_MBOX_PROTO_MAGIC) {
        return cptra_req_fail(req, "bad response magic 0x%08x", hdr.magic);
    }
    if (hdr.version != CPTRA_MBOX_PROTO_VERSION) {
        return cptra_req_fail(req, "bad response version %u", hdr.version);
    }
    if (hdr.command != CPTRA_MBOX_CMD_RESPONSE) {
        return cptra_req_fail(req, "unexpected response command %u",
                              hdr.command);
    }
    if (hdr.payload_len < 8 || hdr.payload_len > CPTRA_MBOX_PROTO_MAX_PAYLOAD) {
        return cptra_req_fail(req, "invalid response payload length %u",
                              hdr.payload_len);
    }

    g_free(payload);
    payload = g_malloc(hdr.payload_len);
    if (cptra_read_all(req, payload, hdr.payload_len) < 0) {
        return -1;
    }

    req->rsp_status = ldl_le_p(payload);
    req->rsp_dlen = ldl_le_p(payload + 4);
    if (req->rsp_dlen > CPTRA_MBOX0_SRAM_SIZE) {
        return cptra_req_fail(req, "response DLEN 0x%x exceeds SRAM",
                              req->rsp_dlen);
    }

    rsp_data_len = cptra_padded_len(req->rsp_dlen);
    if (8 + rsp_data_len > hdr.payload_len) {
        return cptra_req_fail(req, "short response payload for DLEN 0x%x",
                              req->rsp_dlen);
    }

    if (rsp_data_len) {
        req->rsp_data = g_malloc0(rsp_data_len);
        memcpy(req->rsp_data, payload + 8, rsp_data_len);
        req->rsp_len = rsp_data_len;
    }

    return 0;
}

static void cptra_extern_req_free(CptraMboxExternReq *req)
{
    g_free(req->req_data);
    g_free(req->rsp_data);
    g_free(req->error);
    g_free(req);
}

static void cptra_extern_complete(void *opaque, int ret)
{
    CptraMboxExternReq *req = opaque;
    CptraMboxPeerExtern *p = req->p;
    CptraMboxIf *intf = p->parent.intf;
    CptraMboxIfClass *ic = intf ? CPTRA_MBOX_IF_GET_CLASS(intf) : NULL;

    if (req->error) {
        error_report("cptra-mbox-peer-extern: %s", req->error);
    }

    if (ic) {
        if (ret == 0) {
            ic->complete(intf, req->rsp_status, req->rsp_dlen,
                         req->rsp_data, req->rsp_len);
        } else {
            ic->complete(intf, CPTRA_MBOX0_STATUS_CMD_FAILURE, 0, NULL, 0);
        }
    }

    cptra_extern_req_free(req);
    object_unref(OBJECT(p));
}

static void cptra_extern_handle_execute(CptraMboxPeer *peer, uint32_t cmd,
                                        uint32_t dlen, const uint8_t *data,
                                        uint32_t len)
{
    CptraMboxPeerExtern *p = CPTRA_MBOX_PEER_EXTERN(peer);
    CptraMboxIf *intf = peer->intf;
    CptraMboxExternReq *req;

    if (!qemu_chr_fe_backend_connected(&p->chr) ||
        !qemu_chr_fe_backend_open(&p->chr)) {
        if (intf) {
            CPTRA_MBOX_IF_GET_CLASS(intf)->complete(
                intf, CPTRA_MBOX0_STATUS_CMD_FAILURE, 0, NULL, 0);
        }
        return;
    }

    req = g_new0(CptraMboxExternReq, 1);
    req->p = p;
    req->cmd = cmd;
    req->dlen = dlen;
    if (len) {
        req->req_data = g_memdup2(data, len);
        req->req_len = len;
    }

    object_ref(OBJECT(p));
    thread_pool_submit_aio(cptra_extern_worker, req,
                           cptra_extern_complete, req);
}

static void cptra_extern_realize(DeviceState *dev, Error **errp)
{
    CptraMboxPeerExtern *p = CPTRA_MBOX_PEER_EXTERN(dev);

    if (!qemu_chr_fe_backend_connected(&p->chr)) {
        error_setg(errp, "cptra-mbox-peer-extern requires a chardev attribute");
        return;
    }

    qemu_chr_fe_set_open(&p->chr, true);
    error_setg(&p->migration_blocker,
               "Migration disabled: cptra-mbox-peer-extern chardev backend "
               "state is external");
    if (migrate_add_blocker(&p->migration_blocker, errp) < 0) {
        return;
    }
}

static void cptra_extern_finalize(Object *obj)
{
    CptraMboxPeerExtern *p = CPTRA_MBOX_PEER_EXTERN(obj);

    migrate_del_blocker(&p->migration_blocker);
    qemu_chr_fe_deinit(&p->chr, false);
}

static const Property cptra_extern_props[] = {
    DEFINE_PROP_CHR("chardev", CptraMboxPeerExtern, chr),
};

static void cptra_extern_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    CptraMboxPeerClass *pc = CPTRA_MBOX_PEER_CLASS(oc);

    dc->desc = "Caliptra mailbox external (chardev) peer";
    dc->realize = cptra_extern_realize;
    dc->hotpluggable = false;
    device_class_set_props(dc, cptra_extern_props);
    pc->handle_execute = cptra_extern_handle_execute;
}

static const TypeInfo cptra_extern_type = {
    .name              = TYPE_CPTRA_MBOX_PEER_EXTERN,
    .parent            = TYPE_CPTRA_MBOX_PEER,
    .instance_size     = sizeof(CptraMboxPeerExtern),
    .instance_finalize = cptra_extern_finalize,
    .class_init        = cptra_extern_class_init,
};

static void cptra_extern_register_types(void)
{
    type_register_static(&cptra_extern_type);
}

type_init(cptra_extern_register_types)
