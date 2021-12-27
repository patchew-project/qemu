/*
 * UVC Device emulation, base on UVC specification 1.5
 *
 * Copyright 2021 Bytedance, Inc.
 *
 * Authors:
 *   zhenwei pi <pizhenwei@bytedance.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qom/object.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/usb.h"
#include "hw/usb/video.h"
#include "camera/camera.h"
#include "desc.h"
#include "trace.h"

#define USBVIDEO_VENDOR_NUM     0x46f4 /* CRC16() of "QEMU" */
#define USBVIDEO_PRODUCT_NUM    0x0e01

enum usb_video_strings {
    STRING_NULL,
    STRING_MANUFACTURER,
    STRING_PRODUCT,
    STRING_SERIALNUMBER,
    STRING_CONFIG,
    STRING_INTERFACE_ASSOCIATION,
    STRING_VIDEO_CONTROL,
    STRING_INPUT_TERMINAL,
    STRING_SELECTOR_UNIT,
    STRING_PROCESSING_UNIT,
    STRING_OUTPUT_TERMINAL,
    STRING_VIDEO_STREAMING,
    STRING_VIDEO_STREAMING_ALTERNATE1,
};

static const USBDescStrings usb_video_stringtable = {
    [STRING_MANUFACTURER]               = "QEMU",
    [STRING_PRODUCT]                    = "QEMU USB Video",
    [STRING_SERIALNUMBER]               = "1",
    [STRING_CONFIG]                     = "Video Configuration",
    [STRING_INTERFACE_ASSOCIATION]      = "Integrated Camera",
    [STRING_VIDEO_CONTROL]              = "Video Control",
    [STRING_INPUT_TERMINAL]             = "Video Input Terminal",
    [STRING_SELECTOR_UNIT]              = "Video Selector Unit",
    [STRING_PROCESSING_UNIT]            = "Video Processing Unit",
    [STRING_OUTPUT_TERMINAL]            = "Video Output Terminal",
    [STRING_VIDEO_STREAMING]            = "Video Streaming",
    [STRING_VIDEO_STREAMING_ALTERNATE1] = "Video Streaming Alternate Setting 1",
};

/* Interface IDs */
#define IF_CONTROL   0x0
#define IF_STREAMING 0x1

/* Endpoint IDs */
#define EP_CONTROL   0x1
#define EP_STREAMING 0x2

/* Terminal IDs */
#define INPUT_TERMINAL  0x1
#define OUTPUT_TERMINAL 0x3

/* XU IDs */
#define SELECTOR_UNIT   0x4
#define PROCESSING_UNIT 0x5
#define ENCODING_UNIT   0x6

/* Alternate Settings */
#define ALTSET_OFF       0x0
#define ALTSET_STREAMING 0x1

#define U16(x) ((x) & 0xff), (((x) >> 8) & 0xff)
#define U24(x) U16(x), (((x) >> 16) & 0xff)
#define U32(x) U24(x), (((x) >> 24) & 0xff)

/*
 * Note that desc_ifaces works as template, because UVC need to detect
 * format/frame/interval from backend, and built the interfaces dynamically
 */
static const USBDescIface desc_ifaces[] = {
    {
        /* VideoControl Interface Descriptor */
        .bInterfaceNumber              = IF_CONTROL,
        .bNumEndpoints                 = 1,
        .bInterfaceClass               = USB_CLASS_VIDEO,
        .bInterfaceSubClass            = SC_VIDEOCONTROL,
        .bInterfaceProtocol            = PC_PROTOCOL_15,
        .iInterface                    = STRING_VIDEO_CONTROL,
        .ndesc                         = 5,
        .descs = (USBDescOther[]) {
            {
                /* Class-specific VS Interface Input Header Descriptor */
                .data = (uint8_t[]) {
                    0x0d,                    /*  u8  bLength */
                    CS_INTERFACE,            /*  u8  bDescriptorType */
                    VC_HEADER,               /*  u8  bDescriptorSubtype */
                    U16(0x0110),             /* u16  bcdADC */
                    U16(0x3b),               /* u16  wTotalLength */
                    U32(0x005B8D80),         /* u32  dwClockFrequency */
                    0x01,                    /*  u8  bInCollection */
                    0x01,                    /*  u8  baInterfaceNr */
                }
            }, {
                /* Input Terminal Descriptor (Camera) */
                .data = (uint8_t[]) {
                    0x11,                    /*  u8  bLength */
                    CS_INTERFACE,            /*  u8  bDescriptorType */
                    VC_INPUT_TERMINAL,       /*  u8  bDescriptorSubtype */
                    INPUT_TERMINAL,          /*  u8  bTerminalID */
                    U16(ITT_CAMERA),         /* u16  wTerminalType */
                    0x00,                    /*  u8  bAssocTerminal */
                    STRING_INPUT_TERMINAL,   /*  u8  iTerminal */
                    U16(0x0000),             /* u16  wObjectiveFocalLengthMin */
                    U16(0x0000),             /* u16  wObjectiveFocalLengthMax */
                    U16(0x0000),             /* u16  wOcularFocalLength */
                    0x02,                    /*  u8  bControlSize */
                    U16(0x0000),             /* u16  bmControls */
                }
            }, {
                /* Output Terminal Descriptor */
                .data = (uint8_t[]) {
                    0x09,                    /*  u8  bLength */
                    CS_INTERFACE,            /*  u8  bDescriptorType */
                    VC_OUTPUT_TERMINAL,      /*  u8  bDescriptorSubtype */
                    OUTPUT_TERMINAL,         /*  u8  bTerminalID */
                    U16(TT_STREAMING),       /* u16  wTerminalType */
                    0x00,                    /*  u8  bAssocTerminal */
                    PROCESSING_UNIT,         /*  u8  bSourceID */
                    STRING_OUTPUT_TERMINAL,  /*  u8  iTerminal */
                }
            }, {
                /* Selector Unit Descriptor */
                .data = (uint8_t[]) {
                    0x08,                    /*  u8  bLength */
                    CS_INTERFACE,            /*  u8  bDescriptorType */
                    VC_SELECTOR_UNIT,        /*  u8  bDescriptorSubtype */
                    SELECTOR_UNIT,           /*  u8  bUnitID */
                    1,                       /*  u8  bNrInPins */
                    INPUT_TERMINAL,          /*  u8  baSourceID(1) */
                    STRING_SELECTOR_UNIT,    /*  u8  iSelector */
                }
            }, {
                /* Processing Unit Descriptor */
                .data = (uint8_t[]) {
                    0x0d,                    /*  u8  bLength */
                    CS_INTERFACE,            /*  u8  bDescriptorType */
                    VC_PROCESSING_UNIT,      /*  u8  bDescriptorSubtype */
                    PROCESSING_UNIT,         /*  u8  bUnitID */
                    SELECTOR_UNIT,           /*  u8  bSourceID */
                    U16(0x0000),             /* u16  wMaxMultiplier */
                    0x03,                    /*  u8  bControlSize */
                    U24(0x000000),           /* u24  bmControls */
                    STRING_PROCESSING_UNIT,  /*  u8  iProcessing */
                    0x00,                    /*  u8  bmVideoStandards */
                }
            }
        },
        .eps = (USBDescEndpoint[]) {
            {
                /* 3.8.2.1 Standard VC Interrupt Endpoint Descriptor */
                .bEndpointAddress      = USB_DIR_IN | EP_CONTROL,
                .bmAttributes          = USB_ENDPOINT_XFER_INT,
                .wMaxPacketSize        = 0x40,
                .bInterval             = 0x20,
            },
        },
    }, {
        /* VideoStreaming Interface Descriptor */
        .bInterfaceNumber              = IF_STREAMING,
        .bAlternateSetting             = ALTSET_OFF,
        .bNumEndpoints                 = 0,
        .bInterfaceClass               = USB_CLASS_VIDEO,
        .bInterfaceSubClass            = SC_VIDEOSTREAMING,
        .bInterfaceProtocol            = PC_PROTOCOL_15,
        .iInterface                    = STRING_VIDEO_STREAMING,
        /* .ndesc & .descs are built dynamicly during .realize */
    }, {
        /* Operational Alternate Setting 1 */
        .bInterfaceNumber              = IF_STREAMING,
        .bAlternateSetting             = ALTSET_STREAMING,
        .bNumEndpoints                 = 1,
        .bInterfaceClass               = USB_CLASS_VIDEO,
        .bInterfaceSubClass            = SC_VIDEOSTREAMING,
        .bInterfaceProtocol            = PC_PROTOCOL_15,
        .iInterface                    = STRING_VIDEO_STREAMING_ALTERNATE1,
        .eps = (USBDescEndpoint[]) {
            {
                /*
                 * 3.10.1.1 Standard VS Isochronous Video Data Endpoint
                 * Descriptor
                 */
                .bEndpointAddress      = USB_DIR_IN | EP_STREAMING,
                .bmAttributes          = 0x05, /* TODO define BITs USB 9.6.6 */
                .wMaxPacketSize        = 1024,
                .bInterval             = 0x1,
            },
        },
    }
};

static const USBDescIfaceAssoc desc_if_groups[] = {
    {
        .bFirstInterface = IF_CONTROL,
        .bInterfaceCount = 2,
        .bFunctionClass = USB_CLASS_VIDEO,
        .bFunctionSubClass = SC_VIDEO_INTERFACE_COLLECTION,
        .bFunctionProtocol = PC_PROTOCOL_UNDEFINED,
        .iFunction = STRING_INTERFACE_ASSOCIATION,
    },
};

static const USBDescDevice desc_device_full = {
    .bcdUSB                        = 0x0100,
    .bDeviceClass                  = USB_CLASS_MISCELLANEOUS,
    .bDeviceSubClass               = 2,
    .bDeviceProtocol               = 1, /* Interface Association */
    .bMaxPacketSize0               = 8,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 2,
            .bConfigurationValue   = 1,
            .iConfiguration        = STRING_CONFIG,
            .bmAttributes          = USB_CFG_ATT_ONE | USB_CFG_ATT_SELFPOWER,
            .bMaxPower             = 0x32,
            .nif_groups            = ARRAY_SIZE(desc_if_groups),
            .if_groups             = desc_if_groups,
            .nif                   = ARRAY_SIZE(desc_ifaces),
            .ifs                   = desc_ifaces,
        },
    },
};

static const USBDescDevice desc_device_high = {
    .bcdUSB                        = 0x0200,
    .bDeviceClass                  = USB_CLASS_MISCELLANEOUS,
    .bDeviceSubClass               = 2,
    .bDeviceProtocol               = 1, /* Interface Association */
    .bMaxPacketSize0               = 64,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 2,
            .bConfigurationValue   = 1,
            .iConfiguration        = STRING_CONFIG,
            .bmAttributes          = USB_CFG_ATT_ONE | USB_CFG_ATT_SELFPOWER,
            .bMaxPower             = 0x32,
            .nif_groups            = ARRAY_SIZE(desc_if_groups),
            .if_groups             = desc_if_groups,
            .nif = ARRAY_SIZE(desc_ifaces),
            .ifs = desc_ifaces,
        },
    },
};

static const USBDesc desc_video = {
    .id = {
        .idVendor          = USBVIDEO_VENDOR_NUM,
        .idProduct         = USBVIDEO_PRODUCT_NUM,
        .bcdDevice         = 0,
        .iManufacturer     = STRING_MANUFACTURER,
        .iProduct          = STRING_PRODUCT,
        .iSerialNumber     = STRING_SERIALNUMBER,
    },
    .full = &desc_device_full,
    .high = &desc_device_high,
    .str  = usb_video_stringtable,
};

enum AttributeIndex {
    ATTRIBUTE_DEF,
    ATTRIBUTE_MIN,
    ATTRIBUTE_MAX,
    ATTRIBUTE_CUR,
    ATTRIBUTE_RES,

    ATTRIBUTE_ALL
};

typedef struct USBVideoControlStats {
    VideoControlStatus status;
    uint8_t size; /* value size in bytes */
    QTAILQ_ENTRY(USBVideoControlStats) list;
} USBVideoControlStats;

typedef struct USBVideoControlInfo {
    uint8_t selector;
    uint8_t caps;
    uint8_t size;
    uint32_t value[ATTRIBUTE_ALL]; /* store in le32 */
} USBVideoControlInfo;

struct USBVideoState {
    /* qemu interfaces */
    USBDevice dev;

    /* state */
    QEMUCamera *camera;
    USBDesc desc_video;
    USBDescDevice desc_device_full;
    USBDescDevice desc_device_high;
    USBDescIface desc_ifaces[ARRAY_SIZE(desc_ifaces)];
    USBDescOther *vs_descs;
    uint8_t n_vs_descs;
    uint8_t *vs_data;

    /* UVC control */
    int streaming_altset;
    bool fid;
    uint8_t error;
    QTAILQ_HEAD(, USBVideoControlStats) control_status;

    /* video control attributes */
    USBVideoControlInfo pu_attrs[PU_MAX];

    /* video streaming control attributes, vsc_attrs in little endian */
    uint8_t vsc_info;
    uint16_t vsc_len;
    VideoStreamingControl vsc_attrs[ATTRIBUTE_ALL];

    /* properties */
    char *cameradev;
    char *terminal;
};

static int usb_video_pu_control_bits(QEMUCameraControlType type)
{
    switch ((int)type) {
    case QEMUCameraBrightness:
        return PU_CONTRL_BRIGHTNESS;
    case QEMUCameraContrast:
        return PU_CONTRL_CONTRAST;
    case QEMUCameraGain:
        return PU_CONTRL_GAIN;
    case QEMUCameraGamma:
        return PU_CONTRL_GAMMA;
    case QEMUCameraHue:
        return PU_CONTRL_HUE;
    case QEMUCameraHueAuto:
        return PU_CONTRL_HUE_AUTO;
    case QEMUCameraSaturation:
        return PU_CONTRL_SATURATION;
    case QEMUCameraSharpness:
        return PU_CONTRL_SHARPNESS;
    case QEMUCameraWhiteBalanceTemperature:
        return PU_CONTRL_WHITE_BALANCE_TEMPERATURE;
    }

    return 0;
}

static int usb_video_pu_control_type(QEMUCameraControlType type, uint8_t *size)
{
    switch ((int)type) {
    case QEMUCameraBrightness:
        *size = 2;
        return PU_BRIGHTNESS_CONTROL;
    case QEMUCameraContrast:
        *size = 2;
        return PU_CONTRAST_CONTROL;
    case QEMUCameraGain:
        *size = 2;
        return PU_GAIN_CONTROL;
    case QEMUCameraGamma:
        *size = 2;
        return PU_GAMMA_CONTROL;
    case QEMUCameraHue:
        *size = 2;
        return PU_HUE_CONTROL;
    case QEMUCameraHueAuto:
        *size = 1;
        return PU_HUE_AUTO_CONTROL;
    case QEMUCameraSaturation:
        *size = 2;
        return PU_SATURATION_CONTROL;
    case QEMUCameraSharpness:
        *size = 2;
        return PU_SHARPNESS_CONTROL;
    case QEMUCameraWhiteBalanceTemperature:
        *size = 2;
        return PU_WHITE_BALANCE_TEMPERATURE_CONTROL;
    }

    return 0;
}

static QEMUCameraControlType usb_video_pu_control_type_to_qemu(uint8_t cs)
{
    switch (cs) {
    case PU_BRIGHTNESS_CONTROL:
        return QEMUCameraBrightness;
    case PU_CONTRAST_CONTROL:
        return QEMUCameraContrast;
    case PU_GAIN_CONTROL:
        return QEMUCameraGain;
    case PU_GAMMA_CONTROL:
        return QEMUCameraGamma;
    case PU_HUE_CONTROL:
        return QEMUCameraHue;
    case PU_HUE_AUTO_CONTROL:
        return QEMUCameraHueAuto;
    case PU_SATURATION_CONTROL:
        return QEMUCameraSaturation;
    case PU_SHARPNESS_CONTROL:
        return QEMUCameraSharpness;
    case PU_WHITE_BALANCE_TEMPERATURE_CONTROL:
        return QEMUCameraWhiteBalanceTemperature;
    }

    return QEMUCameraControlMax;
}

#define REQ_TO_ATTR(req, idx)  \
    switch (req) {             \
    case SET_CUR:              \
    case GET_CUR:              \
        idx = ATTRIBUTE_CUR;   \
        break;                 \
    case GET_MIN:              \
        idx = ATTRIBUTE_MIN;   \
        break;                 \
    case GET_MAX:              \
        idx = ATTRIBUTE_MAX;   \
        break;                 \
    case GET_RES:              \
        idx = ATTRIBUTE_RES;   \
        break;                 \
    case GET_DEF:              \
        idx = ATTRIBUTE_DEF;   \
        break;                 \
    default:                   \
        idx = -1;              \
        break;                 \
    }

#define handle_get_control(attrs, req, cs, length, data, ret)                \
    do {                                                                     \
        if (!attrs[cs].selector) {                                           \
            break;                                                           \
        }                                                                    \
        if ((req == GET_INFO) && (length >= 1)) {                            \
            *((uint8_t *)data) = attrs[cs].caps;                             \
            ret = 1;                                                         \
        } else if ((req == GET_LEN) && (length >= 2)) {                      \
            *((uint16_t *)data) = cpu_to_le16(attrs[cs].size);               \
            ret = 2;                                                         \
        } else {                                                             \
            int idx = -1;                                                    \
            int len = MIN(length, sizeof(attrs[cs].size));                   \
            REQ_TO_ATTR(req, idx);                                           \
            if (idx >= 0) {                                                  \
                memcpy(data, &attrs[cs].value[idx], len);                    \
                ret = length;                                                \
            }                                                                \
        }                                                                    \
    } while (0)


#define handle_get_streaming(s, req, cs, length, data, ret)                  \
    do {                                                                     \
        if ((req == GET_INFO) && (length >= 1)) {                            \
            *((uint8_t *)data) = s->cs##_len;                                \
            ret = 1;                                                         \
        } else if ((req == GET_LEN) && (length >= 2)) {                      \
            *((uint16_t *)data) = cpu_to_le16(s->cs##_len);                  \
            ret = 2;                                                         \
        } else {                                                             \
            int idx = -1;                                                    \
            int len = MIN(length, sizeof(s->cs##_attrs[0]));                 \
            REQ_TO_ATTR(req, idx);                                           \
            if (idx >= 0) {                                                  \
                memcpy(data, s->cs##_attrs + idx, len);                      \
                ret = length;                                                \
            }                                                                \
        }                                                                    \
    } while (0)

#define TYPE_USB_VIDEO "usb-video"
OBJECT_DECLARE_SIMPLE_TYPE(USBVideoState, USB_VIDEO)

static uint32_t usb_video_vsfmt_to_pixfmt(const uint8_t *data)
{
    uint8_t bDescriptorSubtype = data[2];
    uint32_t pixfmt = 0;

    switch (bDescriptorSubtype) {
    case VS_FORMAT_MJPEG:
        return QEMU_CAMERA_PIX_FMT_MJPEG;

    case VS_FORMAT_UNCOMPRESSED:
        pixfmt = *(uint32_t *)(data + 5);
        if (pixfmt == camera_fourcc_code('Y', 'U', 'Y', '2')) {
            return QEMU_CAMERA_PIX_FMT_YUYV;
        } else if (pixfmt == camera_fourcc_code('R', 'G', 'B', 'P')) {
            return QEMU_CAMERA_PIX_FMT_RGB565;
        }
    }

    return 0;
}

static uint8_t usb_video_pixfmt_to_vsfmt(uint32_t pixfmt)
{
    switch (pixfmt) {
    case QEMU_CAMERA_PIX_FMT_MJPEG:
        return VS_FORMAT_MJPEG;

    case QEMU_CAMERA_PIX_FMT_YUYV:
    case QEMU_CAMERA_PIX_FMT_RGB565:
        return VS_FORMAT_UNCOMPRESSED;
    }

    return VS_UNDEFINED;
}

static uint8_t usb_video_pixfmt_to_vsfrm(uint32_t pixfmt)
{
    switch (pixfmt) {
    case QEMU_CAMERA_PIX_FMT_MJPEG:
        return VS_FRAME_MJPEG;

    case QEMU_CAMERA_PIX_FMT_YUYV:
    case QEMU_CAMERA_PIX_FMT_RGB565:
        return VS_FRAME_UNCOMPRESSED;
    }

    return VS_UNDEFINED;
}

static int usb_video_get_frmival_from_vsc(USBDevice *dev,
                                          VideoStreamingControl *vsc,
                                          QEMUCameraFrameInterval *frmival)
{
    USBVideoState *s = USB_VIDEO(dev);
    USBDescOther *usb_desc;
    uint32_t pixfmt = 0;
    uint16_t width = 0, height = 0;
    uint8_t bDescriptorSubtype;
    uint8_t index;

    /* 1, search bFormatIndex */
    for (index = 0; index < s->n_vs_descs; index++) {
        usb_desc = s->vs_descs + index;
        if (usb_desc->data[0] < 4) {
            return -ENODEV;
        }

        bDescriptorSubtype = usb_desc->data[2];
        if ((bDescriptorSubtype == VS_FORMAT_MJPEG)
           || (bDescriptorSubtype == VS_FORMAT_UNCOMPRESSED)) {
            if (usb_desc->data[3] == vsc->bFormatIndex) {
                pixfmt = usb_video_vsfmt_to_pixfmt(usb_desc->data);
                break;
            }
        }
    }

    /* 2, search bFormatIndex */
    for (index++ ; pixfmt && index < s->n_vs_descs; index++) {
        usb_desc = s->vs_descs + index;
        if (usb_desc->data[0] < 4) {
            return -ENODEV;
        }

        bDescriptorSubtype = usb_desc->data[2];
        if ((bDescriptorSubtype == VS_FRAME_MJPEG)
           || (bDescriptorSubtype == VS_FRAME_UNCOMPRESSED)) {
            if (usb_desc->data[3] == vsc->bFrameIndex) {
                /* see Class-specific VS Frame Descriptor */
                width = le16_to_cpu(*(uint16_t *)(usb_desc->data + 5));
                height = le16_to_cpu(*(uint16_t *)(usb_desc->data + 7));
                break;
            }
        } else {
            break;
        }
    }

    if (pixfmt && width && height) {
        frmival->pixel_format = pixfmt;
        frmival->width = width;
        frmival->height = height;
        frmival->type = QEMU_CAMERA_FRMIVAL_TYPE_DISCRETE;
        frmival->d.numerator = 30; /* prime number 2 * 3 * 5 */
        frmival->d.denominator = frmival->d.numerator * 10000000
                                     / le32_to_cpu(vsc->dwFrameInterval);
        return 0;
    }

    return -ENODEV;
}

static void usb_video_queue_control_status(USBDevice *dev, uint8_t bOriginator,
                uint8_t bSelector, uint32_t *value, uint8_t size)
{
    USBVideoState *s = USB_VIDEO(dev);
    USBBus *bus = usb_bus_from_device(dev);
    USBVideoControlStats *usb_status;
    VideoControlStatus *status;

    usb_status = g_malloc0(sizeof(USBVideoControlStats));
    usb_status->size = size;
    status = &usb_status->status;
    status->bStatusType = STATUS_INTERRUPT_CONTROL;
    status->bOriginator = bOriginator;
    status->bEvent = 0;
    status->bSelector = bSelector;
    status->bAttribute = STATUS_CONTROL_VALUE_CHANGE;
    memcpy(status->bValue, value, size);

    QTAILQ_INSERT_TAIL(&s->control_status, usb_status, list);
    trace_usb_video_queue_control_status(bus->busnr, dev->addr, bOriginator,
        bSelector, *value, size);
}

static int usb_video_get_control(USBDevice *dev, int request, int value,
                                 int index, int length, uint8_t *data)
{
    USBVideoState *s = USB_VIDEO(dev);
    USBBus *bus = usb_bus_from_device(dev);
    uint8_t req = request & 0xff;
    uint8_t cs = value >> 8;
    uint8_t intfnum = index & 0xff;
    uint8_t unit = index >> 8;
    int ret = USB_RET_STALL;

    switch (intfnum) {
    case IF_CONTROL:
        switch (unit) {
        case 0:
            if (length != 1) {
                break;
            }

            if (cs == VC_VIDEO_POWER_MODE_CONTROL) {
                data[0] = 127; /* 4.2.1.1 Power Mode Control */
                ret = 1;
            } else if (cs == VC_REQUEST_ERROR_CODE_CONTROL) {
                data[0] = s->error; /* 4.2.1.2 Request Error Code Control */
                s->error = 0;
                ret = 1;
            }
            break;

        case PROCESSING_UNIT:
            {
                QEMUCameraControlType t = usb_video_pu_control_type_to_qemu(cs);
                handle_get_control(s->pu_attrs, req, t, length, data, ret);
            }
            break;

        case SELECTOR_UNIT:
        case ENCODING_UNIT:
        default:
            /* TODO XU control support */
            break;
        }
        break;

    case IF_STREAMING:
        switch (cs) {
        case VS_PROBE_CONTROL:
            handle_get_streaming(s, req, vsc, length, data, ret);
            break;

        default:
            qemu_log_mask(LOG_UNIMP, "%s: get streamimg %d not implemented\n",
                          TYPE_USB_VIDEO, cs);
        }

        break;
    }

    trace_usb_video_get_control(bus->busnr, dev->addr, intfnum, unit, cs, ret);

    return ret;
}

static int usb_video_set_vs_control(USBDevice *dev, uint8_t req, int length,
                                    uint8_t *data)
{
    USBVideoState *s = USB_VIDEO(dev);
    int idx = -1;
    int ret = USB_RET_STALL;

    REQ_TO_ATTR(req, idx);
    if ((idx >= 0) && (length <= sizeof(s->vsc_attrs[0]))) {
        VideoStreamingControl *dst = s->vsc_attrs + idx;
        VideoStreamingControl *src = (VideoStreamingControl *)data;

        dst->bFormatIndex = src->bFormatIndex;
        dst->bFrameIndex = src->bFrameIndex;
        VIDEO_CONTROL_TEST_AND_SET(src->bmHint, dwFrameInterval, src, dst);
        VIDEO_CONTROL_TEST_AND_SET(src->bmHint, wKeyFrameRate, src, dst);
        VIDEO_CONTROL_TEST_AND_SET(src->bmHint, wPFrameRate, src, dst);
        VIDEO_CONTROL_TEST_AND_SET(src->bmHint, wCompQuality, src, dst);
        VIDEO_CONTROL_TEST_AND_SET(src->bmHint, wCompWindowSize, src, dst);
        ret = length;
    }

    return ret;
}

static int usb_video_set_control(USBDevice *dev, int request, int value,
                                 int index, int length, uint8_t *data)
{
    USBVideoState *s = USB_VIDEO(dev);
    USBBus *bus = usb_bus_from_device(dev);
    uint8_t req = request & 0xff;
    uint8_t cs = value >> 8;
    uint8_t intfnum = index & 0xff;
    uint8_t unit = index >> 8;
    int ret = USB_RET_STALL;

    switch (intfnum) {
    case IF_CONTROL:
        switch (unit) {
        case PROCESSING_UNIT:
            {
                uint32_t value = 0;
                QEMUCameraControl ctrl;
                QEMUCameraControlType type;
                Error *local_err = NULL;

                type = usb_video_pu_control_type_to_qemu(cs);
                if (type == QEMUCameraControlMax) {
                    break;
                }

                if (length > 4) {
                    break;
                }

                memcpy(&value, data, length);
                value = le32_to_cpu(value);
                ctrl.type = type;
                ctrl.cur = value;
                if (qemu_camera_set_control(s->camera, &ctrl, &local_err)) {
                    error_reportf_err(local_err, "%s: ", TYPE_USB_VIDEO);
                    break;
                }

                memcpy(&s->pu_attrs[type].value[ATTRIBUTE_CUR], data, length);
                ret = length;
                usb_video_queue_control_status(dev, PROCESSING_UNIT, cs,
                                               &value, length);
            }
            break;

        /* TODO XU control support */
        }

        break;

    case IF_STREAMING:
        switch (cs) {
        case VS_PROBE_CONTROL:
        case VS_COMMIT_CONTROL:
            {
                QEMUCameraFrameInterval frmival;
                if (usb_video_get_frmival_from_vsc(dev,
                        (VideoStreamingControl *)data, &frmival)) {
                    s->error = VC_ERROR_OUT_OF_RANGE;
                    break;
                }

                ret = usb_video_set_vs_control(dev, req, length, data);
            }
            break;

        default:
            qemu_log_mask(LOG_UNIMP, "%s: set streamimg %d not implemented\n",
                          TYPE_USB_VIDEO, cs);
        }

        break;
    }

    trace_usb_video_set_control(bus->busnr, dev->addr, intfnum, cs, ret);

    return ret;
}

static void usb_video_handle_control(USBDevice *dev, USBPacket *p,
                                    int request, int value, int index,
                                    int length, uint8_t *data)
{
    USBBus *bus = usb_bus_from_device(dev);
    int ret = 0;

    ret = usb_desc_handle_control(dev, p, request, value, index, length, data);
    if (ret >= 0) {
        return;
    }

    switch (request) {
    case ClassInterfaceRequest | GET_CUR:
    case ClassInterfaceRequest | GET_MIN:
    case ClassInterfaceRequest | GET_MAX:
    case ClassInterfaceRequest | GET_RES:
    case ClassInterfaceRequest | GET_LEN:
    case ClassInterfaceRequest | GET_INFO:
    case ClassInterfaceRequest | GET_DEF:
        ret = usb_video_get_control(dev, request, value, index, length, data);
        if (ret < 0) {
            goto error;
        }
        break;
    case ClassInterfaceOutRequest | SET_CUR:
        ret = usb_video_set_control(dev, request, value, index, length, data);
        if (ret < 0) {
            goto error;
        }
        break;
    case ClassInterfaceRequest | GET_CUR_ALL:
    case ClassInterfaceRequest | GET_MIN_ALL:
    case ClassInterfaceRequest | GET_MAX_ALL:
    case ClassInterfaceRequest | GET_RES_ALL:
    case ClassInterfaceRequest | GET_DEF_ALL:
    case ClassInterfaceOutRequest | SET_CUR_ALL:
    default:
        qemu_log_mask(LOG_UNIMP, "%s: request %d not implemented\n",
                      TYPE_USB_VIDEO, request);
        goto error;
    }

    p->actual_length = ret;
    p->status = USB_RET_SUCCESS;
    return;

error:
    trace_usb_video_handle_control_error(bus->busnr, dev->addr, request,
        value, index, length);
    p->status = USB_RET_STALL;
}

static void usb_video_set_streaming_altset(USBDevice *dev, int altset)
{
    USBVideoState *s = USB_VIDEO(dev);
    Error *local_err = NULL;

    if (s->streaming_altset == altset) {
        return;
    }

    switch (altset) {
    case ALTSET_OFF:
        qemu_camera_stream_off(s->camera, &local_err);
        break;

    case ALTSET_STREAMING:
        {
            QEMUCameraFrameInterval frmival;

            if (usb_video_get_frmival_from_vsc(dev,
                    &s->vsc_attrs[ATTRIBUTE_CUR], &frmival)) {
                s->error = VC_ERROR_OUT_OF_RANGE;
                break;
            }

            qemu_camera_set_frame_interval(s->camera, &frmival, &local_err);
            if (local_err) {
                s->error = VC_ERROR_INVALID_VALUE_WITHIN_RANGE;
                error_reportf_err(local_err, "%s: ", TYPE_USB_VIDEO);
                return;
            }

            qemu_camera_stream_on(s->camera, NULL, s, &local_err);
            if (local_err) {
                s->error = VC_ERROR_INVALID_REQUEST;
                error_reportf_err(local_err, "%s: ", TYPE_USB_VIDEO);
                return;
            }
        }
        break;
    }

    s->streaming_altset = altset;
}

static void usb_video_set_interface(USBDevice *dev, int iface,
                                    int old, int value)
{
    USBBus *bus = usb_bus_from_device(dev);

    trace_usb_video_set_interface(bus->busnr, dev->addr, iface, value);

    if (iface == IF_STREAMING) {
        usb_video_set_streaming_altset(dev, value);
    }
}

static void usb_video_handle_reset(USBDevice *dev)
{
    USBVideoState *s = USB_VIDEO(dev);
    USBBus *bus = usb_bus_from_device(dev);
    Error *local_err = NULL;

    trace_usb_video_handle_reset(bus->busnr, dev->addr);
    qemu_camera_stream_off(s->camera, &local_err);
}

static void usb_video_handle_streaming_in(USBDevice *dev, USBPacket *p)
{
    USBVideoState *s = USB_VIDEO(dev);
    USBBus *bus = usb_bus_from_device(dev);
    QEMUIOVector *iov = p->combined ? &p->combined->iov : &p->iov;
    VideoImagePayloadHeader header;
    int len;

    if (s->streaming_altset != ALTSET_STREAMING) {
        p->status = USB_RET_NAK;
        return;
    }

    /* TODO PresentationTime & scrSourceClock support */
    header.bmHeaderInfo = PAYLOAD_HEADER_EOH;
    header.bmHeaderInfo |= s->fid ? PAYLOAD_HEADER_FID : 0;
    header.bHeaderLength = 2;
    if (p->actual_length + header.bHeaderLength > iov->size) {
        p->status = USB_RET_STALL;
        return;
    }

    len = qemu_camera_stream_length(s->camera);
    if (!len) {
        p->status = USB_RET_NAK;
        return;
    }

    if (len < iov->size - header.bHeaderLength) {
        /*
         * if we can take all of the remained data, mark EOF in payload header,
         * also change fid state.
         */
        header.bmHeaderInfo |= PAYLOAD_HEADER_EOF;
        s->fid = !s->fid;
    }

    /* firstly, copy payload header */
    usb_packet_copy(p, &header, header.bHeaderLength);

    /* then, copy payload data */
    len = qemu_camera_stream_read(s->camera, iov->iov, iov->niov,
              p->actual_length, iov->size - p->actual_length);
    p->actual_length += len;

    p->status = USB_RET_SUCCESS;

    trace_usb_video_handle_streaming_in(bus->busnr, dev->addr,
        header.bHeaderLength + len);
}

static void usb_video_handle_control_in(USBDevice *dev, USBPacket *p)
{
    USBVideoState *s = USB_VIDEO(dev);
    USBBus *bus = usb_bus_from_device(dev);
    USBVideoControlStats *usb_status = NULL;
    QEMUIOVector *iov = p->combined ? &p->combined->iov : &p->iov;
    int len = 0;

    if (QTAILQ_EMPTY(&s->control_status)) {
        p->status = USB_RET_NAK;
        goto out;
    }

    usb_status = QTAILQ_FIRST(&s->control_status);
    QTAILQ_REMOVE(&s->control_status, usb_status, list);
    len = MIN(5 + usb_status->size, iov->size); /* see VideoControlStatus */
    usb_packet_copy(p, &usb_status->status, len);
    p->status = USB_RET_SUCCESS;

out:
    trace_usb_video_handle_control_in(bus->busnr, dev->addr, len);
}

static void usb_video_handle_data(USBDevice *dev, USBPacket *p)
{
    if ((p->pid == USB_TOKEN_IN) && (p->ep->nr == EP_STREAMING)) {
        usb_video_handle_streaming_in(dev, p);
        return;
    } else if ((p->pid == USB_TOKEN_IN) && (p->ep->nr == EP_CONTROL)) {
        usb_video_handle_control_in(dev, p);
        return;
    }

    p->status = USB_RET_STALL;
}

static void usb_video_unrealize(USBDevice *dev)
{
}

static int usb_video_build_vc(USBDevice *dev)
{
    USBVideoState *s = USB_VIDEO(dev);
    USBBus *bus = usb_bus_from_device(dev);
    Error *local_err = NULL;
    USBDescIface *vc_iface;
    USBDescOther *usb_desc;
    QEMUCameraControl controls[QEMUCameraControlMax], *control;
    USBVideoControlInfo *controlinfo;
    uint32_t bmControl = 0;
    uint8_t *bmControls = NULL;
    int i, ncontrols, pucontrol;

    vc_iface = &s->desc_ifaces[0]; /* see VideoControl Interface Descriptor */

    /* search Processing Unit Descriptor, and build bmControls field */
    for (i = 0; i < vc_iface->ndesc; i++) {
        usb_desc = &vc_iface->descs[i];
        if (usb_desc->data[2] == VC_PROCESSING_UNIT) {
            bmControls = (uint8_t *)usb_desc->data + 8;
        }
    }

    ncontrols = qemu_camera_enum_control(s->camera, controls,
                                         ARRAY_SIZE(controls), &local_err);

    for (i = 0; i < ncontrols; i++) {
        uint8_t size = 0;
        control = &controls[i];
        bmControl |= usb_video_pu_control_bits(control->type);
        pucontrol = usb_video_pu_control_type(control->type, &size);
        assert(pucontrol < PU_MAX);
        if (pucontrol) {
            controlinfo = &s->pu_attrs[control->type];
            controlinfo->selector = pucontrol;
            controlinfo->caps = CONTROL_CAP_GET | CONTROL_CAP_SET
                                    | CONTROL_CAP_ASYNCHRONOUS;
            controlinfo->size = size;
            controlinfo->value[ATTRIBUTE_DEF] = cpu_to_le32(control->def);
            controlinfo->value[ATTRIBUTE_MIN] = cpu_to_le32(control->min);
            controlinfo->value[ATTRIBUTE_MAX] = cpu_to_le32(control->max);
            controlinfo->value[ATTRIBUTE_CUR] = cpu_to_le32(control->def);
            controlinfo->value[ATTRIBUTE_RES] = cpu_to_le32(control->step);

            trace_usb_video_pu(bus->busnr, dev->addr, pucontrol, size,
                control->def, control->min, control->max, control->step);
        }
    }

    if (bmControls) {
        bmControl = cpu_to_le32(bmControl);
        *bmControls = bmControl & 0xff;
        *(bmControls + 1) = (bmControl >> 8) & 0xff;
        *(bmControls + 2) = (bmControl >> 16) & 0xff;
    }

    return 0;
}

#define USB_VIDEO_PIX_FORMAT_MAX 4
#define USB_VIDEO_FRAME_SIZE_MAX 32
#define USB_VIDEO_FRAME_IVAL_MAX 8

#define VS_HEADER_LEN              0xe
#define VS_FORMAT_UNCOMPRESSED_LEN 0x1b
#define VS_FORMAT_MJPEG_LEN        0xb
#define VS_FORMAT_MAX_LEN MAX(VS_FORMAT_UNCOMPRESSED_LEN, VS_FORMAT_MJPEG_LEN)
#define VS_FRAME_MIN_LEN 0x1a
#define VS_FRAME_MAX_LEN (VS_FRAME_MIN_LEN + 4 * USB_VIDEO_FRAME_IVAL_MAX)

static int usb_video_vs_build_header(uint8_t *addr, uint16_t wTotalLength)
{
    /* Class-specific VS Header Descriptor (Input) */
    uint8_t data[] = {
        VS_HEADER_LEN,              /*  u8  bLength */
        CS_INTERFACE,               /*  u8  bDescriptorType */
        VS_INPUT_HEADER,            /*  u8  bDescriptorSubtype */
        0x01,                       /*  u8  bNumFormats */
        U16(wTotalLength),          /* u16  wTotalLength */
        USB_DIR_IN | EP_STREAMING,  /*  u8  bEndPointAddress */
        0x00,                       /*  u8  bmInfo */
        OUTPUT_TERMINAL,            /*  u8  bTerminalLink */
        0x01,                       /*  u8  bStillCaptureMethod */
        0x01,                       /*  u8  bTriggerSupport */
        0x00,                       /*  u8  bTriggerUsage */
        0x01,                       /*  u8  bControlSize */
        0x00,                       /*  u8  bmaControls */
    };

    memcpy(addr, data, data[0]);

    return data[0];
}

static int usb_video_vs_build_format(uint8_t *addr, uint32_t pixfmt,
                                     uint8_t bFormatIndex,
                                     uint8_t bNumFrameDescriptors)
{
    /* Class-specific VS Format Descriptor */
    uint8_t bDescriptorSubtype = usb_video_pixfmt_to_vsfmt(pixfmt);
    uint8_t *data = NULL;

    uint8_t data_mjpeg[] = {
        VS_FORMAT_MJPEG_LEN,        /*  u8  bLength */
        CS_INTERFACE,               /*  u8  bDescriptorType */
        bDescriptorSubtype,         /*  u8  bDescriptorSubtype */
        bFormatIndex,               /*  u8  bFormatIndex */
        bNumFrameDescriptors,       /*  u8  bNumFrameDescriptors */
        0x01,                       /*  u8  bmFlags */
        0x01,                       /*  u8  bDefaultFrameIndex */
        0x00,                       /*  u8  bAspectRatioX */
        0x00,                       /*  u8  bAspectRatioY */
        0x00,                       /*  u8  bmInterlaceFlags */
        0x00,                       /*  u8  bCopyProtect */
    };

    uint8_t data_uncompressed_yuy2[] = {
        VS_FORMAT_UNCOMPRESSED_LEN, /*  u8  bLength */
        CS_INTERFACE,               /*  u8  bDescriptorType */
        bDescriptorSubtype,         /*  u8  bDescriptorSubtype */
        bFormatIndex,               /*  u8  bFormatIndex */
        bNumFrameDescriptors,       /*  u8  bNumFrameDescriptors */
        /* guidFormat */
         'Y',  'U',  'Y',  '2', 0x00, 0x00, 0x10, 0x00,
        0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71,
        0x10,                       /*  u8  bBitsPerPixel */
        0x01,                       /*  u8  bDefaultFrameIndex */
        0x00,                       /*  u8  bAspectRatioX */
        0x00,                       /*  u8  bAspectRatioY */
        0x00,                       /*  u8  bmInterlaceFlags */
        0x00,                       /*  u8  bCopyProtect */
    };

    uint8_t data_uncompressed_rgb565[] = {
        VS_FORMAT_UNCOMPRESSED_LEN, /*  u8  bLength */
        CS_INTERFACE,               /*  u8  bDescriptorType */
        bDescriptorSubtype,         /*  u8  bDescriptorSubtype */
        bFormatIndex,               /*  u8  bFormatIndex */
        bNumFrameDescriptors,       /*  u8  bNumFrameDescriptors */
        /* guidFormat */
         'R',  'G',  'B',  'P', 0x00, 0x00, 0x10, 0x00,
        0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71,
        0x10,                       /*  u8  bBitsPerPixel */
        0x01,                       /*  u8  bDefaultFrameIndex */
        0x00,                       /*  u8  bAspectRatioX */
        0x00,                       /*  u8  bAspectRatioY */
        0x00,                       /*  u8  bmInterlaceFlags */
        0x00,                       /*  u8  bCopyProtect */
    };

    if (pixfmt == QEMU_CAMERA_PIX_FMT_MJPEG) {
        data = data_mjpeg;
    } else if (pixfmt == QEMU_CAMERA_PIX_FMT_YUYV) {
        data = data_uncompressed_yuy2;
    } else if (pixfmt == QEMU_CAMERA_PIX_FMT_RGB565) {
        data = data_uncompressed_rgb565;
    } else {
        return 0;
    }

    memcpy(addr, data, data[0]);

    return data[0];
}

static int usb_video_vs_build_frame(uint8_t *addr, uint8_t bDescriptorSubtype,
                                    uint8_t bFrameIndex,
                                    QEMUCameraFrameInterval *frmivals,
                                    uint8_t nfrmivals)
{
    uint8_t bLength = VS_FRAME_MIN_LEN + nfrmivals * 4;
    QEMUCameraFrameInterval *deffrmival = &frmivals[0];
    struct FrameIntervalDiscrete *d = &deffrmival->d;
    uint16_t wWidth = deffrmival->width;
    uint16_t wHeight = deffrmival->height;
    uint32_t dwMaxVideoFrameBufSize = wWidth * wHeight * 2;
    uint32_t dwDefaultFrameInterval = 10000000 * d->numerator / d->denominator;
    uint32_t *ival;
    int index;

    /* Class-specific VS Frame Descriptor */
    uint8_t data[VS_FRAME_MAX_LEN] = {
        bLength,                    /*  u8  bLength */
        CS_INTERFACE,               /*  u8  bDescriptorType */
        bDescriptorSubtype,         /*  u8  bDescriptorSubtype */
        bFrameIndex,                /*  u8  bFrameIndex */
        0x03,                       /*  u8  bmCapabilities */
        U16(wWidth),                /* u16  wWidth */
        U16(wHeight),               /* u16  wHeight */
        U32(442368000),             /* u32  dwMinBitRate */
        U32(442368000),             /* u32  dwMaxBitRate */
        U32(dwMaxVideoFrameBufSize),/* u32  dwMaxVideoFrameBufSize */
        U32(dwDefaultFrameInterval),/* u32  dwDefaultFrameInterval */
        nfrmivals,                  /*  u8  bFrameIntervalType */
    };

    for (index = 0; index < nfrmivals; index++) {
        ival = (uint32_t *)(data + VS_FRAME_MIN_LEN + 4 * index);
        d = &frmivals[index].d;
        *ival = cpu_to_le32(10000000 * d->numerator / d->denominator);
    }

    memcpy(addr, data, data[0]);

    return data[0];
}

static void usb_video_initialize(USBDevice *dev, Error **errp)
{
    USBVideoState *s = USB_VIDEO(dev);
    VideoStreamingControl *vsc;
    uint32_t pixfmts[USB_VIDEO_PIX_FORMAT_MAX];
    int npixfmts, pixfmtidx, frmszidx;
    USBDescIface *vs_iface;
    USBDescOther *usb_desc;
    uint32_t dwMaxVideoFrameSize = 0;
    uint32_t vs_length = VS_HEADER_LEN;

    s->vs_descs = g_new0(USBDescOther, 1 + USB_VIDEO_PIX_FORMAT_MAX
                        + USB_VIDEO_PIX_FORMAT_MAX * USB_VIDEO_FRAME_SIZE_MAX);
    s->vs_data = g_malloc0(VS_HEADER_LEN + VS_FORMAT_MAX_LEN
                      * USB_VIDEO_PIX_FORMAT_MAX + VS_FRAME_MAX_LEN
                      * USB_VIDEO_PIX_FORMAT_MAX * USB_VIDEO_FRAME_SIZE_MAX);
    usb_desc = s->vs_descs;
    usb_desc->data = s->vs_data;

    /* build desc video from template */
    memcpy(s->desc_ifaces, desc_ifaces, sizeof(s->desc_ifaces));

    s->desc_device_full = desc_device_full;
    *(USBDescIface **)&(s->desc_device_full.confs[0].ifs) = s->desc_ifaces;

    s->desc_device_high = desc_device_high;
    *(USBDescIface **)&(s->desc_device_high.confs[0].ifs) = s->desc_ifaces;

    s->desc_video = desc_video;
    s->desc_video.full = &s->desc_device_full;
    s->desc_video.high = &s->desc_device_high;

    usb_video_build_vc(dev);

    /*
     * let's build USBDescIfaces layout like this:
     * 1, VideoControl Interface Descriptor(fully copied from template)
     * 2, VideoStreaming Interface Descriptor(detect format & frame dynamically)
     *    2.1 Class-specific VS Header Descriptor(dynamic wTotalLength)
     *    2.2 Class-specific VS Format Descriptor(bFormatIndex 1)
     *    2.3 Class-specific VS Frame Descriptor(bFrameIndex 1)
     *    ...
     *    2.x Class-specific VS Frame Descriptor(bFrameIndex x-2)
     *    2.y Class-specific VS Format Descriptor(bFormatIndex 2)
     *    2.z Class-specific VS Frame Descriptor(bFrameIndex 1)
     *    ...
     * 3, Operational Alternate Setting 1(fully copied from template)
     */
    s->n_vs_descs = 1; /* at least 1 header */

    npixfmts = qemu_camera_enum_pixel_format(s->camera, pixfmts,
                    ARRAY_SIZE(pixfmts), errp);
    if (!npixfmts) {
        error_setg(errp, "%s: no available pixel format support on %s",
                   TYPE_USB_VIDEO, s->cameradev);
        return;
    }

    for (pixfmtidx = 0; pixfmtidx < npixfmts; pixfmtidx++) {
        QEMUCameraFrameSize frmszs[USB_VIDEO_FRAME_SIZE_MAX], *frmsz;
        uint8_t vsfrm = usb_video_pixfmt_to_vsfrm(pixfmts[pixfmtidx]);
        int nfrmszs;

        usb_desc = s->vs_descs + s->n_vs_descs++;
        usb_desc->data = s->vs_data + vs_length;

        nfrmszs = qemu_camera_enum_frame_size(s->camera, pixfmts[pixfmtidx],
                      frmszs, ARRAY_SIZE(frmszs), errp);

        vs_length += usb_video_vs_build_format(s->vs_data + vs_length,
                         pixfmts[pixfmtidx], (uint8_t)pixfmtidx + 1,
                         (uint8_t)nfrmszs);

        for (frmszidx = 0; frmszidx < nfrmszs; frmszidx++) {
            QEMUCameraFrameInterval frmivals[USB_VIDEO_FRAME_IVAL_MAX];
            QEMUCameraFormat fmt;
            int nfrmivals;

            frmsz = &frmszs[frmszidx];
            if (frmsz->type != QEMU_CAMERA_FRMSIZE_TYPE_DISCRETE) {
                continue; /* TODO stepwise support */
            }

            fmt.pixel_format = frmsz->pixel_format;
            fmt.width = frmsz->d.width;
            fmt.height = frmsz->d.height;
            nfrmivals = qemu_camera_enum_frame_interval(s->camera, &fmt,
                            frmivals, ARRAY_SIZE(frmivals), errp);
            if (!nfrmivals) {
                continue;
            }

            if (dwMaxVideoFrameSize < fmt.height * fmt.width * 2) {
                dwMaxVideoFrameSize = fmt.height * fmt.width * 2;
            }

            usb_desc = s->vs_descs + s->n_vs_descs++;
            usb_desc->data = s->vs_data + vs_length;
            vs_length += usb_video_vs_build_frame((uint8_t *)usb_desc->data,
                                                  vsfrm, (uint8_t)frmszidx + 1,
                                                  frmivals, (uint8_t)nfrmivals);
        }
    }

    /* build VideoStreaming Interface Descriptor */
    vs_iface = &s->desc_ifaces[1]; /* see VideoStreaming Interface Descriptor */
    usb_video_vs_build_header(s->vs_data, vs_length);
    vs_iface->ndesc = s->n_vs_descs;
    vs_iface->descs = s->vs_descs;

    /* keep align with VideoStreaming Interface Descriptor */
    s->vsc_info = 0;
    s->vsc_len = sizeof(*vsc);
    vsc = s->vsc_attrs + ATTRIBUTE_DEF;
    vsc->bFormatIndex = 1;
    vsc->bFrameIndex = 1;
    vsc->dwFrameInterval = cpu_to_le32(1000000); /* default 10 FPS */
    vsc->wDelay = cpu_to_le16(32);
    vsc->dwMaxVideoFrameSize = cpu_to_le32(dwMaxVideoFrameSize);
    vsc->dwMaxPayloadTransferSize = cpu_to_le32(1024);
    vsc->dwClockFrequency = cpu_to_le32(15000000);
    memcpy(s->vsc_attrs + ATTRIBUTE_CUR, vsc, sizeof(*vsc));
    memcpy(s->vsc_attrs + ATTRIBUTE_MIN, vsc, sizeof(*vsc));
    memcpy(s->vsc_attrs + ATTRIBUTE_MAX, vsc, sizeof(*vsc));
}

static void usb_video_realize(USBDevice *dev, Error **errp)
{
    USBVideoState *s = USB_VIDEO(dev);

    if (!s->terminal || strcmp(s->terminal, "camera")) {
        error_setg(errp, "%s: support terminal camera only", TYPE_USB_VIDEO);
        return;
    }

    s->camera = qemu_camera_by_id(s->cameradev);
    if (!s->camera) {
        error_setg(errp, "%s: invalid cameradev %s",
                   TYPE_USB_VIDEO, s->cameradev);
        return;
    }

    QTAILQ_INIT(&s->control_status);

    usb_video_initialize(dev, errp);
    dev->usb_desc = &s->desc_video;

    usb_desc_create_serial(dev);
    usb_desc_init(dev);
    s->dev.opaque = s;
}

static Property usb_video_properties[] = {
    DEFINE_PROP_STRING("cameradev", USBVideoState, cameradev),
    DEFINE_PROP_STRING("terminal", USBVideoState, terminal),
    DEFINE_PROP_END_OF_LIST(),
};

static void usb_video_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *k = USB_DEVICE_CLASS(klass);

    device_class_set_props(dc, usb_video_properties);
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
    k->product_desc   = "QEMU USB Video Interface";
    k->realize        = usb_video_realize;
    k->handle_reset   = usb_video_handle_reset;
    k->handle_control = usb_video_handle_control;
    k->handle_data    = usb_video_handle_data;
    k->unrealize      = usb_video_unrealize;
    k->set_interface  = usb_video_set_interface;
}

static const TypeInfo usb_video_info = {
    .name          = TYPE_USB_VIDEO,
    .parent        = TYPE_USB_DEVICE,
    .instance_size = sizeof(USBVideoState),
    .class_init    = usb_video_class_init,
};

static void usb_video_register_types(void)
{
    type_register_static(&usb_video_info);
}

type_init(usb_video_register_types)
