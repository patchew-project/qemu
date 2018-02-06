/*
 * Hyper-V network device protocol definitions
 *
 * Copyright (c) 2011, Microsoft Corporation.
 * Copyright (c) 2018 Virtuozzo International GmbH.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef _HVNET_PROTO_H_
#define _HVNET_PROTO_H_

/****
 * c&p from linux drivers/net/hyperv/hyperv_net.h
 ****/

/* RSS related */
#define OID_GEN_RECEIVE_SCALE_CAPABILITIES 0x00010203  /* query only */
#define OID_GEN_RECEIVE_SCALE_PARAMETERS 0x00010204  /* query and set */

#define NDIS_OBJECT_TYPE_RSS_CAPABILITIES 0x88
#define NDIS_OBJECT_TYPE_RSS_PARAMETERS 0x89
#define NDIS_OBJECT_TYPE_OFFLOAD	0xa7

#define NDIS_RECEIVE_SCALE_CAPABILITIES_REVISION_2 2
#define NDIS_RECEIVE_SCALE_PARAMETERS_REVISION_2 2

struct ndis_obj_header {
    uint8_t type;
    uint8_t rev;
    uint16_t size;
} QEMU_PACKED;

/* ndis_recv_scale_cap/cap_flag */
#define NDIS_RSS_CAPS_MESSAGE_SIGNALED_INTERRUPTS 0x01000000
#define NDIS_RSS_CAPS_CLASSIFICATION_AT_ISR       0x02000000
#define NDIS_RSS_CAPS_CLASSIFICATION_AT_DPC       0x04000000
#define NDIS_RSS_CAPS_USING_MSI_X                 0x08000000
#define NDIS_RSS_CAPS_RSS_AVAILABLE_ON_PORTS      0x10000000
#define NDIS_RSS_CAPS_SUPPORTS_MSI_X              0x20000000
#define NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV4          0x00000100
#define NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV6          0x00000200
#define NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV6_EX       0x00000400

struct ndis_recv_scale_cap { /* NDIS_RECEIVE_SCALE_CAPABILITIES */
    struct ndis_obj_header hdr;
    uint32_t cap_flag;
    uint32_t num_int_msg;
    uint32_t num_recv_que;
    uint16_t num_indirect_tabent;
} QEMU_PACKED;


/* ndis_recv_scale_param flags */
#define NDIS_RSS_PARAM_FLAG_BASE_CPU_UNCHANGED     0x0001
#define NDIS_RSS_PARAM_FLAG_HASH_INFO_UNCHANGED    0x0002
#define NDIS_RSS_PARAM_FLAG_ITABLE_UNCHANGED       0x0004
#define NDIS_RSS_PARAM_FLAG_HASH_KEY_UNCHANGED     0x0008
#define NDIS_RSS_PARAM_FLAG_DISABLE_RSS            0x0010

/* Hash info bits */
#define NDIS_HASH_FUNC_TOEPLITZ 0x00000001
#define NDIS_HASH_IPV4          0x00000100
#define NDIS_HASH_TCP_IPV4      0x00000200
#define NDIS_HASH_IPV6          0x00000400
#define NDIS_HASH_IPV6_EX       0x00000800
#define NDIS_HASH_TCP_IPV6      0x00001000
#define NDIS_HASH_TCP_IPV6_EX   0x00002000

#define NDIS_RSS_INDIRECTION_TABLE_MAX_SIZE_REVISION_2 (128 * 4)
#define NDIS_RSS_HASH_SECRET_KEY_MAX_SIZE_REVISION_2   40

#define ITAB_NUM 128

struct ndis_recv_scale_param { /* NDIS_RECEIVE_SCALE_PARAMETERS */
    struct ndis_obj_header hdr;

    /* Qualifies the rest of the information */
    uint16_t flag;

    /* The base CPU number to do receive processing. not used */
    uint16_t base_cpu_number;

    /* This describes the hash function and type being enabled */
    uint32_t hashinfo;

    /* The size of indirection table array */
    uint16_t indirect_tabsize;

    /* The offset of the indirection table from the beginning of this
     * structure
     */
    uint32_t indirect_taboffset;

    /* The size of the hash secret key */
    uint16_t hashkey_size;

    /* The offset of the secret key from the beginning of this structure */
    uint32_t kashkey_offset;

    uint32_t processor_masks_offset;
    uint32_t num_processor_masks;
    uint32_t processor_masks_entry_size;
};

/* Fwd declaration */
struct ndis_tcp_ip_checksum_info;
struct ndis_pkt_8021q_info;

/*
 * Represent netvsc packet which contains 1 RNDIS and 1 ethernet frame
 * within the RNDIS
 *
 * The size of this structure is less than 48 bytes and we can now
 * place this structure in the skb->cb field.
 */
struct hv_netvsc_packet {
    /* Bookkeeping stuff */
    uint8_t cp_partial; /* partial copy into send buffer */

    uint8_t rmsg_size; /* RNDIS header and PPI size */
    uint8_t rmsg_pgcnt; /* page count of RNDIS header and PPI */
    uint8_t page_buf_cnt;

    uint16_t q_idx;
    uint16_t total_packets;

    uint32_t total_bytes;
    uint32_t send_buf_index;
    uint32_t total_data_buflen;
};

enum rndis_device_state {
    RNDIS_DEV_UNINITIALIZED = 0,
    RNDIS_DEV_INITIALIZING,
    RNDIS_DEV_INITIALIZED,
    RNDIS_DEV_DATAINITIALIZED,
};

#define NETVSC_HASH_KEYLEN 40

#define NVSP_INVALID_PROTOCOL_VERSION	((uint32_t)0xFFFFFFFF)

#define NVSP_PROTOCOL_VERSION_1		2
#define NVSP_PROTOCOL_VERSION_2		0x30002
#define NVSP_PROTOCOL_VERSION_4		0x40000
#define NVSP_PROTOCOL_VERSION_5		0x50000

enum {
    NVSP_MSG_TYPE_NONE = 0,

    /* Init Messages */
    NVSP_MSG_TYPE_INIT			= 1,
    NVSP_MSG_TYPE_INIT_COMPLETE		= 2,

    NVSP_VERSION_MSG_START			= 100,

    /* Version 1 Messages */
    NVSP_MSG1_TYPE_SEND_NDIS_VER		= NVSP_VERSION_MSG_START,

    NVSP_MSG1_TYPE_SEND_RECV_BUF,
    NVSP_MSG1_TYPE_SEND_RECV_BUF_COMPLETE,
    NVSP_MSG1_TYPE_REVOKE_RECV_BUF,

    NVSP_MSG1_TYPE_SEND_SEND_BUF,
    NVSP_MSG1_TYPE_SEND_SEND_BUF_COMPLETE,
    NVSP_MSG1_TYPE_REVOKE_SEND_BUF,

    NVSP_MSG1_TYPE_SEND_RNDIS_PKT,
    NVSP_MSG1_TYPE_SEND_RNDIS_PKT_COMPLETE,

    /* Version 2 messages */
    NVSP_MSG2_TYPE_SEND_CHIMNEY_DELEGATED_BUF,
    NVSP_MSG2_TYPE_SEND_CHIMNEY_DELEGATED_BUF_COMP,
    NVSP_MSG2_TYPE_REVOKE_CHIMNEY_DELEGATED_BUF,

    NVSP_MSG2_TYPE_RESUME_CHIMNEY_RX_INDICATION,

    NVSP_MSG2_TYPE_TERMINATE_CHIMNEY,
    NVSP_MSG2_TYPE_TERMINATE_CHIMNEY_COMP,

    NVSP_MSG2_TYPE_INDICATE_CHIMNEY_EVENT,

    NVSP_MSG2_TYPE_SEND_CHIMNEY_PKT,
    NVSP_MSG2_TYPE_SEND_CHIMNEY_PKT_COMP,

    NVSP_MSG2_TYPE_POST_CHIMNEY_RECV_REQ,
    NVSP_MSG2_TYPE_POST_CHIMNEY_RECV_REQ_COMP,

    NVSP_MSG2_TYPE_ALLOC_RXBUF,
    NVSP_MSG2_TYPE_ALLOC_RXBUF_COMP,

    NVSP_MSG2_TYPE_FREE_RXBUF,

    NVSP_MSG2_TYPE_SEND_VMQ_RNDIS_PKT,
    NVSP_MSG2_TYPE_SEND_VMQ_RNDIS_PKT_COMP,

    NVSP_MSG2_TYPE_SEND_NDIS_CONFIG,

    NVSP_MSG2_TYPE_ALLOC_CHIMNEY_HANDLE,
    NVSP_MSG2_TYPE_ALLOC_CHIMNEY_HANDLE_COMP,

    NVSP_MSG2_MAX = NVSP_MSG2_TYPE_ALLOC_CHIMNEY_HANDLE_COMP,

    /* Version 4 messages */
    NVSP_MSG4_TYPE_SEND_VF_ASSOCIATION,
    NVSP_MSG4_TYPE_SWITCH_DATA_PATH,
    NVSP_MSG4_TYPE_UPLINK_CONNECT_STATE_DEPRECATED,

    NVSP_MSG4_MAX = NVSP_MSG4_TYPE_UPLINK_CONNECT_STATE_DEPRECATED,

    /* Version 5 messages */
    NVSP_MSG5_TYPE_OID_QUERY_EX,
    NVSP_MSG5_TYPE_OID_QUERY_EX_COMP,
    NVSP_MSG5_TYPE_SUBCHANNEL,
    NVSP_MSG5_TYPE_SEND_INDIRECTION_TABLE,

    NVSP_MSG5_MAX = NVSP_MSG5_TYPE_SEND_INDIRECTION_TABLE,
};

enum {
    NVSP_STAT_NONE = 0,
    NVSP_STAT_SUCCESS,
    NVSP_STAT_FAIL,
    NVSP_STAT_PROTOCOL_TOO_NEW,
    NVSP_STAT_PROTOCOL_TOO_OLD,
    NVSP_STAT_INVALID_RNDIS_PKT,
    NVSP_STAT_BUSY,
    NVSP_STAT_PROTOCOL_UNSUPPORTED,
    NVSP_STAT_MAX,
};

struct nvsp_msg_header {
    uint32_t msg_type;
};

/* Init Messages */

/*
 * This message is used by the VSC to initialize the channel after the channels
 * has been opened. This message should never include anything other then
 * versioning (i.e. this message will be the same for ever).
 */
struct nvsp_msg_init {
    uint32_t min_protocol_ver;
    uint32_t max_protocol_ver;
} QEMU_PACKED;

/*
 * This message is used by the VSP to complete the initialization of the
 * channel. This message should never include anything other then versioning
 * (i.e. this message will be the same for ever).
 */
struct nvsp_msg_init_complete {
    uint32_t negotiated_protocol_ver;
    uint32_t max_mdl_chain_len;
    uint32_t status;
};

/* Version 1 Messages */

/*
 * This message is used by the VSC to send the NDIS version to the VSP. The VSP
 * can use this information when handling OIDs sent by the VSC.
 */
struct nvsp1_msg_ndis_ver {
    uint32_t ndis_major_ver;
    uint32_t ndis_minor_ver;
} QEMU_PACKED;

/*
 * This message is used by the VSC to send a receive buffer to the VSP. The VSP
 * can then use the receive buffer to send data to the VSC.
 */
struct nvsp1_msg_rcvbuf {
    uint32_t gpadl_handle;
    uint16_t id;
} QEMU_PACKED;

struct nvsp1_rcvbuf_section {
    uint32_t offset;
    uint32_t sub_alloc_size;
    uint32_t num_sub_allocs;
    uint32_t end_offset;
} QEMU_PACKED;

/*
 * This message is used by the VSP to acknowledge a receive buffer send by the
 * VSC. This message must be sent by the VSP before the VSP uses the receive
 * buffer.
 */
struct nvsp1_msg_rcvbuf_complete {
    uint32_t status;
    uint32_t num_sections;

    /*
     * The receive buffer is split into two parts, a large suballocation
     * section and a small suballocation section. These sections are then
     * suballocated by a certain size.
     */

    /*
     * For example, the following break up of the receive buffer has 6
     * large suballocations and 10 small suballocations.
     */

    /*
     * |            Large Section          |  |   Small Section   |
     * ------------------------------------------------------------
     * |     |     |     |     |     |     |  | | | | | | | | | | |
     * |                                      |
     *  LargeOffset                            SmallOffset
     */

    struct nvsp1_rcvbuf_section sections[1];
} QEMU_PACKED;

/*
 * This message is sent by the VSC to revoke the receive buffer.  After the VSP
 * completes this transaction, the vsp should never use the receive buffer
 * again.
 */
struct nvsp1_msg_revoke_rcvbuf {
    uint16_t id;
};

/*
 * This message is used by the VSC to send a send buffer to the VSP. The VSC
 * can then use the send buffer to send data to the VSP.
 */
struct nvsp1_msg_sndbuf {
    uint32_t gpadl_handle;
    uint16_t id;
} QEMU_PACKED;

/*
 * This message is used by the VSP to acknowledge a send buffer sent by the
 * VSC. This message must be sent by the VSP before the VSP uses the sent
 * buffer.
 */
struct nvsp1_msg_sndbuf_complete {
    uint32_t status;

    /*
     * The VSC gets to choose the size of the send buffer and the VSP gets
     * to choose the sections size of the buffer.  This was done to enable
     * dynamic reconfigurations when the cost of GPA-direct buffers
     * decreases.
     */
    uint32_t section_size;
} QEMU_PACKED;

/*
 * This message is sent by the VSC to revoke the send buffer.  After the VSP
 * completes this transaction, the vsp should never use the send buffer again.
 */
struct nvsp1_msg_revoke_sndbuf {
    uint16_t id;
};

/*
 * This message is used by both the VSP and the VSC to send a RNDIS message to
 * the opposite channel endpoint.
 */
struct nvsp1_msg_rndis_pkt {
    /*
     * This field is specified by RNDIS. They assume there's two different
     * channels of communication. However, the Network VSP only has one.
     * Therefore, the channel travels with the RNDIS packet.
     */
    uint32_t channel_type;

    /*
     * This field is used to send part or all of the data through a send
     * buffer. This values specifies an index into the send buffer. If the
     * index is 0xFFFFFFFF, then the send buffer is not being used and all
     * of the data was sent through other VMBus mechanisms.
     */
    uint32_t send_buf_section_index;
    uint32_t send_buf_section_size;
} QEMU_PACKED;

/*
 * This message is used by both the VSP and the VSC to complete a RNDIS message
 * to the opposite channel endpoint. At this point, the initiator of this
 * message cannot use any resources associated with the original RNDIS packet.
 */
struct nvsp1_msg_rndis_pkt_complete {
    uint32_t status;
};

/*
 * Network VSP protocol version 2 messages:
 */
struct nvsp2_vsc_capability {
    union {
        uint64_t data;
        struct {
            uint64_t vmq:1;
            uint64_t chimney:1;
            uint64_t sriov:1;
            uint64_t ieee8021q:1;
            uint64_t correlation_id:1;
            uint64_t teaming:1;
        };
    };
} QEMU_PACKED;

struct nvsp2_send_ndis_config {
    uint32_t mtu;
    uint32_t reserved;
    struct nvsp2_vsc_capability capability;
} QEMU_PACKED;

/* Allocate receive buffer */
struct nvsp2_alloc_rxbuf {
    /* Allocation ID to match the allocation request and response */
    uint32_t alloc_id;

    /* Length of the VM shared memory receive buffer that needs to
     * be allocated
     */
    uint32_t len;
} QEMU_PACKED;

/* Allocate receive buffer complete */
struct nvsp2_alloc_rxbuf_comp {
    /* The NDIS_STATUS code for buffer allocation */
    uint32_t status;

    uint32_t alloc_id;

    /* GPADL handle for the allocated receive buffer */
    uint32_t gpadl_handle;

    /* Receive buffer ID */
    uint64_t recv_buf_id;
} QEMU_PACKED;

struct nvsp2_free_rxbuf {
    uint64_t recv_buf_id;
} QEMU_PACKED;

struct nvsp4_send_vf_association {
    /* 1: allocated, serial number is valid. 0: not allocated */
    uint32_t allocated;

    /* Serial number of the VF to team with */
    uint32_t serial;
} QEMU_PACKED;

enum nvsp_vm_datapath {
    NVSP_DATAPATH_SYNTHETIC = 0,
    NVSP_DATAPATH_VF,
    NVSP_DATAPATH_MAX
};

struct nvsp4_sw_datapath {
    uint32_t active_datapath; /* active data path in VM */
} QEMU_PACKED;

enum nvsp_subchannel_operation {
    NVSP_SUBCHANNEL_NONE = 0,
    NVSP_SUBCHANNEL_ALLOCATE,
    NVSP_SUBCHANNEL_MAX
};

struct nvsp5_subchannel_request {
    uint32_t op;
    uint32_t num_subchannels;
} QEMU_PACKED;

struct nvsp5_subchannel_complete {
    uint32_t status;
    uint32_t num_subchannels; /* Actual number of subchannels allocated */
} QEMU_PACKED;

struct nvsp5_send_indirect_table {
    /* The number of entries in the send indirection table */
    uint32_t count;

    /* The offset of the send indirection table from top of this struct.
     * The send indirection table tells which channel to put the send
     * traffic on. Each entry is a channel number.
     */
    uint32_t offset;
} QEMU_PACKED;

union nvsp_all_messages {
    struct nvsp_msg_init init;
    struct nvsp_msg_init_complete init_complete;

    struct nvsp1_msg_ndis_ver send_ndis_ver;
    struct nvsp1_msg_rcvbuf send_recv_buf;
    struct nvsp1_msg_rcvbuf_complete send_recv_buf_complete;
    struct nvsp1_msg_revoke_rcvbuf revoke_recv_buf;
    struct nvsp1_msg_sndbuf send_send_buf;
    struct nvsp1_msg_sndbuf_complete send_send_buf_complete;
    struct nvsp1_msg_revoke_sndbuf revoke_send_buf;
    struct nvsp1_msg_rndis_pkt send_rndis_pkt;
    struct nvsp1_msg_rndis_pkt_complete send_rndis_pkt_complete;

    struct nvsp2_send_ndis_config send_ndis_config;
    struct nvsp2_alloc_rxbuf alloc_rxbuf;
    struct nvsp2_alloc_rxbuf_comp alloc_rxbuf_comp;
    struct nvsp2_free_rxbuf free_rxbuf;

    struct nvsp4_send_vf_association vf_assoc;
    struct nvsp4_sw_datapath active_dp;

    struct nvsp5_subchannel_request subchn_req;
    struct nvsp5_subchannel_complete subchn_comp;
    struct nvsp5_send_indirect_table send_table;
} QEMU_PACKED;

/* ALL Messages */
struct nvsp_msg {
    struct nvsp_msg_header hdr;
    union nvsp_all_messages msg;
} QEMU_PACKED;


#define NETVSC_MTU 65535
#define NETVSC_MTU_MIN ETH_MIN_MTU

#define NETVSC_RECEIVE_BUFFER_SIZE		(1024*1024*16)	/* 16MB */
#define NETVSC_RECEIVE_BUFFER_SIZE_LEGACY	(1024*1024*15)  /* 15MB */
#define NETVSC_SEND_BUFFER_SIZE			(1024 * 1024 * 15)   /* 15MB */
#define NETVSC_INVALID_INDEX			-1


#define NETVSC_RECEIVE_BUFFER_ID		0xcafe
#define NETVSC_SEND_BUFFER_ID			0

#define NETVSC_PACKET_SIZE                      4096

#define VRSS_SEND_TAB_SIZE 16  /* must be power of 2 */
#define VRSS_CHANNEL_MAX 64
#define VRSS_CHANNEL_DEFAULT 8

#define RNDIS_MAX_PKT_DEFAULT 8
#define RNDIS_PKT_ALIGN_DEFAULT 8

/* Netvsc Receive Slots Max */
#define NETVSC_RECVSLOT_MAX (NETVSC_RECEIVE_BUFFER_SIZE / ETH_DATA_LEN + 1)

/* NdisInitialize message */
struct rndis_initialize_request {
    uint32_t req_id;
    uint32_t major_ver;
    uint32_t minor_ver;
    uint32_t max_xfer_size;
};

/* Response to NdisInitialize */
struct rndis_initialize_complete {
    uint32_t req_id;
    uint32_t status;
    uint32_t major_ver;
    uint32_t minor_ver;
    uint32_t dev_flags;
    uint32_t medium;
    uint32_t max_pkt_per_msg;
    uint32_t max_xfer_size;
    uint32_t pkt_alignment_factor;
    uint32_t af_list_offset;
    uint32_t af_list_size;
};

/* Call manager devices only: Information about an address family */
/* supported by the device is appended to the response to NdisInitialize. */
struct rndis_co_address_family {
    uint32_t address_family;
    uint32_t major_ver;
    uint32_t minor_ver;
};

/* NdisHalt message */
struct rndis_halt_request {
    uint32_t req_id;
};

/* NdisQueryRequest message */
struct rndis_query_request {
    uint32_t req_id;
    uint32_t oid;
    uint32_t info_buflen;
    uint32_t info_buf_offset;
    uint32_t dev_vc_handle;
};

/* Response to NdisQueryRequest */
struct rndis_query_complete {
    uint32_t req_id;
    uint32_t status;
    uint32_t info_buflen;
    uint32_t info_buf_offset;
};

/* NdisSetRequest message */
struct rndis_set_request {
    uint32_t req_id;
    uint32_t oid;
    uint32_t info_buflen;
    uint32_t info_buf_offset;
    uint32_t dev_vc_handle;
};

/* Response to NdisSetRequest */
struct rndis_set_complete {
    uint32_t req_id;
    uint32_t status;
};

/* NdisReset message */
struct rndis_reset_request {
    uint32_t reserved;
};

/* Response to NdisReset */
struct rndis_reset_complete {
    uint32_t status;
    uint32_t addressing_reset;
};

/* NdisMIndicateStatus message */
struct rndis_indicate_status {
    uint32_t status;
    uint32_t status_buflen;
    uint32_t status_buf_offset;
};

/* Diagnostic information passed as the status buffer in */
/* struct rndis_indicate_status messages signifying error conditions. */
struct rndis_diagnostic_info {
    uint32_t diag_status;
    uint32_t error_offset;
};

/* NdisKeepAlive message */
struct rndis_keepalive_request {
    uint32_t req_id;
};

/* Response to NdisKeepAlive */
struct rndis_keepalive_complete {
    uint32_t req_id;
    uint32_t status;
};

/*
 * Data message. All Offset fields contain byte offsets from the beginning of
 * struct rndis_packet. All Length fields are in bytes.  VcHandle is set
 * to 0 for connectionless data, otherwise it contains the VC handle.
 */
struct rndis_packet {
    uint32_t data_offset;
    uint32_t data_len;
    uint32_t oob_data_offset;
    uint32_t oob_data_len;
    uint32_t num_oob_data_elements;
    uint32_t per_pkt_info_offset;
    uint32_t per_pkt_info_len;
    uint32_t vc_handle;
    uint32_t reserved;
};

/* Optional Out of Band data associated with a Data message. */
struct rndis_oobd {
    uint32_t size;
    uint32_t type;
    uint32_t class_info_offset;
};

/* Packet extension field contents associated with a Data message. */
struct rndis_per_packet_info {
    uint32_t size;
    uint32_t type;
    uint32_t ppi_offset;
};

enum ndis_per_pkt_info_type {
    TCPIP_CHKSUM_PKTINFO,
    IPSEC_PKTINFO,
    TCP_LARGESEND_PKTINFO,
    CLASSIFICATION_HANDLE_PKTINFO,
    NDIS_RESERVED,
    SG_LIST_PKTINFO,
    IEEE_8021Q_INFO,
    ORIGINAL_PKTINFO,
    PACKET_CANCEL_ID,
    NBL_HASH_VALUE = PACKET_CANCEL_ID,
    ORIGINAL_NET_BUFLIST,
    CACHED_NET_BUFLIST,
    SHORT_PKT_PADINFO,
    MAX_PER_PKT_INFO
};

struct ndis_pkt_8021q_info {
    union {
        struct {
            uint32_t pri:3; /* User Priority */
            uint32_t cfi:1; /* Canonical Format ID */
            uint32_t vlanid:12; /* VLAN ID */
            uint32_t reserved:16;
        };
        uint32_t value;
    };
};

struct ndis_object_header {
    uint8_t type;
    uint8_t revision;
    uint16_t size;
};

#define NDIS_OBJECT_TYPE_DEFAULT	0x80
#define NDIS_OFFLOAD_PARAMETERS_REVISION_3 3
#define NDIS_OFFLOAD_PARAMETERS_REVISION_2 2
#define NDIS_OFFLOAD_PARAMETERS_REVISION_1 1

#define NDIS_OFFLOAD_PARAMETERS_NO_CHANGE 0
#define NDIS_OFFLOAD_PARAMETERS_LSOV2_DISABLED 1
#define NDIS_OFFLOAD_PARAMETERS_LSOV2_ENABLED  2
#define NDIS_OFFLOAD_PARAMETERS_LSOV1_ENABLED  2
#define NDIS_OFFLOAD_PARAMETERS_RSC_DISABLED 1
#define NDIS_OFFLOAD_PARAMETERS_RSC_ENABLED 2
#define NDIS_OFFLOAD_PARAMETERS_TX_RX_DISABLED 1
#define NDIS_OFFLOAD_PARAMETERS_TX_ENABLED_RX_DISABLED 2
#define NDIS_OFFLOAD_PARAMETERS_RX_ENABLED_TX_DISABLED 3
#define NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED 4

#define NDIS_TCP_LARGE_SEND_OFFLOAD_V2_TYPE	1
#define NDIS_TCP_LARGE_SEND_OFFLOAD_IPV4	0
#define NDIS_TCP_LARGE_SEND_OFFLOAD_IPV6	1

#define VERSION_4_OFFLOAD_SIZE			22
/*
 * New offload OIDs for NDIS 6
 */
#define OID_TCP_OFFLOAD_CURRENT_CONFIG 0xFC01020B /* query only */
#define OID_TCP_OFFLOAD_PARAMETERS 0xFC01020C		/* set only */
#define OID_TCP_OFFLOAD_HARDWARE_CAPABILITIES 0xFC01020D/* query only */
#define OID_TCP_CONNECTION_OFFLOAD_CURRENT_CONFIG 0xFC01020E /* query only */
#define OID_TCP_CONNECTION_OFFLOAD_HARDWARE_CAPABILITIES 0xFC01020F /* query */
#define OID_OFFLOAD_ENCAPSULATION 0x0101010A /* set/query */

/*
 * OID_TCP_OFFLOAD_HARDWARE_CAPABILITIES
 * ndis_type: NDIS_OBJTYPE_OFFLOAD
 */

#define	NDIS_OFFLOAD_ENCAP_NONE		0x0000
#define	NDIS_OFFLOAD_ENCAP_NULL		0x0001
#define	NDIS_OFFLOAD_ENCAP_8023		0x0002
#define	NDIS_OFFLOAD_ENCAP_8023PQ	0x0004
#define	NDIS_OFFLOAD_ENCAP_8023PQ_OOB	0x0008
#define	NDIS_OFFLOAD_ENCAP_RFC1483	0x0010

struct ndis_csum_offload {
    uint32_t	ip4_txenc;
    uint32_t	ip4_txcsum;
#define	NDIS_TXCSUM_CAP_IP4OPT		0x001
#define	NDIS_TXCSUM_CAP_TCP4OPT		0x004
#define	NDIS_TXCSUM_CAP_TCP4		0x010
#define	NDIS_TXCSUM_CAP_UDP4		0x040
#define	NDIS_TXCSUM_CAP_IP4		0x100

#define NDIS_TXCSUM_ALL_TCP4	(NDIS_TXCSUM_CAP_TCP4 | NDIS_TXCSUM_CAP_TCP4OPT)

    uint32_t	ip4_rxenc;
    uint32_t	ip4_rxcsum;
#define	NDIS_RXCSUM_CAP_IP4OPT		0x001
#define	NDIS_RXCSUM_CAP_TCP4OPT		0x004
#define	NDIS_RXCSUM_CAP_TCP4		0x010
#define	NDIS_RXCSUM_CAP_UDP4		0x040
#define	NDIS_RXCSUM_CAP_IP4		0x100
    uint32_t	ip6_txenc;
    uint32_t	ip6_txcsum;
#define	NDIS_TXCSUM_CAP_IP6EXT		0x001
#define	NDIS_TXCSUM_CAP_TCP6OPT		0x004
#define	NDIS_TXCSUM_CAP_TCP6		0x010
#define	NDIS_TXCSUM_CAP_UDP6		0x040
    uint32_t	ip6_rxenc;
    uint32_t	ip6_rxcsum;
#define	NDIS_RXCSUM_CAP_IP6EXT		0x001
#define	NDIS_RXCSUM_CAP_TCP6OPT		0x004
#define	NDIS_RXCSUM_CAP_TCP6		0x010
#define	NDIS_RXCSUM_CAP_UDP6		0x040

#define NDIS_TXCSUM_ALL_TCP6	(NDIS_TXCSUM_CAP_TCP6 |		\
                                 NDIS_TXCSUM_CAP_TCP6OPT |	\
                                 NDIS_TXCSUM_CAP_IP6EXT)
};

struct ndis_lsov1_offload {
    uint32_t	encap;
    uint32_t	maxsize;
    uint32_t	minsegs;
    uint32_t	opts;
};

struct ndis_ipsecv1_offload {
    uint32_t	encap;
    uint32_t	ah_esp;
    uint32_t	xport_tun;
    uint32_t	ip4_opts;
    uint32_t	flags;
    uint32_t	ip4_ah;
    uint32_t	ip4_esp;
};

struct ndis_lsov2_offload {
    uint32_t	ip4_encap;
    uint32_t	ip4_maxsz;
    uint32_t	ip4_minsg;
    uint32_t	ip6_encap;
    uint32_t	ip6_maxsz;
    uint32_t	ip6_minsg;
    uint32_t	ip6_opts;
#define	NDIS_LSOV2_CAP_IP6EXT		0x001
#define	NDIS_LSOV2_CAP_TCP6OPT		0x004

#define NDIS_LSOV2_CAP_IP6		(NDIS_LSOV2_CAP_IP6EXT | \
                                         NDIS_LSOV2_CAP_TCP6OPT)
};

struct ndis_ipsecv2_offload {
    uint32_t	encap;
    uint16_t	ip6;
    uint16_t	ip4opt;
    uint16_t	ip6ext;
    uint16_t	ah;
    uint16_t	esp;
    uint16_t	ah_esp;
    uint16_t	xport;
    uint16_t	tun;
    uint16_t	xport_tun;
    uint16_t	lso;
    uint16_t	extseq;
    uint32_t	udp_esp;
    uint32_t	auth;
    uint32_t	crypto;
    uint32_t	sa_caps;
};

struct ndis_rsc_offload {
    uint16_t	ip4;
    uint16_t	ip6;
};

struct ndis_encap_offload {
    uint32_t	flags;
    uint32_t	maxhdr;
};

struct ndis_offload {
    struct ndis_object_header	header;
    struct ndis_csum_offload	csum;
    struct ndis_lsov1_offload	lsov1;
    struct ndis_ipsecv1_offload	ipsecv1;
    struct ndis_lsov2_offload	lsov2;
    uint32_t				flags;
    /* NDIS >= 6.1 */
    struct ndis_ipsecv2_offload	ipsecv2;
    /* NDIS >= 6.30 */
    struct ndis_rsc_offload		rsc;
    struct ndis_encap_offload	encap_gre;
};

#define	NDIS_OFFLOAD_SIZE		sizeof(struct ndis_offload)
#define	NDIS_OFFLOAD_SIZE_6_0		offsetof(struct ndis_offload, ipsecv2)
#define	NDIS_OFFLOAD_SIZE_6_1		offsetof(struct ndis_offload, rsc)

struct ndis_offload_params {
    struct ndis_object_header header;
    uint8_t ip_v4_csum;
    uint8_t tcp_ip_v4_csum;
    uint8_t udp_ip_v4_csum;
    uint8_t tcp_ip_v6_csum;
    uint8_t udp_ip_v6_csum;
    uint8_t lso_v1;
    uint8_t ip_sec_v1;
    uint8_t lso_v2_ipv4;
    uint8_t lso_v2_ipv6;
    uint8_t tcp_connection_ip_v4;
    uint8_t tcp_connection_ip_v6;
    uint32_t flags;
    uint8_t ip_sec_v2;
    uint8_t ip_sec_v2_ip_v4;
    struct {
        uint8_t rsc_ip_v4;
        uint8_t rsc_ip_v6;
    };
    struct {
        uint8_t encapsulated_packet_task_offload;
        uint8_t encapsulation_types;
    };
};

struct ndis_tcp_ip_checksum_info {
    union {
        struct {
            uint32_t is_ipv4:1;
            uint32_t is_ipv6:1;
            uint32_t tcp_checksum:1;
            uint32_t udp_checksum:1;
            uint32_t ip_header_checksum:1;
            uint32_t reserved:11;
            uint32_t tcp_header_offset:10;
        } transmit;
        struct {
            uint32_t tcp_checksum_failed:1;
            uint32_t udp_checksum_failed:1;
            uint32_t ip_checksum_failed:1;
            uint32_t tcp_checksum_succeeded:1;
            uint32_t udp_checksum_succeeded:1;
            uint32_t ip_checksum_succeeded:1;
            uint32_t loopback:1;
            uint32_t tcp_checksum_value_invalid:1;
            uint32_t ip_checksum_value_invalid:1;
        } receive;
        uint32_t  value;
    };
};

struct ndis_tcp_lso_info {
    union {
        struct {
            uint32_t unused:30;
            uint32_t type:1;
            uint32_t reserved2:1;
        } transmit;
        struct {
            uint32_t mss:20;
            uint32_t tcp_header_offset:10;
            uint32_t type:1;
            uint32_t reserved2:1;
        } lso_v1_transmit;
        struct {
            uint32_t tcp_payload:30;
            uint32_t type:1;
            uint32_t reserved2:1;
        } lso_v1_transmit_complete;
        struct {
            uint32_t mss:20;
            uint32_t tcp_header_offset:10;
            uint32_t type:1;
            uint32_t ip_version:1;
        } lso_v2_transmit;
        struct {
            uint32_t reserved:30;
            uint32_t type:1;
            uint32_t reserved2:1;
        } lso_v2_transmit_complete;
        uint32_t  value;
    };
};

#define NDIS_VLAN_PPI_SIZE (sizeof(struct rndis_per_packet_info) + \
                            sizeof(struct ndis_pkt_8021q_info))

#define NDIS_CSUM_PPI_SIZE (sizeof(struct rndis_per_packet_info) + \
                            sizeof(struct ndis_tcp_ip_checksum_info))

#define NDIS_LSO_PPI_SIZE (sizeof(struct rndis_per_packet_info) + \
                           sizeof(struct ndis_tcp_lso_info))

#define NDIS_HASH_PPI_SIZE (sizeof(struct rndis_per_packet_info) + \
                            sizeof(uint32_t))

/* Total size of all PPI data */
#define NDIS_ALL_PPI_SIZE (NDIS_VLAN_PPI_SIZE + NDIS_CSUM_PPI_SIZE + \
                           NDIS_LSO_PPI_SIZE + NDIS_HASH_PPI_SIZE)

/* Format of Information buffer passed in a SetRequest for the OID */
/* OID_GEN_RNDIS_CONFIG_PARAMETER. */
struct rndis_config_parameter_info {
    uint32_t parameter_name_offset;
    uint32_t parameter_name_length;
    uint32_t parameter_type;
    uint32_t parameter_value_offset;
    uint32_t parameter_value_length;
};

/* Values for ParameterType in struct rndis_config_parameter_info */
#define RNDIS_CONFIG_PARAM_TYPE_INTEGER     0
#define RNDIS_CONFIG_PARAM_TYPE_STRING      2

/* CONDIS Miniport messages for connection oriented devices */
/* that do not implement a call manager. */

/* CoNdisMiniportCreateVc message */
struct rcondis_mp_create_vc {
    uint32_t req_id;
    uint32_t ndis_vc_handle;
};

/* Response to CoNdisMiniportCreateVc */
struct rcondis_mp_create_vc_complete {
    uint32_t req_id;
    uint32_t dev_vc_handle;
    uint32_t status;
};

/* CoNdisMiniportDeleteVc message */
struct rcondis_mp_delete_vc {
    uint32_t req_id;
    uint32_t dev_vc_handle;
};

/* Response to CoNdisMiniportDeleteVc */
struct rcondis_mp_delete_vc_complete {
    uint32_t req_id;
    uint32_t status;
};

/* CoNdisMiniportQueryRequest message */
struct rcondis_mp_query_request {
    uint32_t req_id;
    uint32_t request_type;
    uint32_t oid;
    uint32_t dev_vc_handle;
    uint32_t info_buflen;
    uint32_t info_buf_offset;
};

/* CoNdisMiniportSetRequest message */
struct rcondis_mp_set_request {
    uint32_t req_id;
    uint32_t request_type;
    uint32_t oid;
    uint32_t dev_vc_handle;
    uint32_t info_buflen;
    uint32_t info_buf_offset;
};

/* CoNdisIndicateStatus message */
struct rcondis_indicate_status {
    uint32_t ndis_vc_handle;
    uint32_t status;
    uint32_t status_buflen;
    uint32_t status_buf_offset;
};

/* CONDIS Call/VC parameters */
struct rcondis_specific_parameters {
    uint32_t parameter_type;
    uint32_t parameter_length;
    uint32_t parameter_lffset;
};

struct rcondis_media_parameters {
    uint32_t flags;
    uint32_t reserved1;
    uint32_t reserved2;
    struct rcondis_specific_parameters media_specific;
};

struct rndis_flowspec {
    uint32_t token_rate;
    uint32_t token_bucket_size;
    uint32_t peak_bandwidth;
    uint32_t latency;
    uint32_t delay_variation;
    uint32_t service_type;
    uint32_t max_sdu_size;
    uint32_t minimum_policed_size;
};

struct rcondis_call_manager_parameters {
    struct rndis_flowspec transmit;
    struct rndis_flowspec receive;
    struct rcondis_specific_parameters call_mgr_specific;
};

/* CoNdisMiniportActivateVc message */
struct rcondis_mp_activate_vc_request {
    uint32_t req_id;
    uint32_t flags;
    uint32_t dev_vc_handle;
    uint32_t media_params_offset;
    uint32_t media_params_length;
    uint32_t call_mgr_params_offset;
    uint32_t call_mgr_params_length;
};

/* Response to CoNdisMiniportActivateVc */
struct rcondis_mp_activate_vc_complete {
    uint32_t req_id;
    uint32_t status;
};

/* CoNdisMiniportDeactivateVc message */
struct rcondis_mp_deactivate_vc_request {
    uint32_t req_id;
    uint32_t flags;
    uint32_t dev_vc_handle;
};

/* Response to CoNdisMiniportDeactivateVc */
struct rcondis_mp_deactivate_vc_complete {
    uint32_t req_id;
    uint32_t status;
};


/* union with all of the RNDIS messages */
union rndis_message_container {
    struct rndis_packet pkt;
    struct rndis_initialize_request init_req;
    struct rndis_halt_request halt_req;
    struct rndis_query_request query_req;
    struct rndis_set_request set_req;
    struct rndis_reset_request reset_req;
    struct rndis_keepalive_request keep_alive_req;
    struct rndis_indicate_status indicate_status;
    struct rndis_initialize_complete init_complete;
    struct rndis_query_complete query_complete;
    struct rndis_set_complete set_complete;
    struct rndis_reset_complete reset_complete;
    struct rndis_keepalive_complete keep_alive_complete;
    struct rcondis_mp_create_vc co_miniport_create_vc;
    struct rcondis_mp_delete_vc co_miniport_delete_vc;
    struct rcondis_indicate_status co_indicate_status;
    struct rcondis_mp_activate_vc_request co_miniport_activate_vc;
    struct rcondis_mp_deactivate_vc_request co_miniport_deactivate_vc;
    struct rcondis_mp_create_vc_complete co_miniport_create_vc_complete;
    struct rcondis_mp_delete_vc_complete co_miniport_delete_vc_complete;
    struct rcondis_mp_activate_vc_complete co_miniport_activate_vc_complete;
    struct rcondis_mp_deactivate_vc_complete
        co_miniport_deactivate_vc_complete;
};

/* Remote NDIS message format */
struct rndis_msg_hdr {
    uint32_t msg_type;
    uint32_t msg_len;
};

#define NDIS_PACKET_TYPE_DIRECTED	0x00000001
#define NDIS_PACKET_TYPE_MULTICAST	0x00000002
#define NDIS_PACKET_TYPE_ALL_MULTICAST	0x00000004
#define NDIS_PACKET_TYPE_BROADCAST	0x00000008
#define NDIS_PACKET_TYPE_SOURCE_ROUTING	0x00000010
#define NDIS_PACKET_TYPE_PROMISCUOUS	0x00000020
#define NDIS_PACKET_TYPE_SMT		0x00000040
#define NDIS_PACKET_TYPE_ALL_LOCAL	0x00000080
#define NDIS_PACKET_TYPE_GROUP		0x00000100
#define NDIS_PACKET_TYPE_ALL_FUNCTIONAL	0x00000200
#define NDIS_PACKET_TYPE_FUNCTIONAL	0x00000400
#define NDIS_PACKET_TYPE_MAC_FRAME	0x00000800

#define TRANSPORT_INFO_NOT_IP   0
#define TRANSPORT_INFO_IPV4_TCP 0x01
#define TRANSPORT_INFO_IPV4_UDP 0x02
#define TRANSPORT_INFO_IPV6_TCP 0x10
#define TRANSPORT_INFO_IPV6_UDP 0x20

#endif
