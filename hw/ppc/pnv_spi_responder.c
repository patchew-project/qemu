/*
 * QEMU PowerPC SPI Responder
 *
 * Copyright (c) 2024, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/ppc/pnv_spi_responder.h"
#include "qapi/error.h"

static const TypeInfo spi_bus_info = {
    .name = TYPE_SPI_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(SpiBus),
};

SpiBus *spi_create_bus(DeviceState *parent, const char *name)
{
    BusState *bus;
    bus = qbus_new(TYPE_SPI_BUS, parent, name);
    return SPI_BUS(bus);
}

/* xfer_buffer_methods */
xfer_buffer *xfer_buffer_new(void)
{
    xfer_buffer *payload = g_malloc0(sizeof(*payload));
    payload->data = g_malloc0(payload->len * sizeof(uint8_t));
    return payload;
}

void xfer_buffer_free(xfer_buffer *payload)
{
    free(payload->data);
    payload->data = NULL;
    free(payload);
}

uint8_t *xfer_buffer_write_ptr(xfer_buffer *payload, uint32_t offset,
                            uint32_t length)
{
    if (payload->len < (offset + length)) {
        payload->len = offset + length;
        payload->data = g_realloc(payload->data,
                        payload->len * sizeof(uint8_t));
    }
    return &payload->data[offset];
}

void xfer_buffer_read_ptr(xfer_buffer *payload, uint8_t **read_buf,
                uint32_t offset, uint32_t length)
{
    static uint32_t prev_len;
    uint32_t offset_org = offset;
    if (offset > payload->len) {
        if (length < payload->len) {
            offset = payload->len - length;
        } else {
            offset = 0;
            length = payload->len;
        }
        qemu_log_mask(LOG_GUEST_ERROR, "Read offset(%d) exceeds buffer "
                        "length(%d), altered offset to %d and length to %d to "
                        "read within buffer\n", offset_org, payload->len,
                        offset, length);
    } else if ((offset + length) > payload->len) {
        qemu_log_mask(LOG_GUEST_ERROR, "Read length(%d) bytes from offset (%d)"
                        ", exceeds buffer length(%d)\n", length, offset,
                        payload->len);
        length = payload->len - offset;
    }

    if ((prev_len != length) || (*read_buf == NULL)) {
        *read_buf = g_realloc(*read_buf, length * sizeof(uint8_t));
        prev_len = length;
    }
    *read_buf = &payload->data[offset];
}

/* Controller interface methods */
bool spi_connect_controller(SpiBus *bus, const char *port)
{
    BusState *b = BUS(bus);
    BusChild *kid;
    QTAILQ_FOREACH(kid, &b->children, sibling) {
        PnvSpiResponder *r = PNV_SPI_RESPONDER(kid->child);
        PnvSpiResponderClass *rc = PNV_SPI_RESPONDER_GET_CLASS(r);
        rc->connect_controller(r, port);
        return true;
    }
    return false;
}
bool spi_disconnect_controller(SpiBus *bus)
{
    BusState *b = BUS(bus);
    BusChild *kid;
    QTAILQ_FOREACH(kid, &b->children, sibling) {
        PnvSpiResponder *r = PNV_SPI_RESPONDER(kid->child);
        PnvSpiResponderClass *rc = PNV_SPI_RESPONDER_GET_CLASS(r);
        rc->disconnect_controller(r);
        return true;
    }
    return false;
}

xfer_buffer *spi_request(SpiBus *bus,
                int first, int last, int bits, xfer_buffer *payload)
{
    BusState *b = BUS(bus);
    BusChild *kid;
    xfer_buffer *rsp_payload = NULL;
    uint8_t *buf = NULL;

    QTAILQ_FOREACH(kid, &b->children, sibling) {
        PnvSpiResponder *r = PNV_SPI_RESPONDER(kid->child);
        PnvSpiResponderClass *rc = PNV_SPI_RESPONDER_GET_CLASS(r);
        rsp_payload = rc->request(r, first, last, bits, payload);
        return rsp_payload;
    }
    if (rsp_payload == NULL) {
        rsp_payload = xfer_buffer_new();
    }
    buf = xfer_buffer_write_ptr(rsp_payload, 0, payload->len);
    memset(buf, 0, payload->len);
    return rsp_payload;
}

/* create and realise spi responder device */
bool spi_realize_and_unref(DeviceState *dev, SpiBus *bus, Error **errp)
{
    return qdev_realize_and_unref(dev, &bus->parent_obj, errp);
}

PnvSpiResponder *spi_create_responder(SpiBus *bus, const char *name)
{
    DeviceState *dev = qdev_new(name);

    spi_realize_and_unref(dev, bus, &error_fatal);
    return PNV_SPI_RESPONDER(dev);
}

static void pnv_spi_responder_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "PowerNV SPI RESPONDER";
}

static const TypeInfo pnv_spi_responder_info = {
    .name          = TYPE_PNV_SPI_RESPONDER,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(PnvSpiResponder),
    .class_init    = pnv_spi_responder_class_init,
    .abstract      = true,
    .class_size    = sizeof(PnvSpiResponderClass),
};

static void pnv_spi_responder_register_types(void)
{
    type_register_static(&pnv_spi_responder_info);
    type_register_static(&spi_bus_info);
}

type_init(pnv_spi_responder_register_types);
