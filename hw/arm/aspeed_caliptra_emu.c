/*
 * ASPEED Caliptra external backend (PoC)
 *
 * Forwards CA35 MMIO accesses on the Caliptra APB window to an external
 * caliptra-server process over a Unix socket.  caliptra-server is expected
 * to be started independently with the Caliptra ROM and firmware bundle
 * and to be listening on socket-path before QEMU is launched.
 *
 * Copyright (C) 2026 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/arm/aspeed_caliptra_emu.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"
#include "io/channel.h"
#include "io/channel-socket.h"
#include "qapi/error.h"
#include "qemu/bswap.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/units.h"
#include "system/memory.h"

/*
 * Wire protocol (little-endian).  Symmetric with caliptra-server.
 *
 *   Header (12 bytes):
 *     u32 magic        -- 0x43505452 ("CPTR")
 *     u16 version      -- 1
 *     u16 command
 *     u32 payload_len
 *
 *   Commands (after the connection is established):
 *     APB_READ   QEMU -> server   {u32 apb_addr}
 *     APB_RDATA  server -> QEMU   {u32 data}
 *     APB_WRITE  QEMU -> server   {u32 apb_addr, u32 data}
 *     APB_WACK   server -> QEMU   {} (empty payload)
 *
 * There is no boot handshake: caliptra-server boots its emulated model to
 * runtime-ready before listen()/accept(), and QEMU just connects and starts
 * forwarding APB traffic.
 */
#define CALIPTRA_SOCKET_MAGIC          0x43505452
#define CALIPTRA_SOCKET_VERSION        1
#define CALIPTRA_SOCKET_CMD_APB_READ   3
#define CALIPTRA_SOCKET_CMD_APB_RDATA  4
#define CALIPTRA_SOCKET_CMD_APB_WRITE  5
#define CALIPTRA_SOCKET_CMD_APB_WACK   6
#define CALIPTRA_SOCKET_MAX_PAYLOAD    (4 * MiB)

/*
 * Base address of the Caliptra APB register space as seen by caliptra-hw-model.
 * Mailbox CSR starts at 0x30020000; SOC IFC starts at 0x30030000.
 * Total mapped size: 0x20000 (128 KiB).
 *
 * MMIO offset from ASPEED_CALIPTRA_MMIO_BASE in the CA35 address space is
 * mapped 1:1 to APB address CALIPTRA_APB_BASE + offset.
 */
#define CALIPTRA_APB_BASE  0x30020000
#define CALIPTRA_MMIO_SIZE 0x20000

typedef struct QEMU_PACKED CaliptraSocketHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t command;
    uint32_t payload_len;
} CaliptraSocketHeader;

OBJECT_DECLARE_SIMPLE_TYPE(AspeedCaliptraEmuState, ASPEED_CALIPTRA_EMU)

struct AspeedCaliptraEmuState {
    SysBusDevice parent_obj;
    char *socket_path;
    QIOChannelSocket *sioc;
    QemuMutex ioc_mutex;
    MemoryRegion mmio;
};

static bool caliptra_socket_write_msg(QIOChannel *ioc, uint16_t command,
                                      const uint8_t *payload, size_t len,
                                      Error **errp)
{
    CaliptraSocketHeader hdr = {
        .magic = cpu_to_le32(CALIPTRA_SOCKET_MAGIC),
        .version = cpu_to_le16(CALIPTRA_SOCKET_VERSION),
        .command = cpu_to_le16(command),
        .payload_len = cpu_to_le32(len),
    };

    if (qio_channel_write_all(ioc, (const char *)&hdr, sizeof(hdr), errp) < 0) {
        return false;
    }
    if (len &&
        qio_channel_write_all(ioc, (const char *)payload, len, errp) < 0) {
        return false;
    }
    return true;
}

static bool caliptra_socket_read_header(QIOChannel *ioc, uint16_t command,
                                        CaliptraSocketHeader *hdr,
                                        Error **errp)
{
    if (qio_channel_read_all(ioc, (char *)hdr, sizeof(*hdr), errp) < 0) {
        return false;
    }

    hdr->magic = le32_to_cpu(hdr->magic);
    hdr->version = le16_to_cpu(hdr->version);
    hdr->command = le16_to_cpu(hdr->command);
    hdr->payload_len = le32_to_cpu(hdr->payload_len);

    if (hdr->magic != CALIPTRA_SOCKET_MAGIC) {
        error_setg(errp, "Invalid Caliptra socket magic 0x%08x", hdr->magic);
        return false;
    }
    if (hdr->version != CALIPTRA_SOCKET_VERSION) {
        error_setg(errp, "Unsupported Caliptra socket version %u",
                   hdr->version);
        return false;
    }
    if (hdr->command != command) {
        error_setg(errp, "Unexpected Caliptra socket command %u (expected %u)",
                   hdr->command, command);
        return false;
    }
    if (hdr->payload_len > CALIPTRA_SOCKET_MAX_PAYLOAD) {
        error_setg(errp, "Caliptra socket response too large: %u bytes",
                   hdr->payload_len);
        return false;
    }
    return true;
}

static bool caliptra_apb_send_recv(AspeedCaliptraEmuState *s,
                                   uint16_t req_cmd, uint16_t rsp_cmd,
                                   const uint8_t *req_payload, size_t req_len,
                                   uint8_t *rsp_buf, size_t rsp_len,
                                   Error **errp)
{
    CaliptraSocketHeader hdr;
    QIOChannel *ioc;

    if (!s->sioc) {
        error_setg(errp, "Caliptra APB access with no backend connection");
        return false;
    }
    ioc = QIO_CHANNEL(s->sioc);

    if (!caliptra_socket_write_msg(ioc, req_cmd, req_payload, req_len, errp)) {
        return false;
    }
    if (!caliptra_socket_read_header(ioc, rsp_cmd, &hdr, errp)) {
        return false;
    }

    if (rsp_len) {
        if (hdr.payload_len < rsp_len) {
            error_setg(errp,
                       "Caliptra short response for cmd %u: got %u, expected %zu",
                       rsp_cmd, hdr.payload_len, rsp_len);
            return false;
        }
        if (qio_channel_read_all(ioc, (char *)rsp_buf, rsp_len, errp) < 0) {
            return false;
        }
        /* Discard any trailing bytes. */
        if (hdr.payload_len > rsp_len) {
            g_autofree uint8_t *extra = g_malloc(hdr.payload_len - rsp_len);
            if (qio_channel_read_all(ioc, (char *)extra,
                                     hdr.payload_len - rsp_len, errp) < 0) {
                return false;
            }
        }
    } else if (hdr.payload_len) {
        g_autofree uint8_t *extra = g_malloc(hdr.payload_len);
        if (qio_channel_read_all(ioc, (char *)extra,
                                 hdr.payload_len, errp) < 0) {
            return false;
        }
    }
    return true;
}

static uint64_t caliptra_mmio_read(void *opaque, hwaddr offset, unsigned size)
{
    AspeedCaliptraEmuState *s = opaque;
    uint8_t req[4], rsp[4] = {};
    Error *err = NULL;

    stl_le_p(req, CALIPTRA_APB_BASE + (uint32_t)offset);

    /*
     * Release the BQL before blocking on the socket so the QEMU event loop
     * stays responsive while caliptra-server processes the request.
     */
    bql_unlock();
    qemu_mutex_lock(&s->ioc_mutex);
    caliptra_apb_send_recv(s,
                           CALIPTRA_SOCKET_CMD_APB_READ,
                           CALIPTRA_SOCKET_CMD_APB_RDATA,
                           req, sizeof(req),
                           rsp, sizeof(rsp), &err);
    qemu_mutex_unlock(&s->ioc_mutex);
    bql_lock();

    if (err) {
        error_report("caliptra APB read 0x%08" PRIx32 ": %s",
                     CALIPTRA_APB_BASE + (uint32_t)offset,
                     error_get_pretty(err));
        error_free(err);
        return 0xdeadbeef;
    }
    return ldl_le_p(rsp);
}

static void caliptra_mmio_write(void *opaque, hwaddr offset,
                                uint64_t value, unsigned size)
{
    AspeedCaliptraEmuState *s = opaque;
    uint8_t req[8];
    Error *err = NULL;

    stl_le_p(req + 0, CALIPTRA_APB_BASE + (uint32_t)offset);
    stl_le_p(req + 4, (uint32_t)value);

    bql_unlock();
    qemu_mutex_lock(&s->ioc_mutex);
    caliptra_apb_send_recv(s,
                           CALIPTRA_SOCKET_CMD_APB_WRITE,
                           CALIPTRA_SOCKET_CMD_APB_WACK,
                           req, sizeof(req),
                           NULL, 0, &err);
    qemu_mutex_unlock(&s->ioc_mutex);
    bql_lock();

    if (err) {
        error_report("caliptra APB write 0x%08" PRIx32 " = 0x%08" PRIx32 ": %s",
                     CALIPTRA_APB_BASE + (uint32_t)offset, (uint32_t)value,
                     error_get_pretty(err));
        error_free(err);
    }
}

static const MemoryRegionOps caliptra_mmio_ops = {
    .read  = caliptra_mmio_read,
    .write = caliptra_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void aspeed_caliptra_emu_realize(DeviceState *dev, Error **errp)
{
    AspeedCaliptraEmuState *s = ASPEED_CALIPTRA_EMU(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    SocketAddress addr;
    QIOChannelSocket *sioc;

    if (!s->socket_path || !s->socket_path[0]) {
        error_setg(errp, "caliptra-emu: socket-path must be set");
        return;
    }

    memory_region_init_io(&s->mmio, OBJECT(s), &caliptra_mmio_ops, s,
                          "caliptra-apb", CALIPTRA_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);

    /*
     * Connect to caliptra-server synchronously.  The server is expected to be
     * already listening with the Caliptra ROM/firmware booted to runtime-ready.
     */
    memset(&addr, 0, sizeof(addr));
    addr.type = SOCKET_ADDRESS_TYPE_UNIX;
    addr.u.q_unix.path = s->socket_path;

    sioc = qio_channel_socket_new();
    if (qio_channel_socket_connect_sync(sioc, &addr, errp) < 0) {
        object_unref(OBJECT(sioc));
        return;
    }
    if (!qio_channel_set_blocking(QIO_CHANNEL(sioc), true, errp)) {
        object_unref(OBJECT(sioc));
        return;
    }
    s->sioc = sioc;
}

static void aspeed_caliptra_emu_instance_init(Object *obj)
{
    AspeedCaliptraEmuState *s = ASPEED_CALIPTRA_EMU(obj);

    qemu_mutex_init(&s->ioc_mutex);
}

static void aspeed_caliptra_emu_finalize(Object *obj)
{
    AspeedCaliptraEmuState *s = ASPEED_CALIPTRA_EMU(obj);

    if (s->sioc) {
        object_unref(OBJECT(s->sioc));
    }
    qemu_mutex_destroy(&s->ioc_mutex);
}

static const Property aspeed_caliptra_emu_props[] = {
    DEFINE_PROP_STRING("socket-path", AspeedCaliptraEmuState, socket_path),
};

static void aspeed_caliptra_emu_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = aspeed_caliptra_emu_realize;
    dc->desc    = "ASPEED Caliptra external backend (PoC)";
    device_class_set_props(dc, aspeed_caliptra_emu_props);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo aspeed_caliptra_emu_type_info = {
    .name              = TYPE_ASPEED_CALIPTRA_EMU,
    .parent            = TYPE_SYS_BUS_DEVICE,
    .instance_size     = sizeof(AspeedCaliptraEmuState),
    .instance_init     = aspeed_caliptra_emu_instance_init,
    .instance_finalize = aspeed_caliptra_emu_finalize,
    .class_init        = aspeed_caliptra_emu_class_init,
};

static void aspeed_caliptra_emu_register_type(void)
{
    type_register_static(&aspeed_caliptra_emu_type_info);
}
type_init(aspeed_caliptra_emu_register_type)
