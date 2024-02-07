/*
 * QEMU SPI Responder.
 *
 * Copyright (c) 2024, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * SPI provides full-duplex synchronous serial communication between single
 * controller and multiple responder devices. One SPI Controller is
 * implemented and supported on a SPI Bus, there is no support for multiple
 * controllers on the SPI bus.
 *
 * The current implementation assumes that single responder is connected to
 * bus, hence chip_select is not modelled.
 */

#ifndef PPC_PNV_SPI_RESPONDER_H
#define PPC_PNV_SPI_RESPONDER_H

#include "hw/qdev-core.h"
#include "qom/object.h"
#include "qemu/log.h"

#define TYPE_PNV_SPI_RESPONDER "spi-responder"
OBJECT_DECLARE_TYPE(PnvSpiResponder, PnvSpiResponderClass,
                    PNV_SPI_RESPONDER)

typedef struct xfer_buffer xfer_buffer;

struct PnvSpiResponderClass {
    DeviceClass parent_class;

    /*
     * These methods are from controller to responder and implemented
     * by all responders.
     * Connect_controller/disconnect_controller methods are called by
     * controller to initiate/stop the SPI transfer.
     */
    void (*connect_controller)(PnvSpiResponder *responder, const char *port);
    void (*disconnect_controller)(PnvSpiResponder *responder);
    /*
     * SPI transfer consists of a number of consecutive calls to the request
     * method.
     * The parameter first is true on first call of the transfer and last is on
     * the final call of the transfer. Parameter bits and payload defines the
     * number of bits and data payload sent by controller.
     * Responder returns the response payload.
     */
    xfer_buffer *(*request)(PnvSpiResponder *responder, int first, int last,
                    int bits, xfer_buffer *payload);
};

struct PnvSpiResponder {
    DeviceState parent_obj;
};

#define TYPE_SPI_BUS "spi-bus"
OBJECT_DECLARE_SIMPLE_TYPE(SpiBus, SPI_BUS)

struct SpiBus {
    BusState parent_obj;
};

/*
 * spi_realize_and_unref: realize and unref an SPI responder
 * @dev: SPI responder to realize
 * @bus: SPI bus to put it on
 * @errp: error pointer
 */
bool spi_realize_and_unref(DeviceState *dev, SpiBus *bus, Error **errp);

/*
 * spi_create_responder: create a SPI responder.
 * @bus: SPI bus to put it on
 * @name: name of the responder object.
 * call spi_realize_and_unref() after creating the responder.
 */

PnvSpiResponder *spi_create_responder(SpiBus *bus, const char *name);

/* xfer_buffer */
typedef struct xfer_buffer {

    uint32_t    len;
    uint8_t    *data;

} xfer_buffer;

/*
 * xfer_buffer_read_ptr: Increment the payload length and return the pointer
 * to the data at offset
 */
uint8_t *xfer_buffer_write_ptr(xfer_buffer *payload, uint32_t offset,
                uint32_t length);
/* xfer_buffer_read_ptr: Return the pointer to the data at offset */
void xfer_buffer_read_ptr(xfer_buffer *payload, uint8_t **read_buf,
                uint32_t offset, uint32_t length);
/* xfer_buffer_new: Allocate memory and return the pointer */
xfer_buffer *xfer_buffer_new(void);
/* xfer_buffer_free: free the payload */
void xfer_buffer_free(xfer_buffer *payload);

/* Controller interface */
SpiBus *spi_create_bus(DeviceState *parent, const char *name);
xfer_buffer *spi_request(SpiBus *bus, int first, int last, int bits,
                xfer_buffer *payload);
bool spi_connect_controller(SpiBus *bus, const char *port);
bool spi_disconnect_controller(SpiBus *bus);
#endif /* PPC_PNV_SPI_SEEPROM_H */
