/*
 * USB Mass Storage Device emulation
 *
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the LGPL.
 */

#include "hw/usb.h"
#include "hw/scsi/scsi.h"

enum USBMSDCBWType {
    USB_MSD_CBW_NONE       = 0,
    USB_MSD_CBW_ZERO,           /* zero-length */
    USB_MSD_CBW_DATAIN,         /* data-in command */
    USB_MSD_CBW_DATAOUT,        /* data-out command */
};

struct QEMU_PACKED usb_msd_csw {
    uint32_t sig;
    uint32_t tag;
    uint32_t residue;
    uint8_t status;
};

struct MSDState {
    USBDevice dev;
    bool ready;
    bool cmd_done;
    enum USBMSDCBWType cbw_type;
    uint32_t scsi_off;
    uint32_t scsi_len;
    uint32_t data_len;
    struct usb_msd_csw csw;
    SCSIRequest *req;
    SCSIBus bus;

    /* For async completion.  */
    USBPacket *data_packet;
    USBPacket *csw_in_packet;
    USBPacket *unknown_in_packet;

    /* usb-storage only */
    BlockConf conf;
    bool removable;
    bool commandlog;
    SCSIDevice *scsi_dev;
    bool needs_reset;
};

typedef struct MSDState MSDState;
#define TYPE_USB_STORAGE "usb-storage-dev"
DECLARE_INSTANCE_CHECKER(MSDState, USB_STORAGE_DEV,
                         TYPE_USB_STORAGE)

void usb_msd_transfer_data(SCSIRequest *req, uint32_t len);
void usb_msd_command_complete(SCSIRequest *req, size_t resid);
void usb_msd_request_cancelled(SCSIRequest *req);
void *usb_msd_load_request(QEMUFile *f, SCSIRequest *req);
void usb_msd_handle_reset(USBDevice *dev);
