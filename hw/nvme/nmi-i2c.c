/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * SPDX-FileCopyrightText: Copyright (c) 2022 Samsung Electronics Co., Ltd.
 *
 * SPDX-FileContributor: Padmakar Kalghatgi <p.kalghatgi@samsung.com>
 * SPDX-FileContributor: Arun Kumar Agasar <arun.kka@samsung.com>
 * SPDX-FileContributor: Saurav Kumar <saurav.29@partner.samsung.com>
 * SPDX-FileContributor: Klaus Jensen <k.jensen@samsung.com>
 */

#include "qemu/osdep.h"
#include "qemu/crc32c.h"
#include "hw/i2c/i2c.h"
#include "hw/registerfields.h"
#include "hw/i2c/mctp.h"
#include "trace.h"

#define NMI_MAX_MESSAGE_LENGTH 4224

#define TYPE_NMI_I2C_DEVICE "nmi-i2c"
OBJECT_DECLARE_SIMPLE_TYPE(NMIDevice, NMI_I2C_DEVICE)

typedef struct NMIDevice {
    MCTPI2CEndpoint mctp;

    uint8_t buffer[NMI_MAX_MESSAGE_LENGTH];
    uint8_t scratch[NMI_MAX_MESSAGE_LENGTH];

    size_t  len;
    int64_t pos;
} NMIDevice;

FIELD(NMI_NMP, ROR, 7, 1)
FIELD(NMI_NMP, NMIMT, 3, 4)

#define NMI_NMP_NMIMT_NMI_CMD 0x1
#define NMI_NMP_NMIMT_NM_ADMIN 0x2

typedef struct NMIMessage {
    uint8_t mctpd;
    uint8_t nmp;
    uint8_t rsvd2[2];
    uint8_t payload[]; /* includes the Message Integrity Check */
} NMIMessage;

typedef struct NMIRequest {
   uint8_t opc;
   uint8_t rsvd1[3];
   uint32_t dw0;
   uint32_t dw1;
   uint32_t mic;
} NMIRequest;

typedef struct NMIResponse {
    uint8_t status;
    uint8_t response[3];
    uint8_t payload[]; /* includes the Message Integrity Check */
} NMIResponse;

typedef enum NMIReadDSType {
    NMI_CMD_READ_NMI_DS_SUBSYSTEM       = 0x0,
    NMI_CMD_READ_NMI_DS_PORTS           = 0x1,
    NMI_CMD_READ_NMI_DS_CTRL_LIST       = 0x2,
    NMI_CMD_READ_NMI_DS_CTRL_INFO       = 0x3,
    NMI_CMD_READ_NMI_DS_CMD_SUPPORT     = 0x4,
    NMI_CMD_READ_NMI_DS_MEB_CMD_SUPPORT = 0x5,
} NMIReadDSType;

static void nmi_handle_mi_read_nmi_ds(NMIDevice *nmi, NMIRequest *request)
{
    I2CSlave *i2c = I2C_SLAVE(nmi);

    uint32_t dw0 = le32_to_cpu(request->dw0);
    uint8_t dtyp = (dw0 >> 24) & 0xf;
    uint8_t *buf;
    size_t len;

    trace_nmi_handle_mi_read_nmi_ds(dtyp);

    static uint8_t nmi_ds_subsystem[36] = {
        0x00,       /* success */
        0x20,       /* response data length */
        0x00, 0x00, /* reserved */
        0x00,       /* number of ports */
        0x01,       /* major version */
        0x01,       /* minor version */
    };

    static uint8_t nmi_ds_ports[36] = {
        0x00,       /* success */
        0x20,       /* response data length */
        0x00, 0x00, /* reserved */
        0x02,       /* port type (smbus) */
        0x00,       /* reserved */
        0x40, 0x00, /* maximum mctp transission unit size (64 bytes) */
        0x00, 0x00, 0x00, 0x00, /* management endpoint buffer size */
        0x00, 0x00, /* vpd i2c address/freq */
        0x00, 0x01, /* management endpoint i2c address/freq */
    };

    static uint8_t nmi_ds_error[4] = {
        0x04,       /* invalid parameter */
        0x00,       /* first invalid bit position */
        0x00, 0x00, /* first invalid byte position */
    };

    static uint8_t nmi_ds_empty[8] = {
        0x00,       /* success */
        0x02,       /* response data length */
        0x00, 0x00, /* reserved */
        0x00, 0x00, /* number of controllers */
        0x00, 0x00, /* padding */
    };

    switch (dtyp) {
    case NMI_CMD_READ_NMI_DS_SUBSYSTEM:
        len = 36;
        buf = nmi_ds_subsystem;

        break;

    case NMI_CMD_READ_NMI_DS_PORTS:
        len = 36;
        buf = nmi_ds_ports;

        /* patch in the i2c address of the endpoint */
        buf[14] = i2c->address;

        break;

    case NMI_CMD_READ_NMI_DS_CTRL_INFO:
        len = 4;
        buf = nmi_ds_error;

        break;

    case NMI_CMD_READ_NMI_DS_CTRL_LIST:
    case NMI_CMD_READ_NMI_DS_CMD_SUPPORT:
    case NMI_CMD_READ_NMI_DS_MEB_CMD_SUPPORT:
        len = 8;
        buf = nmi_ds_empty;

        break;

    default:
        len = 4;
        buf = nmi_ds_error;

        /* patch in the invalid parameter position */
        buf[2] = 0x03; /* first invalid byte position (dtyp) */

        break;
    }

    memcpy(nmi->scratch + nmi->pos, buf, len);
    nmi->pos += len;
}

enum {
    NMI_CMD_CONFIGURATION_GET_SMBUS_FREQ                = 0x1,
    NMI_CMD_CONFIGURATION_GET_HEALTH_STATUS_CHANGE      = 0x2,
    NMI_CMD_CONFIGURATION_GET_MCTP_TRANSMISSION_UNIT    = 0x3,
};

static void nmi_handle_mi_config_get(NMIDevice *nmi, NMIRequest *request)
{
    uint32_t dw0 = le32_to_cpu(request->dw0);
    uint8_t identifier = dw0 & 0xff;
    uint8_t *buf;

    trace_nmi_handle_mi_config_get(identifier);

    switch (identifier) {
    case NMI_CMD_CONFIGURATION_GET_SMBUS_FREQ:
        buf = (uint8_t[]) {
            0x0, 0x1, 0x0, 0x0,
        };

        break;

    case NMI_CMD_CONFIGURATION_GET_HEALTH_STATUS_CHANGE:
        buf = (uint8_t[]) {
            0x0, 0x0, 0x0, 0x0,
        };

        break;

    case NMI_CMD_CONFIGURATION_GET_MCTP_TRANSMISSION_UNIT:
        buf = (uint8_t[]) {
            0x0, 0x40, 0x0, 0x0,
        };

        break;
    }

    memcpy(nmi->scratch + nmi->pos, buf, 4);
    nmi->pos += 4;
}

enum {
    NMI_CMD_READ_NMI_DS         = 0x0,
    NMI_CMD_CONFIGURATION_GET   = 0x4,
};

static void nmi_set_parameter_error(NMIDevice *nmi, uint8_t bit, uint16_t byte)
{
    nmi->scratch[nmi->pos++] = 0x4;
    nmi->scratch[nmi->pos++] = bit;
    nmi->scratch[nmi->pos++] = (byte >> 4) & 0xf;
    nmi->scratch[nmi->pos++] = byte & 0xf;
}

static void nmi_set_error(NMIDevice *nmi, uint8_t status)
{
    uint8_t buf[4] = {};

    buf[0] = status;

    memcpy(nmi->scratch + nmi->pos, buf, 4);
    nmi->pos += 4;
}

static void nmi_handle_mi(NMIDevice *nmi, NMIMessage *msg)
{
    NMIRequest *request = (NMIRequest *)msg->payload;

    trace_nmi_handle_mi(request->opc);

    switch (request->opc) {
    case NMI_CMD_READ_NMI_DS:
        nmi_handle_mi_read_nmi_ds(nmi, request);
        break;

    case NMI_CMD_CONFIGURATION_GET:
        nmi_handle_mi_config_get(nmi, request);
        break;

    default:
        nmi_set_parameter_error(nmi, 0x0, 0x0);
        fprintf(stderr, "nmi command 0x%x not handled\n", request->opc);

        break;
    }
}

enum {
    NMI_MESSAGE_TYPE_NMI = 0x1,
};

static void nmi_handle_message(MCTPI2CEndpoint *mctp)
{
    NMIDevice *nmi = NMI_I2C_DEVICE(mctp);
    NMIMessage *msg = (NMIMessage *)nmi->buffer;
    uint32_t crc;
    uint8_t nmimt;

    uint8_t buf[] = {
        MCTP_MESSAGE_TYPE_NMI | MCTP_MESSAGE_IC,
        FIELD_DP8(msg->nmp, NMI_NMP, ROR, 1),
        0x0, 0x0,
    };

    memcpy(nmi->scratch, buf, sizeof(buf));
    nmi->pos = sizeof(buf);

    nmimt = FIELD_EX8(msg->nmp, NMI_NMP, NMIMT);

    trace_nmi_handle_msg(nmimt);

    switch (nmimt) {
    case NMI_MESSAGE_TYPE_NMI:
        nmi_handle_mi(nmi, msg);
        break;

    default:
        fprintf(stderr, "nmi message type 0x%x not handled\n", nmimt);

        nmi_set_error(nmi, 0x3);

        break;
    }

    /* add message integrity check */
    memset(nmi->scratch + nmi->pos, 0x0, sizeof(crc));

    crc = crc32c(0xffffffff, nmi->scratch, nmi->pos);
    memcpy(nmi->scratch + nmi->pos, &crc, sizeof(crc));

    nmi->len = nmi->pos + sizeof(crc);
    nmi->pos = 0;

    i2c_mctp_schedule_send(mctp);
}

static size_t nmi_get_message_bytes(MCTPI2CEndpoint *mctp, uint8_t *buf,
                                    size_t maxlen, uint8_t *mctp_flags)
{
    NMIDevice *nmi = NMI_I2C_DEVICE(mctp);
    size_t len;

    len = MIN(maxlen, nmi->len - nmi->pos);

    if (len == 0) {
        return 0;
    }

    if (nmi->pos == 0) {
        *mctp_flags |= MCTP_H_FLAGS_SOM;
    }

    memcpy(buf, nmi->scratch + nmi->pos, len);
    nmi->pos += len;

    if (nmi->pos == nmi->len) {
        *mctp_flags |= MCTP_H_FLAGS_EOM;

        nmi->pos = nmi->len = 0;
    }

    return len;
}

static int nmi_put_message_bytes(MCTPI2CEndpoint *mctp, uint8_t *buf,
                                 size_t len)
{
    NMIDevice *nmi = NMI_I2C_DEVICE(mctp);

    if (nmi->len + len > NMI_MAX_MESSAGE_LENGTH) {
        return -1;
    }

    memcpy(nmi->buffer + nmi->len, buf, len);
    nmi->len += len;

    return 0;
}

static void nmi_reset_message(MCTPI2CEndpoint *mctp)
{
    NMIDevice *nmi = NMI_I2C_DEVICE(mctp);
    nmi->len = 0;
}

static size_t nmi_get_message_types(MCTPI2CEndpoint *mctp, uint8_t *data)
{
    uint8_t buf[] = {
        0x0, 0x1, 0x4,
    };

    memcpy(data, buf, sizeof(buf));

    return sizeof(buf);
}

static void nvme_mi_class_init(ObjectClass *oc, void *data)
{
    MCTPI2CEndpointClass *mc = MCTP_I2C_ENDPOINT_CLASS(oc);

    mc->get_message_types = nmi_get_message_types;

    mc->get_message_bytes = nmi_get_message_bytes;
    mc->put_message_bytes = nmi_put_message_bytes;

    mc->handle_message = nmi_handle_message;
    mc->reset_message = nmi_reset_message;
}

static const TypeInfo nvme_mi = {
    .name = TYPE_NMI_I2C_DEVICE,
    .parent = TYPE_MCTP_I2C_ENDPOINT,
    .instance_size = sizeof(NMIDevice),
    .class_init = nvme_mi_class_init,
};

static void register_types(void)
{
    type_register_static(&nvme_mi);
}

type_init(register_types);
