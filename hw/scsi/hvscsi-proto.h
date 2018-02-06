/*
 * Hyper-V storage device protocol definitions
 *
 * Copyright (c) 2009, Microsoft Corporation.
 * Copyright (c) 2017-2018 Virtuozzo International GmbH.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef _HVSCSI_PROTO_H_
#define _HVSCSI_PROTO_H_

#define HV_STOR_PROTO_VERSION(MAJOR_, MINOR_) \
    ((((MAJOR_) & 0xff) << 8) | (((MINOR_) & 0xff)))

#define HV_STOR_PROTO_VERSION_WIN6       HV_STOR_PROTO_VERSION(2, 0)
#define HV_STOR_PROTO_VERSION_WIN7       HV_STOR_PROTO_VERSION(4, 2)
#define HV_STOR_PROTO_VERSION_WIN8       HV_STOR_PROTO_VERSION(5, 1)
#define HV_STOR_PROTO_VERSION_WIN8_1     HV_STOR_PROTO_VERSION(6, 0)
#define HV_STOR_PROTO_VERSION_WIN10      HV_STOR_PROTO_VERSION(6, 2)
#define HV_STOR_PROTO_VERSION_CURRENT    HV_STOR_PROTO_VERSION_WIN8

#define HV_STOR_OPERATION_COMPLETE_IO             1
#define HV_STOR_OPERATION_REMOVE_DEVICE           2
#define HV_STOR_OPERATION_EXECUTE_SRB             3
#define HV_STOR_OPERATION_RESET_LUN               4
#define HV_STOR_OPERATION_RESET_ADAPTER           5
#define HV_STOR_OPERATION_RESET_BUS               6
#define HV_STOR_OPERATION_BEGIN_INITIALIZATION    7
#define HV_STOR_OPERATION_END_INITIALIZATION      8
#define HV_STOR_OPERATION_QUERY_PROTOCOL_VERSION  9
#define HV_STOR_OPERATION_QUERY_PROPERTIES        10
#define HV_STOR_OPERATION_ENUMERATE_BUS           11
#define HV_STOR_OPERATION_FCHBA_DATA              12
#define HV_STOR_OPERATION_CREATE_SUB_CHANNELS     13

#define HV_STOR_REQUEST_COMPLETION_FLAG           0x1

#define HV_STOR_PROPERTIES_MULTI_CHANNEL_FLAG     0x1

#define HV_SRB_MAX_CDB_SIZE                     16
#define HV_SRB_SENSE_BUFFER_SIZE                20

#define HV_SRB_REQUEST_TYPE_WRITE               0
#define HV_SRB_REQUEST_TYPE_READ                1
#define HV_SRB_REQUEST_TYPE_UNKNOWN             2

#define HV_SRB_MAX_LUNS_PER_TARGET              255
#define HV_SRB_MAX_TARGETS                      2
#define HV_SRB_MAX_CHANNELS                     8

#define HV_SRB_FLAGS_QUEUE_ACTION_ENABLE        0x00000002
#define HV_SRB_FLAGS_DISABLE_DISCONNECT         0x00000004
#define HV_SRB_FLAGS_DISABLE_SYNCH_TRANSFER     0x00000008
#define HV_SRB_FLAGS_BYPASS_FROZEN_QUEUE        0x00000010
#define HV_SRB_FLAGS_DISABLE_AUTOSENSE          0x00000020
#define HV_SRB_FLAGS_DATA_IN                    0x00000040
#define HV_SRB_FLAGS_DATA_OUT                   0x00000080
#define HV_SRB_FLAGS_NO_DATA_TRANSFER           0x00000000
#define HV_SRB_FLAGS_UNSPECIFIED_DIRECTION      (SRB_FLAGS_DATA_IN | SRB_FLAGS_DATA_OUT)
#define HV_SRB_FLAGS_NO_QUEUE_FREEZE            0x00000100
#define HV_SRB_FLAGS_ADAPTER_CACHE_ENABLE       0x00000200
#define HV_SRB_FLAGS_FREE_SENSE_BUFFER          0x00000400
#define HV_SRB_FLAGS_D3_PROCESSING              0x00000800
#define HV_SRB_FLAGS_IS_ACTIVE                  0x00010000
#define HV_SRB_FLAGS_ALLOCATED_FROM_ZONE        0x00020000
#define HV_SRB_FLAGS_SGLIST_FROM_POOL           0x00040000
#define HV_SRB_FLAGS_BYPASS_LOCKED_QUEUE        0x00080000
#define HV_SRB_FLAGS_NO_KEEP_AWAKE              0x00100000
#define HV_SRB_FLAGS_PORT_DRIVER_ALLOCSENSE     0x00200000
#define HV_SRB_FLAGS_PORT_DRIVER_SENSEHASPORT   0x00400000
#define HV_SRB_FLAGS_DONT_START_NEXT_PACKET     0x00800000
#define HV_SRB_FLAGS_PORT_DRIVER_RESERVED       0x0F000000
#define HV_SRB_FLAGS_CLASS_DRIVER_RESERVED      0xF0000000

#define HV_SRB_STATUS_AUTOSENSE_VALID           0x80
#define HV_SRB_STATUS_INVALID_LUN               0x20
#define HV_SRB_STATUS_SUCCESS                   0x01
#define HV_SRB_STATUS_ABORTED                   0x02
#define HV_SRB_STATUS_ERROR                     0x04

#define HV_STOR_PACKET_MAX_LENGTH sizeof(struct hv_stor_packet)
#define HV_STOR_PACKET_MIN_LENGTH \
    (sizeof(struct hv_stor_packet) - sizeof(struct hv_srb_win8_extentions))

typedef struct hv_stor_properties {
    uint32_t _reserved1;
    uint16_t max_channel_count;
    uint16_t _reserved2;
    uint32_t flags;
    uint32_t max_transfer_bytes;
    uint32_t _reserved3[2];
} hv_stor_properties;

typedef struct hv_srb_win8_extentions {
    uint16_t _reserved;
    uint8_t  queue_tag;
    uint8_t  queue_action;
    uint32_t srb_flags;
    uint32_t timeout;
    uint32_t queue_sort;
} hv_srb_win8_extentions;

typedef struct hv_srb_packet {
    uint16_t length;
    uint8_t  srb_status;
    uint8_t  scsi_status;

    uint8_t  port;
    uint8_t  channel;
    uint8_t  target;
    uint8_t  lun;

    uint8_t  cdb_length;
    uint8_t  sense_length;
    uint8_t  data_in;
    uint8_t  _reserved;

    uint32_t transfer_length;

    union {
        uint8_t cdb[HV_SRB_MAX_CDB_SIZE];
        uint8_t sense_data[HV_SRB_SENSE_BUFFER_SIZE];
    };

    hv_srb_win8_extentions win8_ext;
} hv_srb_packet;

typedef struct hv_stor_protocol_version {
    uint16_t major_minor;
    uint16_t revision;
} hv_stor_protocol_version;

typedef struct hv_stor_packet {
    uint32_t operation;         /* HV_STOR_OPERATION_* */
    uint32_t flags;             // HV_STOR_FLAG_* */
    uint32_t status;

    union {
        hv_srb_packet srb;
        hv_stor_properties properties;
        hv_stor_protocol_version version;
        uint16_t sub_channel_count;

        uint8_t _reserved[0x34];
    };
} hv_stor_packet;

#endif
