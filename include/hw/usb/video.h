#ifndef HW_USB_VIDEO_H
#define HW_USB_VIDEO_H

/* Base on UVC specification 1.5 */

/* A.2. Video Interface Subclass Codes */
#define SC_UNDEFINED                  0x00
#define SC_VIDEOCONTROL               0x01
#define SC_VIDEOSTREAMING             0x02
#define SC_VIDEO_INTERFACE_COLLECTION 0x03

/* A.3. Video Interface Protocol Codes */
#define PC_PROTOCOL_UNDEFINED 0x00
#define PC_PROTOCOL_15        0x01

/* A.4. Video Class-Specific Descriptor Types */
#define CS_UNDEFINED     0x20
#define CS_DEVICE        0x21
#define CS_CONFIGURATION 0x22
#define CS_STRING        0x23
#define CS_INTERFACE     0x24
#define CS_ENDPOINT      0x25

/* A.5. Video Class-Specific VC Interface Descriptor Subtypes */
#define VC_DESCRIPTOR_UNDEFINED 0x00
#define VC_HEADER               0x01
#define VC_INPUT_TERMINAL       0x02
#define VC_OUTPUT_TERMINAL      0x03
#define VC_SELECTOR_UNIT        0x04
#define VC_PROCESSING_UNIT      0x05
#define VC_EXTENSION_UNIT       0x06
#define VC_ENCODING_UNIT        0x07

/* A.6. Video Class-Specific VS Interface Descriptor Subtypes */
#define VS_UNDEFINED             0x00
#define VS_INPUT_HEADER          0x01
#define VS_OUTPUT_HEADER         0x02
#define VS_STILL_IMAGE_FRAME     0x03
#define VS_FORMAT_UNCOMPRESSED   0x04
#define VS_FRAME_UNCOMPRESSED    0x05
#define VS_FORMAT_MJPEG          0x06
#define VS_FRAME_MJPEG           0x07
#define VS_FORMAT_MPEG2TS        0x0A
#define VS_FORMAT_DV             0x0C
#define VS_COLORFORMAT           0x0D
#define VS_FORMAT_FRAME_BASED    0x10
#define VS_FRAME_FRAME_BASED     0x11
#define VS_FORMAT_STREAM_BASED   0x12
#define VS_FORMAT_H264           0x13
#define VS_FRAME_H264            0x14
#define VS_FORMAT_H264_SIMULCAST 0x15
#define VS_FORMAT_VP8            0x16
#define VS_FRAME_VP8             0x17
#define VS_FORMAT_VP8_SIMULCAST  0x18

/* A.7. Video Class-Specific Endpoint Descriptor Subtypes */
#define EP_UNDEFINED 0x00
#define EP_GENERAL   0x01
#define EP_ENDPOINT  0x02
#define EP_INTERRUPT 0x03

/* A.8. Video Class-Specific Request Codes */
#define RC_UNDEFINED 0x00
#define SET_CUR      0x01
#define SET_CUR_ALL  0x11
#define GET_CUR      0x81
#define GET_MIN      0x82
#define GET_MAX      0x83
#define GET_RES      0x84
#define GET_LEN      0x85
#define GET_INFO     0x86
#define GET_DEF      0x87
#define GET_CUR_ALL  0x91
#define GET_MIN_ALL  0x92
#define GET_MAX_ALL  0x93
#define GET_RES_ALL  0x94
#define GET_DEF_ALL  0x97

/* 4.1.2 Get Request: Defined Bits Containing Capabilities of the Control */
#define CONTROL_CAP_GET          (1 << 0)
#define CONTROL_CAP_SET          (1 << 1)
#define CONTROL_CAP_DISABLED     (1 << 2)
#define CONTROL_CAP_AUTOUPDATE   (1 << 3)
#define CONTROL_CAP_ASYNCHRONOUS (1 << 4)

/* 4.2.1.2 Request Error Code Control */
#define VC_ERROR_NOT_READY                  0x01
#define VC_ERROR_WRONG_STATE                0x02
#define VC_ERROR_POWER                      0x03
#define VC_ERROR_OUT_OF_RANGE               0x04
#define VC_ERROR_INVALID_UNIT               0x05
#define VC_ERROR_INVALID_CONTROL            0x06
#define VC_ERROR_INVALID_REQUEST            0x07
#define VC_ERROR_INVALID_VALUE_WITHIN_RANGE 0x08

/* A.9.1. VideoControl Interface Control Selectors */
#define VC_CONTROL_UNDEFINED          0x00
#define VC_VIDEO_POWER_MODE_CONTROL   0x01
#define VC_REQUEST_ERROR_CODE_CONTROL 0x02

/* A.9.2. Terminal Control Selectors */
#define TE_CONTROL_UNDEFINED 0x00

/* A.9.3. Selector Unit Control Selectors */
#define SU_CONTROL_UNDEFINED    0x00
#define SU_INPUT_SELECT_CONTROL 0x01

/* A.9.4. Camera Terminal Control Selectors */
#define CT_CONTROL_UNDEFINED              0x00
#define CT_SCANNING_MODE_CONTROL          0x01
#define CT_AE_MODE_CONTROL                0x02
#define CT_AE_PRIORITY_CONTROL            0x03
#define CT_EXPOSURE_TIME_ABSOLUTE_CONTROL 0x04
#define CT_EXPOSURE_TIME_RELATIVE_CONTROL 0x05
#define CT_FOCUS_ABSOLUTE_CONTROL         0x06
#define CT_FOCUS_RELATIVE_CONTROL         0x07
#define CT_FOCUS_AUTO_CONTROL             0x08
#define CT_IRIS_ABSOLUTE_CONTROL          0x09
#define CT_IRIS_RELATIVE_CONTROL          0x0A
#define CT_ZOOM_ABSOLUTE_CONTROL          0x0B
#define CT_ZOOM_RELATIVE_CONTROL          0x0C
#define CT_PANTILT_ABSOLUTE_CONTROL       0x0D
#define CT_PANTILT_RELATIVE_CONTROL       0x0E
#define CT_ROLL_ABSOLUTE_CONTROL          0x0F
#define CT_ROLL_RELATIVE_CONTROL          0x10
#define CT_PRIVACY_CONTROL                0x11
#define CT_FOCUS_SIMPLE_CONTROL           0x12
#define CT_WINDOW_CONTROL                 0x13
#define CT_REGION_OF_INTEREST_CONTROL     0x14

/* A.9.5. Processing Unit Control Selectors */
#define PU_CONTROL_UNDEFINED                      0x00
#define PU_BACKLIGHT_COMPENSATION_CONTROL         0x01
#define PU_BRIGHTNESS_CONTROL                     0x02
#define PU_CONTRAST_CONTROL                       0x03
#define PU_GAIN_CONTROL                           0x04
#define PU_POWER_LINE_FREQUENCY_CONTROL           0x05
#define PU_HUE_CONTROL                            0x06
#define PU_SATURATION_CONTROL                     0x07
#define PU_SHARPNESS_CONTROL                      0x08
#define PU_GAMMA_CONTROL                          0x09
#define PU_WHITE_BALANCE_TEMPERATURE_CONTROL      0x0A
#define PU_WHITE_BALANCE_TEMPERATURE_AUTO_CONTROL 0x0B
#define PU_WHITE_BALANCE_COMPONENT_CONTROL        0x0C
#define PU_WHITE_BALANCE_COMPONENT_AUTO_CONTROL   0x0D
#define PU_DIGITAL_MULTIPLIER_CONTROL             0x0E
#define PU_DIGITAL_MULTIPLIER_LIMIT_CONTROL       0x0F
#define PU_HUE_AUTO_CONTROL                       0x10
#define PU_ANALOG_VIDEO_STANDARD_CONTROL          0x11
#define PU_ANALOG_LOCK_STATUS_CONTROL             0x12
#define PU_CONTRAST_AUTO_CONTROL                  0x13
#define PU_MAX                                    0x14 /* self defined */

/* 3.7.2.5 Processing Unit Descriptor bmControl bits */
#define PU_CONTRL_BRIGHTNESS                     (1 << 0)
#define PU_CONTRL_CONTRAST                       (1 << 1)
#define PU_CONTRL_HUE                            (1 << 2)
#define PU_CONTRL_SATURATION                     (1 << 3)
#define PU_CONTRL_SHARPNESS                      (1 << 4)
#define PU_CONTRL_GAMMA                          (1 << 5)
#define PU_CONTRL_WHITE_BALANCE_TEMPERATURE      (1 << 6)
#define PU_CONTRL_WHITE_BALANCE_COMPONENT        (1 << 7)
#define PU_CONTRL_BACKLIGHT_COMPENSATION         (1 << 8)
#define PU_CONTRL_GAIN                           (1 << 9)
#define PU_CONTRL_POWER_LINE_FREQUENCY           (1 << 10)
#define PU_CONTRL_HUE_AUTO                       (1 << 11)
#define PU_CONTRL_WHITE_BALANCE_TEMPERATURE_AUTO (1 << 12)
#define PU_CONTRL_WHITE_BALANCE_COMPONENT_AUTO   (1 << 13)
#define PU_CONTRL_DIGITAL_MULTIPLIER             (1 << 14)
#define PU_CONTRL_DIGITAL_MULTIPLIER_LIMIT       (1 << 15)
#define PU_CONTRL_ANALOG_VIDEO_STANDARD          (1 << 16)
#define PU_CONTRL_ANALOG_VIDEO_LOCK_STATUS       (1 << 17)
#define PU_CONTRL_CONTRAST_AUTO                  (1 << 18)

/* A.9.6. Encoding Unit Control Selectors */
#define EU_CONTROL_UNDEFINED           0x00
#define EU_SELECT_LAYER_CONTROL        0x01
#define EU_PROFILE_TOOLSET_CONTROL     0x02
#define EU_VIDEO_RESOLUTION_CONTROL    0x03
#define EU_MIN_FRAME_INTERVAL_CONTROL  0x04
#define EU_SLICE_MODE_CONTROL          0x05
#define EU_RATE_CONTROL_MODE_CONTROL   0x06
#define EU_AVERAGE_BITRATE_CONTROL     0x07
#define EU_CPB_SIZE_CONTROL            0x08
#define EU_PEAK_BIT_RATE_CONTROL       0x09
#define EU_QUANTIZATION_PARAMS_CONTROL 0x0A
#define EU_SYNC_REF_FRAME_CONTROL      0x0B
#define EU_LTR_BUFFER_ CONTROL         0x0C
#define EU_LTR_PICTURE_CONTROL         0x0D
#define EU_LTR_VALIDATION_CONTROL      0x0E
#define EU_LEVEL_IDC_LIMIT_CONTROL     0x0F
#define EU_SEI_PAYLOADTYPE_CONTROL     0x10
#define EU_QP_RANGE_CONTROL            0x11
#define EU_PRIORITY_CONTROL            0x12
#define EU_START_OR_STOP_LAYER_CONTROL 0x13
#define EU_ERROR_RESILIENCY_CONTROL    0x14

/* A.9.8. VideoStreaming Interface Control Selectors */
#define VS_CONTROL_UNDEFINED            0x00
#define VS_PROBE_CONTROL                0x01
#define VS_COMMIT_CONTROL               0x02
#define VS_STILL_PROBE_CONTROL          0x03
#define VS_STILL_COMMIT_CONTROL         0x04
#define VS_STILL_IMAGE_TRIGGER_CONTROL  0x05
#define VS_STREAM_ERROR_CODE_CONTROL    0x06
#define VS_GENERATE_KEY_FRAME_CONTROL   0x07
#define VS_UPDATE_FRAME_SEGMENT_CONTROL 0x08
#define VS_SYNCH_DELAY_CONTROL          0x09

/* B.1. USB Terminal Types */
#define TT_VENDOR_SPECIFIC 0x0100
#define TT_STREAMING       0x0101

/* B.2. Input Terminal Types */
#define ITT_VENDOR_SPECIFIC       0x0200
#define ITT_CAMERA                0x0201
#define ITT_MEDIA_TRANSPORT_INPUT 0x0202

/* B.3. Output Terminal Types */
#define OTT_VENDOR_SPECIFIC        0x0300
#define OTT_DISPLAY                0x0301
#define OTT_MEDIA_TRANSPORT_OUTPUT 0x0302

/* B.4. External Terminal Types */
#define EXTERNAL_VENDOR_SPECIFIC 0x0400
#define COMPOSITE_CONNECTOR      0x0401
#define SVIDEO_CONNECTOR         0x0402
#define COMPONENT_CONNECTOR      0x0403

/* 4.3.1.1. Video Probe and Commit Controls */
#define VIDEO_CONTROL_dwFrameInterval (1 << 0)
#define VIDEO_CONTROL_wKeyFrameRate   (1 << 1)
#define VIDEO_CONTROL_wPFrameRate     (1 << 2)
#define VIDEO_CONTROL_wCompQuality    (1 << 3)
#define VIDEO_CONTROL_wCompWindowSize (1 << 4)

#define VIDEO_CONTROL_TEST_AND_SET(bmHint, field, src, dst) \
        ((VIDEO_CONTROL_##field & bmHint) ? dst->field = src->field : 0)

typedef struct QEMU_PACKED VideoStreamingControl {
    uint16_t bmHint;
    uint8_t bFormatIndex;
    uint8_t bFrameIndex;
    uint32_t dwFrameInterval;
    uint16_t wKeyFrameRate;
    uint16_t wPFrameRate;
    uint16_t wCompQuality;
    uint16_t wCompWindowSize;
    uint16_t wDelay;
    uint32_t dwMaxVideoFrameSize;
    uint32_t dwMaxPayloadTransferSize;
    uint32_t dwClockFrequency;
    uint8_t bmFramingInfo;
    uint8_t bPreferedVersion;
    uint8_t bMinVersion;
    uint8_t bMaxVersion;
    uint8_t bUsage;
    uint8_t bBitDepthLuma;
    uint8_t bmSettings;
    uint8_t bMaxNumberOfRefFramesPlus1;
    uint16_t bmRateControlModes;
    uint16_t bmLayoutPerStream[4];
} VideoStreamingControl;

/* 2.4.3.3 Video and Still Image Payload Headers */
#define PAYLOAD_HEADER_FID (1 << 0)
#define PAYLOAD_HEADER_EOF (1 << 1)
#define PAYLOAD_HEADER_PTS (1 << 2)
#define PAYLOAD_HEADER_SCR (1 << 3)
#define PAYLOAD_HEADER_RES (1 << 4)
#define PAYLOAD_HEADER_STI (1 << 5)
#define PAYLOAD_HEADER_ERR (1 << 6)
#define PAYLOAD_HEADER_EOH (1 << 7)

typedef struct QEMU_PACKED VideoImagePayloadHeader {
    uint8_t bHeaderLength;
    uint8_t bmHeaderInfo;
    uint32_t dwPresentationTime;
    /* 6 bytes scrSourceClock */
    uint32_t dwStc; /* D31..D0 */
    uint16_t bmSof; /* D42..D32 */
} VideoImagePayloadHeader;

/* 2.4.2.2 Status Interrupt Endpoint */
#define STATUS_INTERRUPT_CONTROL   0x1
#define STATUS_INTERRUPT_STREAMING 0x2

#define STATUS_CONTROL_VALUE_CHANGE   0x00
#define STATUS_CONTROL_INFO_CHANGE    0x01
#define STATUS_CONTROL_FAILURE_CHANGE 0x02
#define STATUS_CONTROL_MIN_CHANGE     0x03
#define STATUS_CONTROL_MAX_CHANGE     0x04

typedef struct QEMU_PACKED VideoControlStatus {
    uint8_t bStatusType;
    uint8_t bOriginator;
    uint8_t bEvent;
    uint8_t bSelector;
    uint8_t bAttribute;
    uint8_t bValue[4];
} VideoControlStatus;

#endif
