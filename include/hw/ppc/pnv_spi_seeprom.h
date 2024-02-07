/*
 * QEMU PowerPC SPI SEEPROM model
 *
 * Copyright (c) 2024, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This model implements a Serial EEPROM utilizing the Serial Peripheral
 * Interface (SPI) compatible bus.
 * Currently supported variants: 25CSM04.
 * The Microchip Technology Inc. 25CSM04 provides 4 Mbits of Serial EEPROM
 * utilizing the Serial Peripheral Interface (SPI) compatible bus. The device
 * is organized as 524288 bytes of 8 bits each (512Kbyte) and is optimized
 * for use in consumer and industrial applications where reliable and
 * dependable nonvolatile memory storage is essential
 */

#ifndef PPC_PNV_SPI_SEEPROM_H
#define PPC_PNV_SPI_SEEPROM_H

#include "hw/ppc/pnv_spi_responder.h"
#include "qom/object.h"

#define TYPE_PNV_SPI_SEEPROM "pnv-spi-seeprom"

OBJECT_DECLARE_SIMPLE_TYPE(PnvSpiSeeprom, PNV_SPI_SEEPROM)

typedef struct xfer_buffer xfer_buffer;

typedef struct PnvSpiSeeprom {
    PnvSpiResponder resp;

    char            *file; /* SEEPROM image file */
    uint8_t         opcode; /* SEEPROM Opcode */
    uint32_t        addr; /* SEEPROM Command Address */
    uint8_t         rd_state; /* READ State Machine state variable */
    bool            locked; /* Security Register Locked */
    bool            controller_connected; /* Flag for master connection */
    /*
     * Device registers
     * The 25CSM04 contains four types of registers that modulate device
     * operation and/or report on the current status of the device. These
     * registers are:
     * STATUS register
     * Security register
     * Memory Partition registers (eight total)
     * Identification register
     */
    uint8_t         status0;
    uint8_t         status1;
    /*
     * The Security register is split into
     * 1. user-programmable lockable ID page section.
     * 2. The read-only section contains a preprogrammed, globally unique,
     *    128-bit serial number.
     */
    uint8_t         uplid[256];
    uint8_t         dsn[16];
    uint8_t         mpr[8];
    uint8_t         idr[5];
} PnvSpiSeeprom;

xfer_buffer *seeprom_spi_request(PnvSpiResponder *resp, int first, int last,
                int bits, xfer_buffer *payload);
void seeprom_connect_controller(PnvSpiResponder *resp, const char *port);
void seeprom_disconnect_controller(PnvSpiResponder *resp);
bool compute_addr(PnvSpiSeeprom *spi_resp, xfer_buffer *req_payload,
                   xfer_buffer *rsp_payload, int bits, uint32_t *data_offset);
bool validate_addr(PnvSpiSeeprom *spi_resp);
#endif /* PPC_PNV_SPI_SEEPROM_H */
