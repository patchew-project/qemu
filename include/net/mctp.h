#ifndef QEMU_MCTP_H
#define QEMU_MCTP_H

#include "hw/registerfields.h"

/* DSP0236 1.3.3, Section 8.4.2 Baseline transmission unit */
#define MCTP_BASELINE_MTU 64

/* DSP0236 1.3.3, Table 1, Message body */
FIELD(MCTP_MESSAGE_H, TYPE, 0, 7)
FIELD(MCTP_MESSAGE_H, IC,   7, 1)

/* DSP0236 1.3.3, Table 1, MCTP transport header */
FIELD(MCTP_H_FLAGS, TAG,    0, 3);
FIELD(MCTP_H_FLAGS, TO,     3, 1);
FIELD(MCTP_H_FLAGS, PKTSEQ, 4, 2);
FIELD(MCTP_H_FLAGS, EOM,    6, 1);
FIELD(MCTP_H_FLAGS, SOM,    7, 1);

/* DSP0236 1.3.3, Figure 4 Generic message fields */
typedef struct MCTPPacketHeader {
    uint8_t version;
    struct {
        uint8_t dest;
        uint8_t source;
    } eid;
    uint8_t flags;
} QEMU_PACKED MCTPPacketHeader;

typedef struct MCTPPacket {
    MCTPPacketHeader hdr;
    uint8_t          payload[];
} QEMU_PACKED MCTPPacket;

/* DSP0236 1.3.3, Figure 20 MCTP control message format */
typedef struct MCTPControlMessage {
#define MCTP_MESSAGE_TYPE_CONTROL 0x0
    uint8_t type;
#define MCTP_CONTROL_FLAGS_RQ               (1 << 7)
#define MCTP_CONTROL_FLAGS_D                (1 << 6)
    uint8_t flags;
    uint8_t command_code;
    uint8_t data[];
} QEMU_PACKED MCTPControlMessage;

enum MCTPControlCommandCodes {
    MCTP_CONTROL_SET_EID                    = 0x01,
    MCTP_CONTROL_GET_EID                    = 0x02,
    MCTP_CONTROL_GET_UUID                   = 0x03,
    MCTP_CONTROL_GET_VERSION                = 0x04,
    MCTP_CONTROL_GET_MESSAGE_TYPE_SUPPORT   = 0x05,
};

/* DSP0236 1.3.3, Table 13 MCTP control message completion codes */
#define MCTP_CONTROL_CC_SUCCESS                 0x0
#define MCTP_CONTROL_CC_ERROR                   0x1
#define MCTP_CONTROL_CC_ERROR_INVALID_DATA      0x2
#define MCTP_CONTROL_CC_ERROR_INVALID_LENGTH    0x3
#define MCTP_CONTROL_CC_ERROR_NOT_READY         0x4
#define MCTP_CONTROL_CC_ERROR_UNSUP_COMMAND     0x5

typedef struct MCTPControlErrRsp {
    uint8_t completion_code;
} MCTPControlErrRsp;

/* DSP0236 1.3.3 Table 14 - Set Endpoint ID message */
typedef struct MCTPControlSetEIDReq {
    uint8_t operation;
    uint8_t eid;
} MCTPControlSetEIDReq;

typedef struct MCTPControlSetEIDRsp {
    uint8_t completion_code;
    uint8_t operation_result; /* Not named in spec */
    uint8_t eid_setting;
    uint8_t eid_pool_size;
} MCTPControlSetEIDRsp;

/* DSP0236 1.3.3 Table 15 - Get Endpoint ID message */
typedef struct MCTPControlGetEIDRsp {
    uint8_t completion_code;
    uint8_t endpoint_id;
    uint8_t endpoint_type;
    uint8_t medium_specific_info;
} MCTPControlGetEIDRsp;

/* DSP0236 1.3.3 Table 16 - Get Endpoint UUID message format */
typedef struct MCTPControlGetUUIDRsp {
    uint8_t completion_code;
    uint8_t uuid[0x10];
} MCTPControlGetUUIDRsp;

/* DSP0236 1.3.3 Table 19 - Get Message Type Support message */
typedef struct MCTPControlGetMessageTypeRsp {
    uint8_t completion_code;
    uint8_t message_type_count;
    uint8_t types[];
} MCTPControlGetMessageTypeRsp;

#endif /* QEMU_MCTP_H */
