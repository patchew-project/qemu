/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Emulation of a CXL Switch Fabric Management interface over MCTP over I2C.
 *
 * Copyright (c) 2023 Huawei Technologies.
 *
 * Reference list:
 * From www.dmtf.org
 * DSP0236 Management Component Transport Protocol (MCTP) Base Specification
 *    1.3.0
 * DPS0234 CXL Fabric Manager API over MCTP Binding Specification 1.0.0
 * DSP0281 CXL Type 3 Device Component Command Interface over MCTP Binding
 *    Specification (note some commands apply to switches as well)
 * From www.computeexpresslink.org
 * Compute Express Link (CXL) Specification revision 3.0 Version 1.0
 */

#include "qemu/osdep.h"
#include "hw/i2c/i2c.h"
#include "hw/i2c/mctp.h"
#include "net/mctp.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "hw/cxl/cxl.h"
#include "hw/pci-bridge/cxl_upstream_port.h"
#include "hw/pci/pcie.h"
#include "hw/pci/pcie_port.h"
#include "hw/qdev-properties.h"
#include "hw/registerfields.h"

#define TYPE_I2C_MCTP_CXL "i2c_mctp_cxl"

/* DMTF DSP0234 CXL Fabric Manager API over MCTP Binding Specification */
#define MCTP_MT_CXL_FMAPI 0x7
/*
 * DMTF DSP0281 CXL Type 3 Deivce Component Command Interface over MCTP
 * Binding Specification
 */
#define MCTP_MT_CXL_TYPE3 0x8

/* FMAPI binding specification defined */
#define MCTP_CXL_MAX_MSG_LEN 1088

/* Implementation choice - may make this configurable */
#define MCTP_CXL_MAILBOX_BYTES 512

typedef struct CXLMCTPMessage {
    /*
     * DSP0236 (MCTP Base) Integrity Check + Message Type
     * DSP0234/DSP0281 (CXL bindings) state no Integrity Check
     * so just the message type.
     */
    uint8_t message_type;
    /* Remaing fields from CXL r3.0 Table 7-14 CCI Message Format */
    uint8_t category;
    uint8_t tag;
    uint8_t rsvd;
    /*
     * CXL r3.0 - Table 8-36 Generic Component Command Opcodes:
     * Command opcode is split into two sub fields
     */
    uint8_t command;
    uint8_t command_set;
    uint8_t pl_length[3];
    uint16_t rc;
    uint16_t vendor_status;
    uint8_t payload[];
} QEMU_PACKED CXLMCTPMessage;

enum cxl_dev_type {
    cxl_type3,
    cxl_switch,
};

struct I2C_MCTP_CXL_State {
    MCTPI2CEndpoint mctp;
    PCIDevice *target;
    CXLCCI *cci;
    enum cxl_dev_type type;
    size_t len;
    int64_t pos;
    uint8_t buffer[MCTP_CXL_MAX_MSG_LEN];
    uint8_t scratch[MCTP_CXL_MAX_MSG_LEN];
};

OBJECT_DECLARE_SIMPLE_TYPE(I2C_MCTP_CXL_State, I2C_MCTP_CXL)

static const Property i2c_mctp_cxl_props[] = {
    DEFINE_PROP_LINK("target", I2C_MCTP_CXL_State,
                     target, TYPE_PCI_DEVICE, PCIDevice *),
};

static size_t i2c_mctp_cxl_get_buf(MCTPI2CEndpoint *mctp,
                                   const uint8_t **buf,
                                   size_t maxlen,
                                   uint8_t *mctp_flags)
{
    I2C_MCTP_CXL_State *s = I2C_MCTP_CXL(mctp);
    size_t len;

    len = MIN(maxlen, s->len - s->pos);

    if (len == 0) {
        return 0;
    }

    if (s->pos == 0) {
        *mctp_flags = FIELD_DP8(*mctp_flags, MCTP_H_FLAGS, SOM, 1);
    }

    *buf = s->scratch + s->pos;
    s->pos += len;

    if (s->pos == s->len) {
        *mctp_flags = FIELD_DP8(*mctp_flags, MCTP_H_FLAGS, EOM, 1);

        s->pos = s->len = 0;
    }

    return len;
}

static int i2c_mctp_cxl_put_buf(MCTPI2CEndpoint *mctp,
                                uint8_t *buf, size_t len)
{
    I2C_MCTP_CXL_State *s = I2C_MCTP_CXL(mctp);

    if (s->len + len > MCTP_CXL_MAX_MSG_LEN) {
        return -1;
    }

    memcpy(s->buffer + s->len, buf, len);
    s->len += len;

    return 0;
}

static size_t i2c_mctp_cxl_get_types(MCTPI2CEndpoint *mctp,
                                     const uint8_t **data)
{
    static const uint8_t buf[] = {
        0x0, /* Success */
        2, /* Message types in list - supported in addition to control */
        MCTP_MT_CXL_FMAPI,
        MCTP_MT_CXL_TYPE3,
    };
    *data = buf;

    return sizeof(buf);
}

static void i2c_mctp_cxl_reset_message(MCTPI2CEndpoint *mctp)
{
    I2C_MCTP_CXL_State *s = I2C_MCTP_CXL(mctp);

    s->len = 0;
}

static void i2c_mctp_cxl_handle_message(MCTPI2CEndpoint *mctp)
{
    I2C_MCTP_CXL_State *s = I2C_MCTP_CXL(mctp);
    CXLMCTPMessage *msg = (CXLMCTPMessage *)s->buffer;
    CXLMCTPMessage *buf = (CXLMCTPMessage *)s->scratch;

    *buf = (CXLMCTPMessage) {
        .message_type = msg->message_type,
        .category = 1,
        .tag = msg->tag,
        .command = msg->command,
        .command_set = msg->command_set,
    };
    s->pos = sizeof(*buf);
    if (s->cci) {
        bool bg_started;
        size_t len_out = 0;
        size_t len_in;
        int rc;

        /*
         * As it was not immediately obvious from the various specifications,
         * clarification was sort for which binding applies for which command
         * set. The outcome was:
         *
         * Any command forming part of the CXL FM-API command set
         * e.g. Present in CXL r3.0 Table 8-132: CXL FM API Command Opcodes
         * (and equivalent in later CXL specifications) is valid only with
         * the CXL Fabric Manager API over MCTP binding (DSP0234).
         *
         * Any other CXL command currently should be sent using the
         * CXL Type 3 Device Component Command interface over MCTP binding,
         * even if it is being sent to a switch.
         *
         * If tunneling is used, the component creating the PCIe VDMs must
         * use the appropriate binding for sending the tunnel contents
         * onwards.
         */

        if (!(msg->message_type == MCTP_MT_CXL_TYPE3 &&
              msg->command_set < 0x51) &&
            !(msg->message_type == MCTP_MT_CXL_FMAPI &&
              msg->command_set >= 0x51 && msg->command_set < 0x56)) {
            buf->rc = CXL_MBOX_UNSUPPORTED;
            st24_le_p(buf->pl_length, len_out);
            s->len = s->pos;
            s->pos = 0;
            i2c_mctp_schedule_send(mctp);
            return;
        }

        len_in = msg->pl_length[2] << 16 | msg->pl_length[1] << 8 |
            msg->pl_length[0];

        rc = cxl_process_cci_message(s->cci, msg->command_set, msg->command,
                                     len_in, msg->payload,
                                     &len_out,
                                     s->scratch + sizeof(CXLMCTPMessage),
                                     &bg_started);
        buf->rc = rc;
        s->pos += len_out;
        s->len = s->pos;
        st24_le_p(buf->pl_length, len_out);
        s->pos = 0;
        i2c_mctp_schedule_send(mctp);
    } else {
        g_assert_not_reached(); /* The cci must be hooked up */
    }
}

static void i2c_mctp_cxl_realize(DeviceState *d, Error **errp)
{
    I2C_MCTP_CXL_State *s = I2C_MCTP_CXL(d);

    /* Check this is a type we support */
    if (object_dynamic_cast(OBJECT(s->target), TYPE_CXL_USP)) {
        CXLUpstreamPort *usp = CXL_USP(s->target);

        s->type = cxl_switch;
        s->cci = &usp->mctpcci;

        cxl_initialize_usp_mctpcci(s->cci, DEVICE(s->target), d,
                                   MCTP_CXL_MAILBOX_BYTES);

        return;
    }

    if (object_dynamic_cast(OBJECT(s->target), TYPE_CXL_TYPE3)) {
        CXLType3Dev *ct3d = CXL_TYPE3(s->target);

        s->type = cxl_type3;
        s->cci = &ct3d->oob_mctp_cci;

        cxl_initialize_t3_fm_owned_ld_mctpcci(s->cci, DEVICE(s->target), d,
                                              MCTP_CXL_MAILBOX_BYTES);
        return;
    }

    error_setg(errp, "Unhandled target type for CXL MCTP EP");
}

static void i2c_mctp_cxl_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    MCTPI2CEndpointClass *mc = MCTP_I2C_ENDPOINT_CLASS(klass);

    dc->realize = i2c_mctp_cxl_realize;
    mc->get_types = i2c_mctp_cxl_get_types;
    mc->get_buf = i2c_mctp_cxl_get_buf;
    mc->put_buf = i2c_mctp_cxl_put_buf;

    mc->handle = i2c_mctp_cxl_handle_message;
    mc->reset = i2c_mctp_cxl_reset_message;
    device_class_set_props(dc, i2c_mctp_cxl_props);
}

static const TypeInfo i2c_mctp_cxl_info = {
    .name = TYPE_I2C_MCTP_CXL,
    .parent = TYPE_MCTP_I2C_ENDPOINT,
    .instance_size = sizeof(I2C_MCTP_CXL_State),
    .class_init = i2c_mctp_cxl_class_init,
};

static void i2c_mctp_cxl_register_types(void)
{
    type_register_static(&i2c_mctp_cxl_info);
}

type_init(i2c_mctp_cxl_register_types)
