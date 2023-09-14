/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * SPDX-FileCopyrightText: Copyright (c) 2023 Samsung Electronics Co., Ltd.
 *
 * SPDX-FileContributor: Padmakar Kalghatgi <p.kalghatgi@samsung.com>
 * SPDX-FileContributor: Arun Kumar Agasar <arun.kka@samsung.com>
 * SPDX-FileContributor: Saurav Kumar <saurav.29@partner.samsung.com>
 * SPDX-FileContributor: Klaus Jensen <k.jensen@samsung.com>
 */

#include "qemu/osdep.h"
#include "qemu/crc32c.h"
#include "hw/registerfields.h"
#include "hw/i2c/i2c.h"
#include "hw/i2c/mctp.h"
#include "net/mctp.h"
#include "trace.h"

/* NVM Express Management Interface 1.2c, Section 3.1 */
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

FIELD(NMI_MCTPD, MT, 0, 7)
FIELD(NMI_MCTPD, IC, 7, 1)

#define NMI_MCTPD_MT_NMI 0x4
#define NMI_MCTPD_IC_ENABLED 0x1

FIELD(NMI_NMP, ROR, 7, 1)
FIELD(NMI_NMP, NMIMT, 3, 4)

#define NMI_NMP_NMIMT_NVME_MI 0x1
#define NMI_NMP_NMIMT_NVME_ADMIN 0x2

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

FIELD(NMI_CMD_READ_NMI_DS_DW0, DTYP, 24, 8)

typedef enum NMIReadDSType {
    NMI_CMD_READ_NMI_DS_SUBSYSTEM       = 0x0,
    NMI_CMD_READ_NMI_DS_PORTS           = 0x1,
    NMI_CMD_READ_NMI_DS_CTRL_LIST       = 0x2,
    NMI_CMD_READ_NMI_DS_CTRL_INFO       = 0x3,
    NMI_CMD_READ_NMI_DS_OPT_CMD_SUPPORT = 0x4,
    NMI_CMD_READ_NMI_DS_MEB_CMD_SUPPORT = 0x5,
} NMIReadDSType;

#define NMI_STATUS_INVALID_PARAMETER 0x4

static void nmi_scratch_append(NMIDevice *nmi, const void *buf, size_t count)
{
    assert(nmi->pos + count <= NMI_MAX_MESSAGE_LENGTH);

    memcpy(nmi->scratch + nmi->pos, buf, count);
    nmi->pos += count;
}

static void nmi_set_parameter_error(NMIDevice *nmi, uint8_t bit, uint16_t byte)
{
    /* NVM Express Management Interface 1.2c, Figure 30 */
    struct resp {
        uint8_t  status;
        uint8_t  bit;
        uint16_t byte;
    };

    struct resp buf = {
        .status = NMI_STATUS_INVALID_PARAMETER,
        .bit = bit & 0x3,
        .byte = byte,
    };

    nmi_scratch_append(nmi, &buf, sizeof(buf));
}

static void nmi_set_error(NMIDevice *nmi, uint8_t status)
{
    const uint8_t buf[4] = {status,};

    nmi_scratch_append(nmi, buf, sizeof(buf));
}

static void nmi_handle_mi_read_nmi_ds(NMIDevice *nmi, NMIRequest *request)
{
    I2CSlave *i2c = I2C_SLAVE(nmi);

    uint32_t dw0 = le32_to_cpu(request->dw0);
    uint8_t dtyp = FIELD_EX32(dw0, NMI_CMD_READ_NMI_DS_DW0, DTYP);

    trace_nmi_handle_mi_read_nmi_ds(dtyp);

    static const uint8_t nmi_ds_subsystem[36] = {
        0x00,       /* success */
        0x20, 0x00, /* response data length */
        0x00,       /* reserved */
        0x00,       /* number of ports */
        0x01,       /* major version */
        0x01,       /* minor version */
    };

    /*
     * Cannot be static (or const) since we need to patch in the i2c
     * address.
     */
    const uint8_t nmi_ds_ports[36] = {
        0x00,       /* success */
        0x20, 0x00, /* response data length */
        0x00,       /* reserved */
        0x02,       /* port type (smbus) */
        0x00,       /* reserved */
        0x40, 0x00, /* maximum mctp transission unit size (64 bytes) */
        0x00, 0x00, 0x00, 0x00, /* management endpoint buffer size */
        0x00,       /* vpd i2c address */
        0x00,       /* vpd i2c frequency */
        i2c->address, /* management endpoint i2c address */
        0x01,       /* management endpoint i2c frequency */
        0x00,       /* nvme basic management command NOT supported */
    };

    /**
     * Controller Information is zeroed, since there are no associated
     * controllers at this point.
     */
    static const uint8_t nmi_ds_ctrl[36] = {};

    /**
     * For the Controller List, Optionally Supported Command List and
     * Management Endpoint Buffer Supported Command List data structures.
     *
     * The Controller List data structure is defined in the NVM Express Base
     * Specification, revision 2.0b, Figure 134.
     */
    static const uint8_t nmi_ds_empty[6] = {
        0x00,       /* success */
        0x02,       /* response data length */
        0x00, 0x00, /* reserved */
        0x00, 0x00, /* number of entries (zero) */
    };

    switch (dtyp) {
    case NMI_CMD_READ_NMI_DS_SUBSYSTEM:
        nmi_scratch_append(nmi, &nmi_ds_subsystem, sizeof(nmi_ds_subsystem));
        return;

    case NMI_CMD_READ_NMI_DS_PORTS:
        nmi_scratch_append(nmi, &nmi_ds_ports, sizeof(nmi_ds_ports));
        return;

    case NMI_CMD_READ_NMI_DS_CTRL_INFO:
        nmi_scratch_append(nmi, &nmi_ds_ctrl, sizeof(nmi_ds_ctrl));
        return;

    case NMI_CMD_READ_NMI_DS_CTRL_LIST:
    case NMI_CMD_READ_NMI_DS_OPT_CMD_SUPPORT:
    case NMI_CMD_READ_NMI_DS_MEB_CMD_SUPPORT:
        nmi_scratch_append(nmi, &nmi_ds_empty, sizeof(nmi_ds_empty));
        return;

    default:
        nmi_set_parameter_error(nmi, offsetof(NMIRequest, dw0) + 4, 0);
        return;
    }
}

FIELD(NMI_CMD_CONFIGURATION_GET_DW0, IDENTIFIER, 0, 8)

enum {
    NMI_CMD_CONFIGURATION_GET_SMBUS_FREQ                = 0x1,
    NMI_CMD_CONFIGURATION_GET_HEALTH_STATUS_CHANGE      = 0x2,
    NMI_CMD_CONFIGURATION_GET_MCTP_TRANSMISSION_UNIT    = 0x3,
};

static void nmi_handle_mi_config_get(NMIDevice *nmi, NMIRequest *request)
{
    uint32_t dw0 = le32_to_cpu(request->dw0);
    uint8_t identifier = FIELD_EX32(dw0, NMI_CMD_CONFIGURATION_GET_DW0,
                                    IDENTIFIER);
    static const uint8_t smbus_freq[4] = {
        0x00,               /* success */
        0x01, 0x00, 0x00,   /* 100 kHz */
    };

    static const uint8_t mtu[4] = {
        0x00,       /* success */
        0x40, 0x00, /* 64 */
        0x00,       /* reserved */
    };

    trace_nmi_handle_mi_config_get(identifier);

    switch (identifier) {
    case NMI_CMD_CONFIGURATION_GET_SMBUS_FREQ:
        nmi_scratch_append(nmi, &smbus_freq, sizeof(smbus_freq));
        return;

    case NMI_CMD_CONFIGURATION_GET_MCTP_TRANSMISSION_UNIT:
        nmi_scratch_append(nmi, &mtu, sizeof(mtu));
        return;

    default:
        nmi_set_parameter_error(nmi, 0x0, offsetof(NMIRequest, dw0));
        return;
    }
}

enum {
    NMI_CMD_READ_NMI_DS         = 0x0,
    NMI_CMD_CONFIGURATION_GET   = 0x4,
};

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

static void nmi_reset(MCTPI2CEndpoint *mctp)
{
    NMIDevice *nmi = NMI_I2C_DEVICE(mctp);
    nmi->len = 0;
}

static void nmi_handle(MCTPI2CEndpoint *mctp)
{
    NMIDevice *nmi = NMI_I2C_DEVICE(mctp);
    NMIMessage *msg = (NMIMessage *)nmi->buffer;
    uint32_t crc;
    uint8_t nmimt;

    const uint8_t buf[] = {
        msg->mctpd,
        FIELD_DP8(msg->nmp, NMI_NMP, ROR, 1),
        0x0, 0x0,
    };

    if (FIELD_EX8(msg->mctpd, NMI_MCTPD, MT) != NMI_MCTPD_MT_NMI) {
        goto drop;
    }

    if (FIELD_EX8(msg->mctpd, NMI_MCTPD, IC) != NMI_MCTPD_IC_ENABLED) {
        goto drop;
    }

    nmi->pos = 0;
    nmi_scratch_append(nmi, buf, sizeof(buf));

    nmimt = FIELD_EX8(msg->nmp, NMI_NMP, NMIMT);

    trace_nmi_handle_msg(nmimt);

    switch (nmimt) {
    case NMI_NMP_NMIMT_NVME_MI:
        nmi_handle_mi(nmi, msg);
        break;

    default:
        fprintf(stderr, "nmi message type 0x%x not handled\n", nmimt);

        nmi_set_error(nmi, 0x3);

        break;
    }

    crc = crc32c(0xffffffff, nmi->scratch, nmi->pos);
    nmi_scratch_append(nmi, &crc, sizeof(crc));

    nmi->len = nmi->pos;
    nmi->pos = 0;

    i2c_mctp_schedule_send(mctp);

    return;

drop:
    nmi_reset(mctp);
}

static size_t nmi_get_buf(MCTPI2CEndpoint *mctp, const uint8_t **buf,
                          size_t maxlen, uint8_t *mctp_flags)
{
    NMIDevice *nmi = NMI_I2C_DEVICE(mctp);
    size_t len;

    len = MIN(maxlen, nmi->len - nmi->pos);

    if (len == 0) {
        return 0;
    }

    if (nmi->pos == 0) {
        *mctp_flags = FIELD_DP8(*mctp_flags, MCTP_H_FLAGS, SOM, 1);
    }

    *buf = nmi->scratch + nmi->pos;
    nmi->pos += len;

    if (nmi->pos == nmi->len) {
        *mctp_flags = FIELD_DP8(*mctp_flags, MCTP_H_FLAGS, EOM, 1);

        nmi->pos = nmi->len = 0;
    }

    return len;
}

static int nmi_put_buf(MCTPI2CEndpoint *mctp, uint8_t *buf, size_t len)
{
    NMIDevice *nmi = NMI_I2C_DEVICE(mctp);

    if (nmi->len + len > NMI_MAX_MESSAGE_LENGTH) {
        return -1;
    }

    memcpy(nmi->buffer + nmi->len, buf, len);
    nmi->len += len;

    return 0;
}

static size_t nmi_get_types(MCTPI2CEndpoint *mctp, const uint8_t **data)
{
    /**
     * DSP0236 1.3.0, Table 19.
     *
     * This only includes message types that are supported *in addition* to the
     * MCTP control message type.
     */
    static const uint8_t buf[] = {
        0x0,                /* success */
        0x1,                /* number of message types in list (supported) */
        NMI_MCTPD_MT_NMI,
    };

    *data = buf;

    return sizeof(buf);
}

static void nvme_mi_class_init(ObjectClass *oc, void *data)
{
    MCTPI2CEndpointClass *mc = MCTP_I2C_ENDPOINT_CLASS(oc);

    mc->get_types = nmi_get_types;

    mc->get_buf = nmi_get_buf;
    mc->put_buf = nmi_put_buf;

    mc->handle = nmi_handle;
    mc->reset = nmi_reset;
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
