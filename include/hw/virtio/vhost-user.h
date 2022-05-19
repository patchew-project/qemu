/*
 * Copyright (c) 2017-2018 Intel Corporation
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_VIRTIO_VHOST_USER_H
#define HW_VIRTIO_VHOST_USER_H

#include "chardev/char-fe.h"
#include "hw/virtio/virtio.h"
#include <linux/vhost.h>
#include "sysemu/cryptodev.h"

#define VHOST_MEMORY_BASELINE_NREGIONS    8
#define VHOST_USER_F_PROTOCOL_FEATURES 30
#define VHOST_USER_SLAVE_MAX_FDS     8

/*
 * Maximum size of virtio device config space
 */
#define VHOST_USER_MAX_CONFIG_SIZE 256

#define VHOST_USER_PROTOCOL_FEATURE_MASK ((1 << VHOST_USER_PROTOCOL_F_MAX) - 1)

#define VHOST_MEMORY_MAX_NREGIONS    8

typedef enum VhostUserRequest {
    VHOST_USER_NONE = 0,
    VHOST_USER_GET_FEATURES = 1,
    VHOST_USER_SET_FEATURES = 2,
    VHOST_USER_SET_OWNER = 3,
    VHOST_USER_RESET_OWNER = 4,
    VHOST_USER_SET_MEM_TABLE = 5,
    VHOST_USER_SET_LOG_BASE = 6,
    VHOST_USER_SET_LOG_FD = 7,
    VHOST_USER_SET_VRING_NUM = 8,
    VHOST_USER_SET_VRING_ADDR = 9,
    VHOST_USER_SET_VRING_BASE = 10,
    VHOST_USER_GET_VRING_BASE = 11,
    VHOST_USER_SET_VRING_KICK = 12,
    VHOST_USER_SET_VRING_CALL = 13,
    VHOST_USER_SET_VRING_ERR = 14,
    VHOST_USER_GET_PROTOCOL_FEATURES = 15,
    VHOST_USER_SET_PROTOCOL_FEATURES = 16,
    VHOST_USER_GET_QUEUE_NUM = 17,
    VHOST_USER_SET_VRING_ENABLE = 18,
    VHOST_USER_SEND_RARP = 19,
    VHOST_USER_NET_SET_MTU = 20,
    VHOST_USER_SET_SLAVE_REQ_FD = 21,
    VHOST_USER_IOTLB_MSG = 22,
    VHOST_USER_SET_VRING_ENDIAN = 23,
    VHOST_USER_GET_CONFIG = 24,
    VHOST_USER_SET_CONFIG = 25,
    VHOST_USER_CREATE_CRYPTO_SESSION = 26,
    VHOST_USER_CLOSE_CRYPTO_SESSION = 27,
    VHOST_USER_POSTCOPY_ADVISE  = 28,
    VHOST_USER_POSTCOPY_LISTEN  = 29,
    VHOST_USER_POSTCOPY_END     = 30,
    VHOST_USER_GET_INFLIGHT_FD = 31,
    VHOST_USER_SET_INFLIGHT_FD = 32,
    VHOST_USER_GPU_SET_SOCKET = 33,
    VHOST_USER_RESET_DEVICE = 34,
    /* Message number 35 reserved for VHOST_USER_VRING_KICK. */
    VHOST_USER_GET_MAX_MEM_SLOTS = 36,
    VHOST_USER_ADD_MEM_REG = 37,
    VHOST_USER_REM_MEM_REG = 38,
    VHOST_USER_MAX
} VhostUserRequest;

typedef enum VhostUserSlaveRequest {
    VHOST_USER_SLAVE_NONE = 0,
    VHOST_USER_SLAVE_IOTLB_MSG = 1,
    VHOST_USER_SLAVE_CONFIG_CHANGE_MSG = 2,
    VHOST_USER_SLAVE_VRING_HOST_NOTIFIER_MSG = 3,
    VHOST_USER_SLAVE_MAX
}  VhostUserSlaveRequest;

typedef struct VhostUserMemoryRegion {
    uint64_t guest_phys_addr;
    uint64_t memory_size;
    uint64_t userspace_addr;
    uint64_t mmap_offset;
} VhostUserMemoryRegion;

typedef struct VhostUserMemory {
    uint32_t nregions;
    uint32_t padding;
    VhostUserMemoryRegion regions[VHOST_MEMORY_BASELINE_NREGIONS];
} VhostUserMemory;

typedef struct VhostUserMemRegMsg {
    uint64_t padding;
    VhostUserMemoryRegion region;
} VhostUserMemRegMsg;

typedef struct VhostUserLog {
    uint64_t mmap_size;
    uint64_t mmap_offset;
} VhostUserLog;

typedef struct VhostUserConfig {
    uint32_t offset;
    uint32_t size;
    uint32_t flags;
    uint8_t region[VHOST_USER_MAX_CONFIG_SIZE];
} VhostUserConfig;

#define VHOST_CRYPTO_SYM_HMAC_MAX_KEY_LEN    512
#define VHOST_CRYPTO_SYM_CIPHER_MAX_KEY_LEN  64

typedef struct VhostUserCryptoSession {
    /* session id for success, -1 on errors */
    int64_t session_id;
    CryptoDevBackendSymSessionInfo session_setup_data;
    uint8_t key[VHOST_CRYPTO_SYM_CIPHER_MAX_KEY_LEN];
    uint8_t auth_key[VHOST_CRYPTO_SYM_HMAC_MAX_KEY_LEN];
} VhostUserCryptoSession;

static VhostUserConfig c __attribute__ ((unused));
#define VHOST_USER_CONFIG_HDR_SIZE (sizeof(c.offset) \
                                   + sizeof(c.size) \
                                   + sizeof(c.flags))

typedef struct VhostUserVringArea {
    uint64_t u64;
    uint64_t size;
    uint64_t offset;
} VhostUserVringArea;

typedef struct VhostUserInflight {
    uint64_t mmap_size;
    uint64_t mmap_offset;
    uint16_t num_queues;
    uint16_t queue_size;
} VhostUserInflight;

typedef struct {
    VhostUserRequest request;

#define VHOST_USER_VERSION_MASK     (0x3)
#define VHOST_USER_REPLY_MASK       (0x1<<2)
#define VHOST_USER_NEED_REPLY_MASK  (0x1 << 3)
    uint32_t flags;
    uint32_t size; /* the following payload size */
} QEMU_PACKED VhostUserHeader;

typedef union {
#define VHOST_USER_VRING_IDX_MASK   (0xff)
#define VHOST_USER_VRING_NOFD_MASK  (0x1<<8)
        uint64_t u64;
        struct vhost_vring_state state;
        struct vhost_vring_addr addr;
        VhostUserMemory memory;
        VhostUserMemRegMsg mem_reg;
        VhostUserLog log;
        struct vhost_iotlb_msg iotlb;
        VhostUserConfig config;
        VhostUserCryptoSession session;
        VhostUserVringArea area;
        VhostUserInflight inflight;
} VhostUserPayload;

typedef struct VhostUserMsg {
    VhostUserHeader hdr;
    VhostUserPayload payload;
} QEMU_PACKED VhostUserMsg;

#define VHOST_USER_HDR_SIZE (sizeof(VhostUserHeader))

#define VHOST_USER_PAYLOAD_SIZE (sizeof(VhostUserPayload))

/* The version of the protocol we support */
#define VHOST_USER_VERSION    (0x1)

/**
 * VhostUserHostNotifier - notifier information for one queue
 * @rcu: rcu_head for cleanup
 * @mr: memory region of notifier
 * @addr: current mapped address
 * @unmap_addr: address to be un-mapped
 * @idx: virtioqueue index
 *
 * The VhostUserHostNotifier entries are re-used. When an old mapping
 * is to be released it is moved to @unmap_addr and @addr is replaced.
 * Once the RCU process has completed the unmap @unmap_addr is
 * cleared.
 */
typedef struct VhostUserHostNotifier {
    struct rcu_head rcu;
    MemoryRegion mr;
    void *addr;
    void *unmap_addr;
    int idx;
} VhostUserHostNotifier;

/**
 * VhostUserState - shared state for all vhost-user devices
 * @chr: the character backend for the socket
 * @notifiers: GPtrArray of @VhostUserHostnotifier
 * @memory_slots:
 */
typedef struct VhostUserState {
    CharBackend *chr;
    GPtrArray *notifiers;
    int memory_slots;
    bool supports_config;
} VhostUserState;

/**
 * vhost_user_init() - initialise shared vhost_user state
 * @user: allocated area for storing shared state
 * @chr: the chardev for the vhost socket
 * @errp: error handle
 *
 * User can either directly g_new() space for the state or embed
 * VhostUserState in their larger device structure and just point to
 * it.
 *
 * Return: true on success, false on error while setting errp.
 */
bool vhost_user_init(VhostUserState *user, CharBackend *chr, Error **errp);

/**
 * vhost_user_cleanup() - cleanup state
 * @user: ptr to use state
 *
 * Cleans up shared state and notifiers, callee is responsible for
 * freeing the @VhostUserState memory itself.
 */
void vhost_user_cleanup(VhostUserState *user);

#endif
