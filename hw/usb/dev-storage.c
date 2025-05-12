/*
 * USB Mass Storage Device emulation
 *
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the LGPL.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "hw/usb.h"
#include "hw/usb/msd.h"
#include "desc.h"
#include "hw/qdev-properties.h"
#include "hw/scsi/scsi.h"
#include "migration/vmstate.h"
#include "qemu/cutils.h"
#include "qom/object.h"
#include "trace.h"

/* USB requests.  */
#define MassStorageReset  0xff
#define GetMaxLun         0xfe

struct usb_msd_cbw {
    uint32_t sig;
    uint32_t tag;
    uint32_t data_len;
    uint8_t flags;
    uint8_t lun;
    uint8_t cmd_len;
    uint8_t cmd[16];
} __attribute__((packed));

enum {
    STR_MANUFACTURER = 1,
    STR_PRODUCT,
    STR_SERIALNUMBER,
    STR_CONFIG_FULL,
    STR_CONFIG_HIGH,
    STR_CONFIG_SUPER,
};

static const USBDescStrings desc_strings = {
    [STR_MANUFACTURER] = "QEMU",
    [STR_PRODUCT]      = "QEMU USB HARDDRIVE",
    [STR_SERIALNUMBER] = "1",
    [STR_CONFIG_FULL]  = "Full speed config (usb 1.1)",
    [STR_CONFIG_HIGH]  = "High speed config (usb 2.0)",
    [STR_CONFIG_SUPER] = "Super speed config (usb 3.0)",
};

static const USBDescIface desc_iface_full = {
    .bInterfaceNumber              = 0,
    .bNumEndpoints                 = 2,
    .bInterfaceClass               = USB_CLASS_MASS_STORAGE,
    .bInterfaceSubClass            = 0x06, /* SCSI */
    .bInterfaceProtocol            = 0x50, /* Bulk */
    .eps = (USBDescEndpoint[]) {
        {
            .bEndpointAddress      = USB_DIR_IN | 0x01,
            .bmAttributes          = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize        = 64,
        },{
            .bEndpointAddress      = USB_DIR_OUT | 0x02,
            .bmAttributes          = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize        = 64,
        },
    }
};

static const USBDescDevice desc_device_full = {
    .bcdUSB                        = 0x0200,
    .bMaxPacketSize0               = 8,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 1,
            .bConfigurationValue   = 1,
            .iConfiguration        = STR_CONFIG_FULL,
            .bmAttributes          = USB_CFG_ATT_ONE | USB_CFG_ATT_SELFPOWER,
            .nif = 1,
            .ifs = &desc_iface_full,
        },
    },
};

static const USBDescIface desc_iface_high = {
    .bInterfaceNumber              = 0,
    .bNumEndpoints                 = 2,
    .bInterfaceClass               = USB_CLASS_MASS_STORAGE,
    .bInterfaceSubClass            = 0x06, /* SCSI */
    .bInterfaceProtocol            = 0x50, /* Bulk */
    .eps = (USBDescEndpoint[]) {
        {
            .bEndpointAddress      = USB_DIR_IN | 0x01,
            .bmAttributes          = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize        = 512,
        },{
            .bEndpointAddress      = USB_DIR_OUT | 0x02,
            .bmAttributes          = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize        = 512,
        },
    }
};

static const USBDescDevice desc_device_high = {
    .bcdUSB                        = 0x0200,
    .bMaxPacketSize0               = 64,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 1,
            .bConfigurationValue   = 1,
            .iConfiguration        = STR_CONFIG_HIGH,
            .bmAttributes          = USB_CFG_ATT_ONE | USB_CFG_ATT_SELFPOWER,
            .nif = 1,
            .ifs = &desc_iface_high,
        },
    },
};

static const USBDescIface desc_iface_super = {
    .bInterfaceNumber              = 0,
    .bNumEndpoints                 = 2,
    .bInterfaceClass               = USB_CLASS_MASS_STORAGE,
    .bInterfaceSubClass            = 0x06, /* SCSI */
    .bInterfaceProtocol            = 0x50, /* Bulk */
    .eps = (USBDescEndpoint[]) {
        {
            .bEndpointAddress      = USB_DIR_IN | 0x01,
            .bmAttributes          = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize        = 1024,
            .bMaxBurst             = 15,
        },{
            .bEndpointAddress      = USB_DIR_OUT | 0x02,
            .bmAttributes          = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize        = 1024,
            .bMaxBurst             = 15,
        },
    }
};

static const USBDescDevice desc_device_super = {
    .bcdUSB                        = 0x0300,
    .bMaxPacketSize0               = 9,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 1,
            .bConfigurationValue   = 1,
            .iConfiguration        = STR_CONFIG_SUPER,
            .bmAttributes          = USB_CFG_ATT_ONE | USB_CFG_ATT_SELFPOWER,
            .nif = 1,
            .ifs = &desc_iface_super,
        },
    },
};

static const USBDesc desc = {
    .id = {
        .idVendor          = 0x46f4, /* CRC16() of "QEMU" */
        .idProduct         = 0x0001,
        .bcdDevice         = 0,
        .iManufacturer     = STR_MANUFACTURER,
        .iProduct          = STR_PRODUCT,
        .iSerialNumber     = STR_SERIALNUMBER,
    },
    .full  = &desc_device_full,
    .high  = &desc_device_high,
    .super = &desc_device_super,
    .str   = desc_strings,
};

static void usb_msd_data_packet_complete(MSDState *s, int status)
{
    USBPacket *p = s->data_packet;

    /*
     * Set s->data_packet to NULL before calling usb_packet_complete
     * because another request may be issued before usb_packet_complete
     * returns.
     */
    trace_usb_msd_packet_complete();
    s->data_packet = NULL;
    p->status = status;
    usb_packet_complete(&s->dev, p);
}

static void usb_msd_csw_packet_complete(MSDState *s, int status)
{
    USBPacket *p = s->csw_in_packet;

    /*
     * Set s->csw_in_packet to NULL before calling usb_packet_complete
     * because another request may be issued before usb_packet_complete
     * returns.
     */
    trace_usb_msd_packet_complete();
    s->csw_in_packet = NULL;
    p->status = status;
    usb_packet_complete(&s->dev, p);
}

static void usb_msd_fatal_error(MSDState *s)
{
    trace_usb_msd_fatal_error();

    if (s->data_packet) {
        usb_msd_data_packet_complete(s, USB_RET_STALL);
    }

    if (s->csw_in_packet) {
        usb_msd_csw_packet_complete(s, USB_RET_STALL);
    }

    /*
     * Guest messed up up device state with illegal requests.  Go
     * ignore any requests until the guests resets the device (and
     * brings it into a known state that way).
     */
    s->needs_reset = true;
}

static void usb_msd_copy_data(MSDState *s, USBPacket *p)
{
    uint32_t len;
    len = p->iov.size - p->actual_length;
    if (len > s->scsi_len)
        len = s->scsi_len;
    usb_packet_copy(p, scsi_req_get_buf(s->req) + s->scsi_off, len);
    s->scsi_len -= len;
    s->scsi_off += len;
    if (len > s->data_len) {
        len = s->data_len;
    }
    s->data_len -= len;
    if (s->scsi_len == 0 || s->data_len == 0) {
        scsi_req_continue(s->req);
    }
}

static void usb_msd_send_status(MSDState *s, USBPacket *p)
{
    int len;

    trace_usb_msd_send_status(s->csw.status, le32_to_cpu(s->csw.tag),
                              p->iov.size);

    assert(s->csw.sig == cpu_to_le32(0x53425355));
    len = MIN(sizeof(s->csw), p->iov.size);
    usb_packet_copy(p, &s->csw, len);
    memset(&s->csw, 0, sizeof(s->csw));
}

void usb_msd_transfer_data(SCSIRequest *req, uint32_t len)
{
    MSDState *s = DO_UPCAST(MSDState, dev.qdev, req->bus->qbus.parent);
    USBPacket *p = s->data_packet;

    if (s->cbw_type == USB_MSD_CBW_DATAIN) {
        if (req->cmd.mode == SCSI_XFER_TO_DEV) {
            usb_msd_fatal_error(s);
            return;
        }
    } else if (s->cbw_type == USB_MSD_CBW_DATAOUT) {
        if (req->cmd.mode != SCSI_XFER_TO_DEV) {
            usb_msd_fatal_error(s);
            return;
        }
    } else {
        g_assert_not_reached();
    }

    assert(s->scsi_len == 0);
    s->scsi_len = len;
    s->scsi_off = 0;

    if (p) {
        usb_msd_copy_data(s, p);
        p = s->data_packet;
        if (p && p->actual_length == p->iov.size) {
            /* USB_RET_SUCCESS status clears previous ASYNC status */
            usb_msd_data_packet_complete(s, USB_RET_SUCCESS);
        }
    }
}

void usb_msd_command_complete(SCSIRequest *req, size_t resid)
{
    MSDState *s = DO_UPCAST(MSDState, dev.qdev, req->bus->qbus.parent);
    USBPacket *p = s->data_packet;

    trace_usb_msd_cmd_complete(req->status, req->tag);

    g_assert(s->req);
    /* The CBW is what starts the SCSI request */
    g_assert(s->cbw_type != 0);
    g_assert(!s->cmd_done);

    if (s->cbw_type == USB_MSD_CBW_ZERO) {
        g_assert(!p);
    }

    s->csw.sig = cpu_to_le32(0x53425355);
    s->csw.tag = cpu_to_le32(req->tag);
    s->csw.residue = cpu_to_le32(s->data_len);
    s->csw.status = req->status != 0;

    /* Set cmd_done req to NULL before completing packet because another
     * packet may come in after that */
    s->cmd_done = true;
    scsi_req_unref(req);
    s->req = NULL;

    if (p) {
        if (s->data_len) {
            int len = (p->iov.size - p->actual_length);
            usb_packet_skip(p, len);
            if (len > s->data_len) {
                len = s->data_len;
            }
            s->data_len -= len;
        }
        /* USB_RET_SUCCESS status clears previous ASYNC status */
        usb_msd_data_packet_complete(s, USB_RET_SUCCESS);
    }

    if (s->csw_in_packet) {
        g_assert(s->csw_in_packet);
        usb_msd_send_status(s, s->csw_in_packet);
        usb_msd_csw_packet_complete(s, USB_RET_SUCCESS);
        s->cbw_type = USB_MSD_CBW_NONE;
        s->cmd_done = false;
        s->ready = true;
    }
}

void usb_msd_request_cancelled(SCSIRequest *req)
{
    MSDState *s = DO_UPCAST(MSDState, dev.qdev, req->bus->qbus.parent);

    trace_usb_msd_cmd_cancel(req->tag);

    if (req == s->req) {
        s->csw.sig = cpu_to_le32(0x53425355);
        s->csw.tag = cpu_to_le32(req->tag);
        s->csw.status = 1; /* error */

        s->scsi_len = 0;
        s->cmd_done = true;
        scsi_req_unref(s->req);
        s->req = NULL;
    }
}

void usb_msd_handle_reset(USBDevice *dev)
{
    MSDState *s = (MSDState *)dev;

    trace_usb_msd_reset();
    if (s->req) {
        scsi_req_cancel(s->req);
    }
    assert(s->req == NULL);

    if (s->data_packet) {
        usb_msd_data_packet_complete(s, USB_RET_STALL);
    }

    if (s->csw_in_packet) {
        usb_msd_csw_packet_complete(s, USB_RET_STALL);
    }

    s->unknown_in_packet = NULL;
    memset(&s->csw, 0, sizeof(s->csw));
    s->cbw_type = USB_MSD_CBW_NONE;
    s->cmd_done = false;
    s->ready = true;

    s->needs_reset = false;
}

static void usb_msd_handle_control(USBDevice *dev, USBPacket *p,
               int request, int value, int index, int length, uint8_t *data)
{
    MSDState *s = (MSDState *)dev;
    SCSIDevice *scsi_dev;
    int ret, maxlun;

    ret = usb_desc_handle_control(dev, p, request, value, index, length, data);
    if (ret >= 0) {
        return;
    }

    switch (request) {
    case EndpointOutRequest | USB_REQ_CLEAR_FEATURE:
        /* XXX clear halt not implemented */
        break;
        /* Class specific requests.  */
    case ClassInterfaceOutRequest | MassStorageReset:
        /* Reset state ready for the next CBW.  */
        usb_msd_handle_reset(dev);
        break;
    case ClassInterfaceRequest | GetMaxLun:
        maxlun = 0;
        for (;;) {
            scsi_dev = scsi_device_find(&s->bus, 0, 0, maxlun+1);
            if (scsi_dev == NULL) {
                break;
            }
            if (scsi_dev->lun != maxlun+1) {
                break;
            }
            maxlun++;
        }
        trace_usb_msd_maxlun(maxlun);
        data[0] = maxlun;
        p->actual_length = 1;
        break;
    default:
        p->status = USB_RET_STALL;
        break;
    }
}

static void usb_msd_cancel_io(USBDevice *dev, USBPacket *p)
{
    MSDState *s = USB_STORAGE_DEV(dev);

    if (p == s->data_packet) {
        s->data_packet = NULL;
        if (s->req) {
            scsi_req_cancel(s->req);
        }
    } else if (p == s->csw_in_packet) {
        s->csw_in_packet = NULL;
    } else if (p == s->unknown_in_packet) {
        s->unknown_in_packet = NULL;
    } else {
        g_assert_not_reached();
    }
}

static void usb_msd_handle_data_out(USBDevice *dev, USBPacket *p)
{
    MSDState *s = (MSDState *)dev;

    if (!s->cbw_type) {
        uint32_t tag;
        struct usb_msd_cbw cbw;
        SCSIDevice *scsi_dev;
        int len;

        if (p->iov.size != 31) {
            error_report("usb-msd: Bad CBW size %ld", p->iov.size);
            goto fail;
        }
        usb_packet_copy(p, &cbw, 31);
        if (le32_to_cpu(cbw.sig) != 0x43425355) {
            error_report("usb-msd: Bad signature %08x",
                         le32_to_cpu(cbw.sig));
            goto fail;
        }
        scsi_dev = scsi_device_find(&s->bus, 0, 0, cbw.lun);
        if (scsi_dev == NULL) {
            error_report("usb-msd: Bad LUN %d", cbw.lun);
            goto fail;
        }

        tag = le32_to_cpu(cbw.tag);
        s->data_len = le32_to_cpu(cbw.data_len);

        s->ready = false;
        if ((cbw.flags & 0x80) && s->data_len > 0) {
            s->cbw_type = USB_MSD_CBW_DATAIN;
            if (s->unknown_in_packet) {
                /* In packet must be be DATAIN */
                g_assert(!s->data_packet);
                s->data_packet = s->unknown_in_packet;
                s->unknown_in_packet = NULL;
            }

        } else {
            if (s->data_len == 0) {
                s->cbw_type = USB_MSD_CBW_ZERO;
            } else {
                s->cbw_type = USB_MSD_CBW_DATAOUT;
            }
            if (s->unknown_in_packet) {
                /* In packet must be the CSW */
                if (s->unknown_in_packet->iov.size != 13) {
                    /*
                     * Not strictly within spec to reject size > 13? But
                     * could catch some guest errors.
                     */
                    error_report("usb-msd: CSW size %zd != 13\n",
                                 s->unknown_in_packet->iov.size);
                    goto fail;
                }

                /* In packet must be be CSW */
                s->csw_in_packet = s->unknown_in_packet;
                s->unknown_in_packet = NULL;
            }
        }
        trace_usb_msd_cmd_submit(cbw.lun, tag, cbw.flags,
                                 cbw.cmd_len, s->data_len);
        assert(le32_to_cpu(s->csw.residue) == 0);
        assert(s->scsi_len == 0);
        s->req = scsi_req_new(scsi_dev, tag, cbw.lun, cbw.cmd,
                              cbw.cmd_len, NULL);
        if (s->commandlog) {
            scsi_req_print(s->req);
        }
        len = scsi_req_enqueue(s->req);
        if (len) {
            scsi_req_continue(s->req);
        }

    } else if (s->cbw_type == USB_MSD_CBW_DATAOUT) {
        trace_usb_msd_data_out(p->iov.size, s->data_len);
        if (s->data_packet) {
            error_report("usb-msd: dataout packet while dataout in progress");
            goto fail;
        }
        if (s->unknown_in_packet) {
            error_report("usb-msd: dataout packet with unknown in packet");
            goto fail;
        }
        if (p->iov.size > s->data_len) {
            error_report("usb-msd: dataout packet too large for CBW");
            goto fail;
        }

        if (s->scsi_len) {
            usb_msd_copy_data(s, p);
        }

        if (s->cmd_done) {
            int len;

            g_assert(!s->req);
            len = p->iov.size - p->actual_length;
            if (len) {
                usb_packet_skip(p, len);
                if (len > s->data_len) {
                    len = s->data_len;
                }
                s->data_len -= len;
            }
            if (s->csw_in_packet) {
                usb_msd_send_status(s, s->csw_in_packet);
                usb_msd_csw_packet_complete(s, USB_RET_SUCCESS);
                s->cbw_type = USB_MSD_CBW_NONE;
                s->cmd_done = false;
                s->ready = true;
            }
        } else {
            if (p->actual_length < p->iov.size) {
                trace_usb_msd_packet_async();
                assert(!s->csw_in_packet);
                s->data_packet = p;
                p->status = USB_RET_ASYNC;
            }
        }
    } else { /* USB_MSD_CBW_ZERO or USB_MSD_CBW_DATAIN */
        error_report("usb-msd: unexpected out packet in state %x",
                     s->cbw_type);
fail:
        p->status = USB_RET_STALL;
    }
}

static void usb_msd_handle_data_in(USBDevice *dev, USBPacket *p)
{
    MSDState *s = (MSDState *)dev;

    if (!s->cbw_type) {
        s->ready = false;
        if (s->unknown_in_packet) {
            /*
             * It's unclear from the USB MSD spec if this is permitted, but
             * for now we don't try to handle it.
             */
            error_report("usb-msd: 2nd datain packet before CBW");
            goto fail;
        }
        g_assert(!s->req);
        g_assert(!s->data_packet);
        g_assert(!s->csw_in_packet);
        s->unknown_in_packet = p;
        trace_usb_msd_packet_async();
        p->status = USB_RET_ASYNC;

    } else if (s->cmd_done) {
        g_assert(!s->req);
        if (s->data_len) {
            int len = (p->iov.size - p->actual_length);
            usb_packet_skip(p, len);
            if (len > s->data_len) {
                len = s->data_len;
            }
            s->data_len -= len;
        } else {
            if (p->iov.size != 13) {
                qemu_log("usb-msd: CSD size is %ld (expecting 13)\n", p->iov.size);
                goto fail;
            }

            usb_msd_send_status(s, p);
            s->cbw_type = USB_MSD_CBW_NONE;
            s->cmd_done = false;
            s->ready = true;
        }

    } else if (s->cbw_type == USB_MSD_CBW_DATAIN) {
        trace_usb_msd_data_in(p->iov.size, s->data_len, s->scsi_len);
        if (s->data_packet) {
            error_report("usb-msd: data packet while datain in progress");
            goto fail;
        }
        g_assert(!s->data_packet);
        g_assert(!s->unknown_in_packet);
        g_assert(!s->csw_in_packet);

        if (s->scsi_len) {
            usb_msd_copy_data(s, p);
        }

        if (s->cmd_done) {
            int len;

            g_assert(!s->req);
            len = p->iov.size - p->actual_length;
            if (len) {
                usb_packet_skip(p, len);
                if (len > s->data_len) {
                    len = s->data_len;
                }
                s->data_len -= len;
                g_assert(s->data_len == 0);
            }
            if (p->status == USB_RET_ASYNC) {
                usb_msd_data_packet_complete(s, USB_RET_SUCCESS);
            }
        } else {
            if (p->actual_length < p->iov.size) {
                trace_usb_msd_packet_async();
                s->data_packet = p;
                p->status = USB_RET_ASYNC;
            }
        }
    } else { /* USB_MSD_CBW_ZERO or USB_MSD_CBW_DATAOUT */
        if (s->csw_in_packet) {
            /*
             * It's unclear from the USB MSD spec if this is permitted, but
             * for now we don't try to handle it.
             */
            error_report("usb-msd: 2nd CSW packet");
            goto fail;
        }
        g_assert(!s->unknown_in_packet);
        if (s->cbw_type == USB_MSD_CBW_ZERO) {
            g_assert(s->data_len == 0);
            g_assert(!s->data_packet);
        }
        g_assert(s->req);
        g_assert(!s->unknown_in_packet);

        if (p->iov.size != 13) {
            qemu_log("usb-msd: CSD size is %ld (expecting 13)\n", p->iov.size);
            goto fail;
        }

        /* Waiting for SCSI zero/write to complete.  */
        trace_usb_msd_packet_async();
        s->csw_in_packet = p;
        p->status = USB_RET_ASYNC;
    }
    return;
fail:
    p->status = USB_RET_STALL;
}

static void usb_msd_handle_data(USBDevice *dev, USBPacket *p)
{
    MSDState *s = (MSDState *)dev;
    uint8_t devep = p->ep->nr;

    if (s->needs_reset) {
        p->status = USB_RET_STALL;
        return;
    }

    switch (p->pid) {
    case USB_TOKEN_OUT:
        if (devep != 2) {
            goto fail;
        }
        usb_msd_handle_data_out(dev, p);
        break;

    case USB_TOKEN_IN:
        if (devep != 1) {
            goto fail;
        }
        usb_msd_handle_data_in(dev, p);
        break;

    default:
    fail:
        p->status = USB_RET_STALL;
        break;
    }
}

void *usb_msd_load_request(QEMUFile *f, SCSIRequest *req)
{
    MSDState *s = DO_UPCAST(MSDState, dev.qdev, req->bus->qbus.parent);

    /* nothing to load, just store req in our state struct */
    assert(s->req == NULL);
    scsi_req_ref(req);
    s->req = req;
    return NULL;
}

static const VMStateDescription vmstate_usb_msd = {
    .name = "usb-storage",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_USB_DEVICE(dev, MSDState),
//        VMSTATE_UINT32(state, MSDState),
        VMSTATE_UINT32(scsi_len, MSDState),
        VMSTATE_UINT32(scsi_off, MSDState),
        VMSTATE_UINT32(data_len, MSDState),
        VMSTATE_UINT32(csw.sig, MSDState),
        VMSTATE_UINT32(csw.tag, MSDState),
        VMSTATE_UINT32(csw.residue, MSDState),
        VMSTATE_UINT8(csw.status, MSDState),
        VMSTATE_END_OF_LIST()
    }
};

static void usb_msd_class_initfn_common(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->product_desc   = "QEMU USB MSD";
    uc->usb_desc       = &desc;
    uc->cancel_packet  = usb_msd_cancel_io;
    uc->handle_attach  = usb_desc_attach;
    uc->handle_reset   = usb_msd_handle_reset;
    uc->handle_control = usb_msd_handle_control;
    uc->handle_data    = usb_msd_handle_data;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    dc->fw_name = "storage";
    dc->vmsd = &vmstate_usb_msd;
}

static const TypeInfo usb_storage_dev_type_info = {
    .name = TYPE_USB_STORAGE,
    .parent = TYPE_USB_DEVICE,
    .instance_size = sizeof(MSDState),
    .abstract = true,
    .class_init = usb_msd_class_initfn_common,
};

static void usb_msd_register_types(void)
{
    qemu_build_assert(sizeof(struct usb_msd_cbw) == 31);
    qemu_build_assert(sizeof(struct usb_msd_csw) == 13);

    type_register_static(&usb_storage_dev_type_info);
}

type_init(usb_msd_register_types)
