/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright Huawei 2025
 *
 * Author: Jonathan Cameron <jonathan.cameron@huawei.com>
 *
 * CXL MCTP device
 *
 * TODO:
 * - Respect MTU on packets being sent to host. For now it work in Linux but
 *   who knows longer term.
 * - More complete sanity checking of command flags etc.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"

#include "hw/pci/pci_device.h"
#include "hw/qdev-properties.h"
#include "hw/cxl/cxl.h"
#include "hw/pci-bridge/cxl_upstream_port.h"
#include "hw/pci/pcie.h"
#include "hw/pci/pcie_port.h"
#include "hw/usb.h"
#include "hw/usb/desc.h"
#include "net/mctp.h"

/* TODO: Move to header */

/* DMTF DSP0234 CXL Fabric Manager API over MCTP Binding Specification */
#define MCTP_MT_CXL_FMAPI 0x7
/*
 * DMTF DSP0281 CXL Type 3 Device Component Command Interface over MCTP
 * Binding Specification
 */
#define MCTP_MT_CXL_TYPE3 0x8
typedef struct CXLMCTPMessage {
    /*
     * DSP0236 (MCTP Base) Integrity Check + Message Type
     * DSP0234/DSP0281 (CXL bindings) state no Integrity Check
     * so just the message type.
     */
    uint8_t message_type;
    /* Remaining fields from CXL r3.0 Table 7-14 CCI Message Format */
    uint8_t category;
    uint8_t tag;
    uint8_t rsvd;
    /*
     * CXL r3.0 - Table 8-36 Generic Component Command Opcodes:
     * Command opcode is split into two sub fields
     */
    uint8_t command;
    uint8_t command_set;
    /* Only bits 4:0 of pl_length[2] are part of the length */
    uint8_t pl_length[3];
    uint16_t rc;
    uint16_t vendor_status;
    uint8_t payload[];
} QEMU_PACKED CXLMCTPMessage;

/* DSP0283 1.0.0 Figure 5 */
typedef struct MCTPUSBPacket {
    uint16_t dmtf_id;
    uint8_t resv;
    uint8_t length;
    /* Do we see the header? */
    MCTPPacket mctp;
} MCTPUSBPacket;

#define MCTP_CXL_MAILBOX_BYTES 128

enum cxl_dev_type {
    cxl_type3,
    cxl_switch,
};

typedef struct USBCXLMCTPState {
    USBDevice dev;
    PCIDevice *target;
    CXLCCI *cci;
    enum cxl_dev_type type;
    USBPacket *cached_tohost;
    USBPacket *cached_fromhost;
    uint8_t my_eid;
    bool building_input;
/* TODO, match size to what we report as acceptable */
#define MCTPUSBCXL_MAX_SIZE 1024 * 1024
    MCTPUSBPacket *pack0; /* Current incoming packet */
    MCTPUSBPacket *pack; /* Aggregate packet - what we'd get with large MTU */
} USBCLXMCTState;

#define TYPE_USB_CXL_MCTP "usb-cxl-mctp"
OBJECT_DECLARE_SIMPLE_TYPE(USBCXLMCTPState, USB_CXL_MCTP)

enum {
    STR_MANUFACTURER = 1,
    STR_PRODUCT,
    STR_SERIALNUMBER,
    STR_MCTP,
    STR_CONFIG_FULL,
    STR_CONFIG_HIGH,
    STR_CONFIG_SUPER,
};

static const USBDescStrings desc_strings = {
    [STR_MANUFACTURER] = "QEMU",
    [STR_PRODUCT] = "QEMU CXL MCTP",
    [STR_SERIALNUMBER] = "34618",
    [STR_MCTP] = "MCTP",
    [STR_CONFIG_FULL] = "Full speed config (usb 1.1)",
    [STR_CONFIG_HIGH] = "High speed config (usb 2.0)",
    [STR_CONFIG_SUPER] = "Super speed config (usb 3.0)",
};

static const USBDescIface desc_iface_full = {
    .bInterfaceNumber = 0,
    .bNumEndpoints = 2,
    .bInterfaceClass = USB_CLASS_MCTP,
    .bInterfaceSubClass = 0x0,
    .bInterfaceProtocol = 0x1,
    .iInterface = STR_MCTP,
    .eps = (USBDescEndpoint[]) {
        {
             /* DSP0283 6.1.4.2.1 Out Bulk endpoint descriptor */
            .bEndpointAddress = USB_DIR_OUT | 0x1,
            .bmAttributes = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize = 512,
            .bInterval = 0x1,
        }, {
            /* DSP0283 6.1.4.2.2 In Bulk endpoint descriptor */
            .bEndpointAddress = USB_DIR_IN | 0x1,
            .bmAttributes = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize = 512,
            .bInterval = 0x1,
        },
    },
};

static const USBDescDevice desc_device_full = {
    .bcdUSB = 0x200,
    .bMaxPacketSize0 = 8,
    .bNumConfigurations = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces = 1,
            .bConfigurationValue = 1,
            .iConfiguration = STR_CONFIG_FULL,
            .bmAttributes = USB_CFG_ATT_ONE | USB_CFG_ATT_WAKEUP,
            .bMaxPower = 2,
            .nif = 1,
            .ifs = &desc_iface_full,
        },
    },
};

static const USBDescMSOS desc_msos = {
    .CompatibleID = "MCTP",
    .SelectiveSuspendEnabled = true,
};

static const USBDesc desc = {
    .id = {
        .idVendor = 0x46f4, /* CRC16() of "QEMU" */
        .idProduct = 0x0006,
        .bcdDevice = 0,
        .iManufacturer = STR_MANUFACTURER,
        .iSerialNumber = STR_SERIALNUMBER,
    },
    .full = &desc_device_full,
    .high = &desc_device_full,
    .str = desc_strings,
    .msos = &desc_msos,
};

static void usb_cxl_mctp_handle_reset(USBDevice *dev)
{
    USBCXLMCTPState *s = USB_CXL_MCTP(dev);

    s->cached_tohost = NULL;
    s->cached_fromhost = NULL;
    s->building_input = 0;
    s->my_eid = 0;
}

static void usb_cxl_mctp_handle_control(USBDevice *dev, USBPacket *p,
                                        int request, int value, int index,
                                        int length, uint8_t *data)
{
    usb_desc_handle_control(dev, p, request, value, index, length, data);
}

/* A lot of fields are the same for all control responses */
static void usb_mctp_fill_common(MCTPUSBPacket *o_usb_pkt,
                                 MCTPUSBPacket *i_usb_pkt,
                                 uint8_t usb_pkt_len)
{
    MCTPPacket *o_mctp_pkt = &o_usb_pkt->mctp;
    uint8_t tag, flags;

    o_usb_pkt->dmtf_id = cpu_to_be16(0x1AB4);
    o_usb_pkt->length = usb_pkt_len;

    o_mctp_pkt->hdr.version = 1;
    o_mctp_pkt->hdr.eid.dest =  i_usb_pkt->mctp.hdr.eid.source;
    o_mctp_pkt->hdr.eid.source = i_usb_pkt->mctp.hdr.eid.dest;

    tag = FIELD_EX8(i_usb_pkt->mctp.hdr.flags, MCTP_H_FLAGS, TAG);

    flags = FIELD_DP8(0, MCTP_H_FLAGS, PKTSEQ, 0);
    flags = FIELD_DP8(flags, MCTP_H_FLAGS, TAG, tag);
    flags = FIELD_DP8(flags, MCTP_H_FLAGS, SOM, 1);
    flags = FIELD_DP8(flags, MCTP_H_FLAGS, EOM, 1);
    o_mctp_pkt->hdr.flags = flags;
}

static MCTPUSBPacket *usb_mctp_handle_control(USBCXLMCTPState *s,
                                              MCTPControlMessage *ctrlmsg,
                                              USBPacket *p)
{
    switch (ctrlmsg->command_code) {
    case MCTP_CONTROL_SET_EID: {
        MCTPControlSetEIDReq *req = (MCTPControlSetEIDReq *)(ctrlmsg->data);
        uint8_t usb_pkt_len = sizeof(MCTPUSBPacket) +
            sizeof(MCTPControlMessage) +
            sizeof(MCTPControlSetEIDRsp);
        MCTPUSBPacket *o_usb_pkt = g_malloc0(usb_pkt_len);
        MCTPPacket *o_mctp_pkt = &o_usb_pkt->mctp;
        MCTPControlMessage *o_mctp_ctrl =
            (MCTPControlMessage *)(o_mctp_pkt->payload);
        MCTPControlSetEIDRsp *rsp = (MCTPControlSetEIDRsp *)o_mctp_ctrl->data;

        /* Todo - check flags in request */
        s->my_eid = req->eid;

        o_mctp_ctrl->type = 0;
        o_mctp_ctrl->command_code = ctrlmsg->command_code;
        o_mctp_ctrl->flags = ctrlmsg->flags &
            ~(MCTP_CONTROL_FLAGS_RQ | MCTP_CONTROL_FLAGS_D);
        *rsp = (MCTPControlSetEIDRsp) {
            .completion_code = MCTP_CONTROL_CC_SUCCESS,
            .operation_result = 0,
            .eid_setting = s->my_eid,
            .eid_pool_size = 0,
        };

        usb_mctp_fill_common(o_usb_pkt, s->pack, usb_pkt_len);

        return o_usb_pkt;
    }
    case MCTP_CONTROL_GET_EID: {
        uint8_t usb_pkt_len = sizeof(MCTPUSBPacket) +
            sizeof(MCTPControlMessage) +
            sizeof(MCTPControlGetEIDRsp);
        MCTPUSBPacket *o_usb_pkt = g_malloc0(usb_pkt_len);
        MCTPPacket *o_mctp_pkt = &o_usb_pkt->mctp;
        MCTPControlMessage *o_mctp_ctrl =
            (MCTPControlMessage *)(o_mctp_pkt->payload);
        MCTPControlGetEIDRsp *rsp = (MCTPControlGetEIDRsp *)o_mctp_ctrl->data;

        o_mctp_ctrl->type = 0;
        o_mctp_ctrl->command_code = ctrlmsg->command_code;
        o_mctp_ctrl->flags = ctrlmsg->flags &
            ~(MCTP_CONTROL_FLAGS_RQ | MCTP_CONTROL_FLAGS_D);
        *rsp = (MCTPControlGetEIDRsp) {
            .completion_code = MCTP_CONTROL_CC_SUCCESS,
            .endpoint_id = s->my_eid,
            .endpoint_type = 0,
            .medium_specific_info = 0,
        };
        usb_mctp_fill_common(o_usb_pkt, s->pack, usb_pkt_len);

        return o_usb_pkt;
    }
    case MCTP_CONTROL_GET_UUID: {
        uint8_t usb_pkt_len = sizeof(MCTPUSBPacket) +
            sizeof(MCTPControlMessage) +
            sizeof(MCTPControlGetUUIDRsp);
        MCTPUSBPacket *o_usb_pkt = g_malloc0(usb_pkt_len);
        MCTPPacket *o_mctp_pkt = &o_usb_pkt->mctp;
        MCTPControlMessage *o_mctp_ctrl =
            (MCTPControlMessage *)(o_mctp_pkt->payload);
        MCTPControlGetUUIDRsp *rsp = (MCTPControlGetUUIDRsp *)o_mctp_ctrl->data;

        o_mctp_ctrl->type = 0;
        o_mctp_ctrl->command_code = ctrlmsg->command_code;
        o_mctp_ctrl->flags = ctrlmsg->flags &
            ~(MCTP_CONTROL_FLAGS_RQ | MCTP_CONTROL_FLAGS_D);
        *rsp = (MCTPControlGetUUIDRsp) {
            .completion_code = MCTP_CONTROL_CC_SUCCESS,
            /* TODO: Fill in uuid */
        };
        usb_mctp_fill_common(o_usb_pkt, s->pack, usb_pkt_len);

        return o_usb_pkt;
    }
    case MCTP_CONTROL_GET_MESSAGE_TYPE_SUPPORT: {
        const uint8_t types[] = { MCTP_MT_CXL_FMAPI, MCTP_MT_CXL_TYPE3, };
        uint8_t usb_pkt_len = sizeof(MCTPUSBPacket) +
            sizeof(MCTPControlMessage) +
            sizeof(MCTPControlGetMessageTypeRsp) +
            sizeof(types);
        MCTPUSBPacket *o_usb_pkt = g_malloc0(usb_pkt_len);
        MCTPPacket *o_mctp_pkt = &o_usb_pkt->mctp;
        MCTPControlMessage *o_mctp_ctrl =
            (MCTPControlMessage *)(o_mctp_pkt->payload);
        MCTPControlGetMessageTypeRsp *rsp =
            (MCTPControlGetMessageTypeRsp *)o_mctp_ctrl->data;

        o_mctp_ctrl->type = 0;
        o_mctp_ctrl->command_code = ctrlmsg->command_code;
        o_mctp_ctrl->flags = ctrlmsg->flags &
            ~(MCTP_CONTROL_FLAGS_RQ | MCTP_CONTROL_FLAGS_D);
        *rsp = (MCTPControlGetMessageTypeRsp) {
            .completion_code = MCTP_CONTROL_CC_SUCCESS,
            .message_type_count = sizeof(types),
        };
        memcpy(rsp->types, types, sizeof(types));

        usb_mctp_fill_common(o_usb_pkt, s->pack, usb_pkt_len);

        return o_usb_pkt;
    }
    default: {
        uint8_t usb_pkt_len = sizeof(MCTPUSBPacket) +
            sizeof(MCTPControlMessage) +
            sizeof(MCTPControlErrRsp);
        MCTPUSBPacket *o_usb_pkt = g_malloc0(usb_pkt_len);
        MCTPPacket *o_mctp_pkt = &o_usb_pkt->mctp;
        MCTPControlMessage *o_mctp_ctrl =
            (MCTPControlMessage *)(o_mctp_pkt->payload);
        MCTPControlErrRsp *rsp = (MCTPControlErrRsp *)o_mctp_ctrl->data;

        o_mctp_ctrl->type = 0;
        o_mctp_ctrl->command_code = ctrlmsg->command_code;
        o_mctp_ctrl->flags = ctrlmsg->flags &
            ~(MCTP_CONTROL_FLAGS_RQ | MCTP_CONTROL_FLAGS_D);
        *rsp = (MCTPControlErrRsp) {
            .completion_code = MCTP_CONTROL_CC_ERROR_UNSUP_COMMAND,
        };
        usb_mctp_fill_common(o_usb_pkt, s->pack, usb_pkt_len);
        return o_usb_pkt;
    }
    }
}

static void usb_cxl_mctp_handle_data(USBDevice *dev, USBPacket *p)
{
    USBCXLMCTPState *s = USB_CXL_MCTP(dev);
    USBPacket *tohost, *fromhost;
    uint8_t message_type;
    bool som, eom;

    /*
     * In and out on EP 0x1: anything else is bug.
     * Not sure what right answer is.
     */
    if (p->ep->nr != 1) {
        p->status = USB_RET_STALL;
        return;
    }

    /*
     * Conservative approach - don't proceed until we have at least one packet
     * in each direction. For fragmented messages cases we only need this to be
     * true for the EOM packet (potential optimization).
     */
    if (p->pid == USB_TOKEN_IN) { /* Direction from point of view of host */
        tohost = p;
        if (s->cached_fromhost == NULL) {
            s->cached_tohost = tohost;
            tohost->status = USB_RET_ASYNC;
            return;
        }
        fromhost = s->cached_fromhost;
    } else {
        fromhost = p;
        if (s->cached_tohost == NULL) {
            s->cached_fromhost = fromhost;
            fromhost->status = USB_RET_ASYNC;
            return;
        }
        tohost = s->cached_tohost;
    }

    /* DSP0236 1.3.3 section 8.7 Dropped packets, physical layer errors */
    if (fromhost->iov.size < sizeof(s->pack->mctp)) {
        goto err_drop;
    }

    usb_packet_copy(fromhost, s->pack0, fromhost->iov.size);
    /* DSP0236 1.3.3 section 8.7 Dropped packets, physical layer errors */
    if ((be16_to_cpu(s->pack0->dmtf_id) != 0x1AB4) ||
        (fromhost->iov.size != s->pack0->length)) {
        goto err_drop;
    }

    eom = FIELD_EX8(s->pack0->mctp.hdr.flags, MCTP_H_FLAGS, EOM);
    som = FIELD_EX8(s->pack0->mctp.hdr.flags, MCTP_H_FLAGS, SOM);

    /* DSP0236 1.3.3 section 8.7 Dropped packets, unexpected middle or end */
    if (!som && !s->building_input) {
        goto err_drop;
    }

    if (som) {
        /* Note repeated SOM without EOM is not an error */
        s->building_input = true;
        /* Put first part of full message in place */
        memcpy(s->pack, s->pack0, s->pack0->length);
    } else {
        size_t additional_len;

        additional_len = s->pack0->length - 8;
        memcpy((void *)(s->pack) + s->pack->length, s->pack0->mctp.payload,
               additional_len);
        s->pack->length += additional_len;
    }

    if (eom) {
        s->building_input = false;
    } else { /* More to come so complete message from host to let it flow */
        if (p->pid == USB_TOKEN_IN) {
            /* Hold the tohost packet */
            p->status = USB_RET_ASYNC;
            s->cached_tohost = p;
            /* Release the fromhost packet */
            fromhost->status = USB_RET_SUCCESS;
            s->cached_fromhost = NULL;
            usb_packet_complete(dev, fromhost);
        } else {
            /* From host - handled synchronously */
            p->status = USB_RET_SUCCESS;
        }
        return;
    }

    /* DSP0236 1.3.3 section 8.7 Dropped packets, bad header version */
    if (s->pack->mctp.hdr.version != 1) {
        goto err_drop;
    }

    /* DSP0236 1.3.3 section 8.7 Dropped packets, unknown eid */
    if ((s->pack->mctp.hdr.eid.dest != s->my_eid) &&
        (s->pack->mctp.hdr.eid.dest != 0)) {
        goto err_drop;
    }

    message_type = s->pack->mctp.payload[0];
    switch (message_type) {
    case MCTP_MESSAGE_TYPE_CONTROL: {
        MCTPUSBPacket *o_usb_pkt;
        MCTPControlMessage *ctrlmsg =
            (MCTPControlMessage *)(s->pack->mctp.payload);

        /* DSP0236 1.3.3 section 8.7 Dropped packets, physical layer errors */
        if (fromhost->iov.size < sizeof(s->pack->mctp) + sizeof(*ctrlmsg)) {
            goto err_drop;
        }

        o_usb_pkt = usb_mctp_handle_control(s, ctrlmsg, tohost);

        usb_packet_copy(tohost, o_usb_pkt, o_usb_pkt->length);
        g_free(o_usb_pkt);

        break;
    }
    case MCTP_MT_CXL_TYPE3:
    case MCTP_MT_CXL_FMAPI: {
        size_t usb_pkt_len = MCTPUSBCXL_MAX_SIZE;
        g_autofree MCTPUSBPacket *o_usb_pkt = g_malloc0(usb_pkt_len);
        MCTPPacket *o_mctp_pkt = &o_usb_pkt->mctp;
        CXLMCTPMessage *rsp = (CXLMCTPMessage *)(o_mctp_pkt->payload);
        CXLMCTPMessage *req = (CXLMCTPMessage *)(s->pack->mctp.payload);
        bool bg_started;
        size_t len_out = 0;
        size_t len_in;
        int rc;

        *rsp = (CXLMCTPMessage) {
            .message_type = req->message_type,
            .category = 1,
            .tag = req->tag,
            .command = req->command,
            .command_set = req->command_set,
        };

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
        if (!(req->message_type == MCTP_MT_CXL_TYPE3 &&
              req->command_set < 0x51) &&
            !(req->message_type == MCTP_MT_CXL_FMAPI &&
              req->command_set >= 0x51 && req->command_set < 0x56)) {
            len_out = 0;
            usb_pkt_len = sizeof(MCTPUSBPacket) + sizeof(CXLMCTPMessage) +
                len_out;
            rsp->rc = CXL_MBOX_UNSUPPORTED;
            st24_le_p(rsp->pl_length, len_out);

            usb_mctp_fill_common(o_usb_pkt, s->pack, usb_pkt_len);
            usb_packet_copy(tohost, o_usb_pkt, usb_pkt_len);
            break;
        }
        /* TODO: add ld24_le_p */
        len_in = req->pl_length[2] << 16 | req->pl_length[1] << 8 |
            req->pl_length[0];
        rc = cxl_process_cci_message(s->cci, req->command_set,
                                     req->command,
                                     len_in, req->payload,
                                     &len_out,
                                     (uint8_t *)(rsp + 1),
                                     &bg_started);
        rsp->rc = rc;
        st24_le_p(rsp->pl_length, len_out);
        usb_pkt_len = sizeof(MCTPUSBPacket) + sizeof(CXLMCTPMessage) +
            len_out;

        usb_mctp_fill_common(o_usb_pkt, s->pack, usb_pkt_len);
        usb_packet_copy(tohost, o_usb_pkt, usb_pkt_len);
        break;
    }
    default:
        /*
         * 8.9 Dropped messages - message type unsuported.
         * Dropping after assembly
         */
        goto err_drop;
    }
    /* Something to send */
    tohost->status = USB_RET_SUCCESS;
    fromhost->status = USB_RET_SUCCESS;
    if (p->pid == USB_TOKEN_IN) {
        s->cached_fromhost = NULL;
        usb_packet_complete(dev, fromhost);
    } else {
        s->cached_tohost = NULL;
        usb_packet_complete(dev, tohost);
    }
    return;

err_drop:
    /* Reply with 'nothing' as dropping packet */
    fromhost->status = USB_RET_SUCCESS;
    if (p->pid == USB_TOKEN_IN) {
        /* Hold the tohost packet */
        tohost->status = USB_RET_ASYNC;
        s->cached_tohost = p;
        s->cached_fromhost = NULL;
        usb_packet_complete(dev, fromhost);
    }
}

static void usb_cxl_mctp_realize(USBDevice *dev, Error **errp)
{
    USBCXLMCTPState *s = USB_CXL_MCTP(dev);

    s->pack = g_malloc0(MCTPUSBCXL_MAX_SIZE);
    s->pack0 = g_malloc0(MCTPUSBCXL_MAX_SIZE);
    usb_desc_create_serial(dev);
    usb_desc_init(dev);

    /* Check this is a type we support */
    if (object_dynamic_cast(OBJECT(s->target), TYPE_CXL_USP)) {
        CXLUpstreamPort *usp = CXL_USP(s->target);

        s->type = cxl_switch;
        s->cci = &usp->mctpcci;

        cxl_initialize_usp_mctpcci(s->cci, DEVICE(s->target), DEVICE(dev),
                                   MCTP_CXL_MAILBOX_BYTES);

        return;
    }

    if (object_dynamic_cast(OBJECT(s->target), TYPE_CXL_TYPE3)) {
        CXLType3Dev *ct3d = CXL_TYPE3(s->target);

        s->type = cxl_type3;
        s->cci = &ct3d->oob_mctp_cci;

        cxl_initialize_t3_fm_owned_ld_mctpcci(s->cci, DEVICE(s->target),
                                              DEVICE(dev),
                                              MCTP_CXL_MAILBOX_BYTES);
        return;
    }

    error_setg(errp, "Unhandled target type for CXL MCTP EP");
}

static const Property usb_cxl_mctp_properties[] = {
    DEFINE_PROP_LINK("target", USBCXLMCTPState, target, TYPE_PCI_DEVICE,
                     PCIDevice *),
};

static void usb_cxl_mctp_class_initfn(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->realize = usb_cxl_mctp_realize;
    uc->product_desc = "QEMU USB CXL MCTP";
    uc->usb_desc = &desc;
    uc->handle_attach = usb_desc_attach;
    uc->handle_reset = usb_cxl_mctp_handle_reset;
    uc->handle_control = usb_cxl_mctp_handle_control;
    uc->handle_data = usb_cxl_mctp_handle_data;
    dc->desc = "USB CXL MCTP device";
    dc->fw_name = "mctp";
    device_class_set_props(dc, usb_cxl_mctp_properties);
}

static const TypeInfo usb_cxl_mctp_info = {
    .name = TYPE_USB_CXL_MCTP,
    .parent = TYPE_USB_DEVICE,
    .instance_size = sizeof(USBCXLMCTPState),
    .class_init = usb_cxl_mctp_class_initfn,
};

static void usb_cxl_mctp_register_types(void)
{
    type_register_static(&usb_cxl_mctp_info);
}
type_init(usb_cxl_mctp_register_types)
