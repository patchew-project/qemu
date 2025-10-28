/*
 * Virtio MSG - Message packing/unpacking functions.
 *
 * Copyright (c) 2024 Advanced Micro Devices, Inc.
 * Written by Edgar E. Iglesias <edgar.iglesias@amd.com>.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_VIRTIO_MSG_H
#define QEMU_VIRTIO_MSG_H

#include <stdint.h>
#include "standard-headers/linux/virtio_config.h"

enum {
    VIRTIO_MSG_NO_ERROR = 0,
    VIRTIO_MSG_ERROR_RETRY,
    VIRTIO_MSG_ERROR_TIMEOUT,
    VIRTIO_MSG_ERROR_UNSUPPORTED_MESSAGE_ID,
    VIRTIO_MSG_ERROR_BAD_MESSAGE,
    VIRTIO_MSG_ERROR_MEMORY, /* General memory error. */
};

enum {
    VIRTIO_MSG_DEVICE_INFO       = 0x02,
    VIRTIO_MSG_GET_FEATURES      = 0x03,
    VIRTIO_MSG_SET_FEATURES      = 0x04,
    VIRTIO_MSG_GET_CONFIG        = 0x05,
    VIRTIO_MSG_SET_CONFIG        = 0x06,
    VIRTIO_MSG_GET_DEVICE_STATUS = 0x07,
    VIRTIO_MSG_SET_DEVICE_STATUS = 0x08,
    VIRTIO_MSG_GET_VQUEUE        = 0x09,
    VIRTIO_MSG_SET_VQUEUE        = 0x0a,
    VIRTIO_MSG_RESET_VQUEUE      = 0x0b,
    VIRTIO_MSG_GET_SHM           = 0x0c, /* Not yet supported */
    VIRTIO_MSG_EVENT_CONFIG      = 0x40,
    VIRTIO_MSG_EVENT_AVAIL       = 0x41,
    VIRTIO_MSG_EVENT_USED        = 0x42,

    /* Generic bus messages.  */
    VIRTIO_MSG_BUS_GET_DEVICES   = 0x02,

    VIRTIO_MSG_MAX = VIRTIO_MSG_EVENT_USED,
};

#define VIRTIO_MSG_MAX_SIZE 48

#define VIRTIO_MSG_TYPE_RESPONSE  (1 << 0)
#define VIRTIO_MSG_TYPE_BUS       (1 << 1)

typedef struct VirtIOMSG {
    uint8_t type;
    uint8_t msg_id;
    uint16_t dev_num;
    uint16_t token;
    uint16_t msg_size;

    union {
        uint8_t payload_u8[40];

        struct {
            uint32_t device_id;
            uint32_t vendor_id;
            uint32_t num_feature_bits;
            uint32_t config_size;
            uint32_t max_vqs;
            uint16_t admin_vq_idx;
            uint16_t admin_vq_count;
        } QEMU_PACKED get_device_info_resp;
        struct {
            uint32_t index;
            uint32_t num;
        } QEMU_PACKED get_features;
        struct {
            uint32_t index;
            uint32_t num;
            uint32_t b32[];
        } QEMU_PACKED get_features_resp;
        struct {
            uint32_t index;
            uint32_t num;
            uint32_t b32[];
        } QEMU_PACKED set_features;
        struct {
            uint32_t offset;
            uint32_t size;
        } QEMU_PACKED get_config;
        struct {
            uint32_t generation;
            uint32_t offset;
            uint32_t size;
            uint8_t data[];
        } QEMU_PACKED get_config_resp;
        struct {
            uint32_t generation;
            uint32_t offset;
            uint32_t size;
            uint8_t data[];
        } QEMU_PACKED set_config;
        struct {
            uint32_t generation;
            uint32_t offset;
            uint32_t size;
            uint8_t data[];
        } QEMU_PACKED set_config_resp;
        struct {
            uint32_t status;
        } QEMU_PACKED get_device_status_resp;
        struct {
            uint32_t status;
        } QEMU_PACKED set_device_status;
        struct {
            uint32_t status;
        } QEMU_PACKED set_device_status_resp;
        struct {
            uint32_t index;
        } QEMU_PACKED get_vqueue;
        struct {
            uint32_t index;
            uint32_t max_size;
            uint32_t size;
            uint32_t reserved;
            uint64_t descriptor_addr;
            uint64_t driver_addr;
            uint64_t device_addr;
        } QEMU_PACKED get_vqueue_resp;
        struct {
            uint32_t index;
            uint32_t unused;
            uint32_t size;
            uint32_t reserved;
            uint64_t descriptor_addr;
            uint64_t driver_addr;
            uint64_t device_addr;
        } QEMU_PACKED set_vqueue;
        struct {
            uint32_t index;
        } QEMU_PACKED reset_vqueue;
        struct {
            uint32_t status;
            uint32_t generation;
            uint32_t offset;
            uint32_t size;
            uint8_t config_value[];
        } QEMU_PACKED event_config;
        struct {
            uint32_t index;
            uint32_t next_offset_wrap;
        } QEMU_PACKED event_avail;
        struct {
            uint32_t index;
        } QEMU_PACKED event_used;

        /* Generic bus messages.  */
        struct {
            uint16_t offset;
            uint16_t num;
        } QEMU_PACKED bus_get_devices;
        struct {
            uint16_t offset;
            uint16_t num;
            uint16_t next_offset;
            uint8_t data[];
        } QEMU_PACKED bus_get_devices_resp;
    };
} QEMU_PACKED VirtIOMSG;

/* Maximum number of 32b feature-blocks in a single message.  */
#define VIRTIO_MSG_MAX_FEATURE_NUM \
    ((VIRTIO_MSG_MAX_SIZE - offsetof(VirtIOMSG, get_features_resp.b32)) / 4)

/* Maximum amount of config-data in a single message, in bytes.  */
#define VIRTIO_MSG_MAX_CONFIG_BYTES \
    (VIRTIO_MSG_MAX_SIZE - offsetof(VirtIOMSG, set_config.data))

#define LE_TO_CPU(v)                                        \
{                                                           \
    if (sizeof(v) == 2) {                                   \
        v = le16_to_cpu(v);                                 \
    } else if (sizeof(v) == 4) {                            \
        v = le32_to_cpu(v);                                 \
    } else if (sizeof(v) == 8) {                            \
        v = le64_to_cpu(v);                                 \
    } else {                                                \
        g_assert_not_reached();                             \
    }                                                       \
}

static inline void virtio_msg_unpack_bus(VirtIOMSG *msg)
{
    switch (msg->msg_id) {
    case VIRTIO_MSG_BUS_GET_DEVICES:
        LE_TO_CPU(msg->bus_get_devices.offset);
        LE_TO_CPU(msg->bus_get_devices.num);
        break;
    default:
        break;
    }
}

/**
 * virtio_msg_unpack_resp: Unpacks a wire virtio message responses into
 *                         a host version
 * @msg: the virtio message to unpack
 *
 * See virtio_msg_unpack().
 */
static inline void virtio_msg_unpack_resp(VirtIOMSG *msg)
{
    int i;

    switch (msg->msg_id) {
    case VIRTIO_MSG_DEVICE_INFO:
        LE_TO_CPU(msg->get_device_info_resp.device_id);
        LE_TO_CPU(msg->get_device_info_resp.vendor_id);
        LE_TO_CPU(msg->get_device_info_resp.num_feature_bits);
        LE_TO_CPU(msg->get_device_info_resp.config_size);
        LE_TO_CPU(msg->get_device_info_resp.max_vqs);
        LE_TO_CPU(msg->get_device_info_resp.admin_vq_idx);
        LE_TO_CPU(msg->get_device_info_resp.admin_vq_count);
        break;
    case VIRTIO_MSG_GET_FEATURES:
        LE_TO_CPU(msg->get_features_resp.index);
        LE_TO_CPU(msg->get_features_resp.num);
        for (i = 0; i < VIRTIO_MSG_MAX_FEATURE_NUM &&
                    i < msg->get_features_resp.num; i++) {
            LE_TO_CPU(msg->get_features_resp.b32[i]);
        }
        break;
    case VIRTIO_MSG_GET_DEVICE_STATUS:
        LE_TO_CPU(msg->get_device_status_resp.status);
        break;
    case VIRTIO_MSG_GET_CONFIG:
        LE_TO_CPU(msg->get_config_resp.generation);
        LE_TO_CPU(msg->get_config_resp.offset);
        LE_TO_CPU(msg->get_config_resp.size);
        break;
    case VIRTIO_MSG_SET_CONFIG:
        LE_TO_CPU(msg->set_config_resp.generation);
        LE_TO_CPU(msg->set_config_resp.offset);
        LE_TO_CPU(msg->set_config_resp.size);
        break;
    case VIRTIO_MSG_GET_VQUEUE:
        LE_TO_CPU(msg->get_vqueue_resp.index);
        LE_TO_CPU(msg->get_vqueue_resp.max_size);
        LE_TO_CPU(msg->get_vqueue_resp.size);
        LE_TO_CPU(msg->get_vqueue_resp.descriptor_addr);
        LE_TO_CPU(msg->get_vqueue_resp.driver_addr);
        LE_TO_CPU(msg->get_vqueue_resp.device_addr);
        break;
    default:
        break;
    }
}

/**
 * virtio_msg_unpack: Unpacks a wire virtio message into a host version
 * @msg: the virtio message to unpack
 *
 * Virtio messages arriving on the virtio message bus have a specific
 * format (little-endian, packet encoding, etc). To simplify for the
 * the rest of the implementation, we have packers and unpackers that
 * convert the wire messages into host versions. This includes endianess
 * conversion and potentially future decoding and expansion.
 *
 * At the moment, we only do endian conversion, virtio_msg_unpack() should
 * get completely eliminated by the compiler when targetting little-endian
 * hosts.
 */
static inline void virtio_msg_unpack(VirtIOMSG *msg)
{
    int i;

    LE_TO_CPU(msg->dev_num);
    LE_TO_CPU(msg->token);
    LE_TO_CPU(msg->msg_size);

    if (msg->type & VIRTIO_MSG_TYPE_BUS) {
        virtio_msg_unpack_bus(msg);
        return;
    }

    if (msg->type & VIRTIO_MSG_TYPE_RESPONSE) {
        virtio_msg_unpack_resp(msg);
        return;
    }

    switch (msg->msg_id) {
    case VIRTIO_MSG_GET_FEATURES:
        LE_TO_CPU(msg->get_features.index);
        LE_TO_CPU(msg->get_features.num);
        break;
    case VIRTIO_MSG_SET_FEATURES:
        LE_TO_CPU(msg->set_features.index);
        LE_TO_CPU(msg->set_features.num);
        for (i = 0; i < VIRTIO_MSG_MAX_FEATURE_NUM &&
                    i < msg->set_features.num; i++) {
            LE_TO_CPU(msg->set_features.b32[i]);
        }
        break;
    case VIRTIO_MSG_SET_DEVICE_STATUS:
        LE_TO_CPU(msg->set_device_status.status);
        break;
    case VIRTIO_MSG_GET_CONFIG:
        LE_TO_CPU(msg->get_config.offset);
        LE_TO_CPU(msg->get_config.size);
        break;
    case VIRTIO_MSG_SET_CONFIG:
        LE_TO_CPU(msg->set_config.generation);
        LE_TO_CPU(msg->set_config.offset);
        LE_TO_CPU(msg->set_config.size);
        break;
    case VIRTIO_MSG_GET_VQUEUE:
        LE_TO_CPU(msg->get_vqueue.index);
        break;
    case VIRTIO_MSG_SET_VQUEUE:
        LE_TO_CPU(msg->set_vqueue.index);
        LE_TO_CPU(msg->set_vqueue.size);
        LE_TO_CPU(msg->set_vqueue.descriptor_addr);
        LE_TO_CPU(msg->set_vqueue.driver_addr);
        LE_TO_CPU(msg->set_vqueue.device_addr);
        break;
    case VIRTIO_MSG_RESET_VQUEUE:
        LE_TO_CPU(msg->reset_vqueue.index);
        break;
    case VIRTIO_MSG_EVENT_CONFIG:
        LE_TO_CPU(msg->event_config.status);
        LE_TO_CPU(msg->event_config.generation);
        LE_TO_CPU(msg->event_config.offset);
        LE_TO_CPU(msg->event_config.size);
        break;
    case VIRTIO_MSG_EVENT_AVAIL:
        LE_TO_CPU(msg->event_avail.index);
        LE_TO_CPU(msg->event_avail.next_offset_wrap);
        break;
    case VIRTIO_MSG_EVENT_USED:
        LE_TO_CPU(msg->event_used.index);
        break;
    default:
        break;
    }
}

static inline size_t virtio_msg_header_size(void)
{
    return offsetof(VirtIOMSG, payload_u8);
}

static inline void virtio_msg_pack_header(VirtIOMSG *msg,
                                          uint8_t msg_id,
                                          uint8_t type,
                                          uint16_t dev_num,
                                          uint16_t token,
                                          uint16_t payload_size)
{
    uint16_t msg_size = virtio_msg_header_size() + payload_size;

    msg->type = type;
    msg->msg_id = msg_id;
    msg->dev_num = cpu_to_le16(dev_num);
    msg->token = cpu_to_le16(token);
    msg->msg_size = cpu_to_le16(msg_size);

    /* Keep things predictable.  */
    memset(msg->payload_u8, 0, sizeof msg->payload_u8);
}

static inline void virtio_msg_pack_get_device_info(VirtIOMSG *msg,
                                                   uint16_t dev_num,
                                                   uint16_t token)
{
    virtio_msg_pack_header(msg, VIRTIO_MSG_DEVICE_INFO, 0, dev_num, token, 0);
}

static inline void virtio_msg_pack_get_device_info_resp(VirtIOMSG *msg,
                                                   uint16_t dev_num,
                                                   uint16_t token,
                                                   uint32_t device_id,
                                                   uint32_t vendor_id,
                                                   uint32_t num_feature_bits,
                                                   uint32_t config_size,
                                                   uint32_t max_vqs,
                                                   uint16_t admin_vq_idx,
                                                   uint16_t admin_vq_count)
{
    virtio_msg_pack_header(msg, VIRTIO_MSG_DEVICE_INFO,
                           VIRTIO_MSG_TYPE_RESPONSE, dev_num, token,
                           sizeof msg->get_device_info_resp);

    msg->get_device_info_resp.device_id = cpu_to_le32(device_id);
    msg->get_device_info_resp.vendor_id = cpu_to_le32(vendor_id);
    msg->get_device_info_resp.num_feature_bits = cpu_to_le32(num_feature_bits);
    msg->get_device_info_resp.config_size = cpu_to_le32(config_size);
    msg->get_device_info_resp.max_vqs = cpu_to_le32(max_vqs);
    msg->get_device_info_resp.admin_vq_idx = cpu_to_le16(admin_vq_idx);
    msg->get_device_info_resp.admin_vq_count = cpu_to_le16(admin_vq_count);
}

static inline void virtio_msg_pack_get_features(VirtIOMSG *msg,
                                                uint16_t dev_num,
                                                uint16_t token,
                                                uint32_t index,
                                                uint32_t num)
{
    virtio_msg_pack_header(msg, VIRTIO_MSG_GET_FEATURES, 0, dev_num, token,
                           sizeof msg->get_features);

    msg->get_features.index = cpu_to_le32(index);
    msg->get_features.num = cpu_to_le32(num);
}

static inline void virtio_msg_pack_get_features_resp(VirtIOMSG *msg,
                                                     uint16_t dev_num,
                                                     uint16_t token,
                                                     uint32_t index,
                                                     uint32_t num,
                                                     uint32_t *f)
{
    unsigned int i;

    virtio_msg_pack_header(msg, VIRTIO_MSG_GET_FEATURES,
                           VIRTIO_MSG_TYPE_RESPONSE, dev_num, token,
                           sizeof msg->get_features_resp + num * sizeof(*f));

    msg->get_features_resp.index = cpu_to_le32(index);
    msg->get_features_resp.num = cpu_to_le32(num);

    assert(num <= VIRTIO_MSG_MAX_FEATURE_NUM);

    for (i = 0; i < num && i < VIRTIO_MSG_MAX_FEATURE_NUM; i++) {
        msg->get_features_resp.b32[i] = cpu_to_le32(f[i]);
    }
}

static inline void virtio_msg_pack_set_features(VirtIOMSG *msg,
                                                uint16_t dev_num,
                                                uint16_t token,
                                                uint32_t index,
                                                uint32_t num,
                                                uint32_t *f)
{
    unsigned int i;

    virtio_msg_pack_header(msg, VIRTIO_MSG_SET_FEATURES, 0, dev_num, token,
                           sizeof msg->set_features + num * sizeof(*f));

    msg->set_features.index = cpu_to_le32(index);
    msg->set_features.num = cpu_to_le32(num);

    assert(num <= VIRTIO_MSG_MAX_FEATURE_NUM);

    for (i = 0; i < num && i < VIRTIO_MSG_MAX_FEATURE_NUM; i++) {
        msg->set_features.b32[i] = cpu_to_le32(f[i]);
    }
}

static inline void virtio_msg_pack_set_features_resp(VirtIOMSG *msg,
                                                     uint16_t dev_num,
                                                     uint16_t token)
{
    virtio_msg_pack_header(msg, VIRTIO_MSG_SET_FEATURES,
                           VIRTIO_MSG_TYPE_RESPONSE, dev_num, token, 0);
}

static inline void virtio_msg_pack_set_device_status(VirtIOMSG *msg,
                                                     uint16_t dev_num,
                                                     uint16_t token,
                                                     uint32_t status)
{
    virtio_msg_pack_header(msg, VIRTIO_MSG_SET_DEVICE_STATUS, 0, dev_num,
                           token, sizeof msg->set_device_status);

    msg->set_device_status.status = cpu_to_le32(status);
}

static inline void virtio_msg_pack_set_device_status_resp(VirtIOMSG *msg,
                                                          uint16_t dev_num,
                                                          uint16_t token,
                                                          uint32_t status)
{
    virtio_msg_pack_header(msg, VIRTIO_MSG_SET_DEVICE_STATUS,
                           VIRTIO_MSG_TYPE_RESPONSE, dev_num, token,
                           sizeof msg->set_device_status_resp);

    msg->set_device_status_resp.status = cpu_to_le32(status);
}

static inline void virtio_msg_pack_get_device_status(VirtIOMSG *msg,
                                                     uint16_t dev_num,
                                                     uint16_t token)
{
    virtio_msg_pack_header(msg, VIRTIO_MSG_GET_DEVICE_STATUS, 0,
                           dev_num, token, 0);
}

static inline void virtio_msg_pack_get_device_status_resp(VirtIOMSG *msg,
                                                          uint16_t dev_num,
                                                          uint16_t token,
                                                          uint32_t status)
{
    virtio_msg_pack_header(msg, VIRTIO_MSG_GET_DEVICE_STATUS,
                           VIRTIO_MSG_TYPE_RESPONSE, dev_num, token,
                           sizeof msg->get_device_status_resp);

    msg->get_device_status_resp.status = cpu_to_le32(status);
}

static inline void virtio_msg_pack_get_config(VirtIOMSG *msg,
                                              uint16_t dev_num,
                                              uint16_t token,
                                              uint32_t size,
                                              uint32_t offset)
{
    virtio_msg_pack_header(msg, VIRTIO_MSG_GET_CONFIG, 0, dev_num, token,
                           sizeof msg->get_config);

    msg->get_config.offset = cpu_to_le32(offset);
    msg->get_config.size = cpu_to_le32(size);
}

static inline void virtio_msg_pack_get_config_resp(VirtIOMSG *msg,
                                                   uint16_t dev_num,
                                                   uint16_t token,
                                                   uint32_t size,
                                                   uint32_t offset,
                                                   uint32_t generation,
                                                   uint8_t data[])
{
    assert(size <= VIRTIO_MSG_MAX_CONFIG_BYTES);

    virtio_msg_pack_header(msg, VIRTIO_MSG_GET_CONFIG,
                           VIRTIO_MSG_TYPE_RESPONSE, dev_num, token,
                           sizeof msg->get_config_resp + size);

    msg->get_config_resp.generation = cpu_to_le32(generation);
    msg->get_config_resp.offset = cpu_to_le32(offset);
    msg->get_config_resp.size = cpu_to_le32(size);

    memcpy(&msg->get_config_resp.data, data, size);
}

static inline void virtio_msg_pack_set_config(VirtIOMSG *msg,
                                              uint16_t dev_num,
                                              uint16_t token,
                                              uint32_t size,
                                              uint32_t offset,
                                              uint32_t generation,
                                              uint8_t data[])
{
    assert(size <= VIRTIO_MSG_MAX_CONFIG_BYTES);

    virtio_msg_pack_header(msg, VIRTIO_MSG_SET_CONFIG, 0, dev_num, token,
                           sizeof msg->set_config + size);

    msg->set_config.offset = cpu_to_le32(offset);
    msg->set_config.size = cpu_to_le32(size);
    msg->set_config.generation = cpu_to_le32(generation);

    memcpy(&msg->set_config.data, data, size);
}

static inline void virtio_msg_pack_set_config_resp(VirtIOMSG *msg,
                                                   uint16_t dev_num,
                                                   uint16_t token,
                                                   uint32_t size,
                                                   uint32_t offset,
                                                   uint32_t generation,
                                                   uint8_t data[])
{
    assert(size <= VIRTIO_MSG_MAX_CONFIG_BYTES);

    virtio_msg_pack_header(msg, VIRTIO_MSG_SET_CONFIG,
                           VIRTIO_MSG_TYPE_RESPONSE, dev_num, token,
                           sizeof msg->set_config_resp + size);

    msg->set_config_resp.offset = cpu_to_le32(offset);
    msg->set_config_resp.size = cpu_to_le32(size);
    msg->set_config_resp.generation = cpu_to_le32(generation);

    memcpy(&msg->set_config_resp.data, data, size);
}

static inline void virtio_msg_pack_get_vqueue(VirtIOMSG *msg,
                                              uint16_t dev_num,
                                              uint16_t token,
                                              uint32_t index)
{
    virtio_msg_pack_header(msg, VIRTIO_MSG_GET_VQUEUE, 0, dev_num, token,
                           sizeof msg->get_vqueue);

    msg->get_vqueue.index = cpu_to_le32(index);
}

static inline void virtio_msg_pack_get_vqueue_resp(VirtIOMSG *msg,
                                                   uint16_t dev_num,
                                                   uint16_t token,
                                                   uint32_t index,
                                                   uint32_t max_size,
                                                   uint32_t size,
                                                   uint64_t descriptor_addr,
                                                   uint64_t driver_addr,
                                                   uint64_t device_addr)
{
    virtio_msg_pack_header(msg, VIRTIO_MSG_GET_VQUEUE,
                           VIRTIO_MSG_TYPE_RESPONSE, dev_num, token,
                           sizeof msg->get_vqueue_resp);

    msg->get_vqueue_resp.index = cpu_to_le32(index);
    msg->get_vqueue_resp.max_size = cpu_to_le32(max_size);
    msg->get_vqueue_resp.size = cpu_to_le32(size);
    msg->get_vqueue_resp.descriptor_addr = cpu_to_le64(descriptor_addr);
    msg->get_vqueue_resp.driver_addr = cpu_to_le64(driver_addr);
    msg->get_vqueue_resp.device_addr = cpu_to_le64(device_addr);
}

static inline void virtio_msg_pack_reset_vqueue(VirtIOMSG *msg,
                                                uint16_t dev_num,
                                                uint16_t token,
                                                uint32_t index)
{
    virtio_msg_pack_header(msg, VIRTIO_MSG_RESET_VQUEUE, 0, dev_num, token,
                           sizeof msg->reset_vqueue);

    msg->reset_vqueue.index = cpu_to_le32(index);
}

static inline void virtio_msg_pack_reset_vqueue_resp(VirtIOMSG *msg,
                                                     uint16_t dev_num,
                                                     uint16_t token)
{
    virtio_msg_pack_header(msg, VIRTIO_MSG_RESET_VQUEUE,
                           VIRTIO_MSG_TYPE_RESPONSE, dev_num, token, 0);
}

static inline void virtio_msg_pack_set_vqueue(VirtIOMSG *msg,
                                              uint16_t dev_num,
                                              uint16_t token,
                                              uint32_t index,
                                              uint32_t size,
                                              uint64_t descriptor_addr,
                                              uint64_t driver_addr,
                                              uint64_t device_addr)
{
    virtio_msg_pack_header(msg, VIRTIO_MSG_SET_VQUEUE, 0, dev_num, token,
                           sizeof msg->set_vqueue);

    msg->set_vqueue.index = cpu_to_le32(index);
    msg->set_vqueue.unused = 0;
    msg->set_vqueue.size = cpu_to_le32(size);
    msg->set_vqueue.descriptor_addr = cpu_to_le64(descriptor_addr);
    msg->set_vqueue.driver_addr = cpu_to_le64(driver_addr);
    msg->set_vqueue.device_addr = cpu_to_le64(device_addr);
}

static inline void virtio_msg_pack_set_vqueue_resp(VirtIOMSG *msg,
                                                   uint16_t dev_num,
                                                   uint16_t token)
{
    virtio_msg_pack_header(msg, VIRTIO_MSG_SET_VQUEUE,
                           VIRTIO_MSG_TYPE_RESPONSE, dev_num, token, 0);
}

static inline void virtio_msg_pack_event_avail(VirtIOMSG *msg,
                                               uint16_t dev_num,
                                               uint32_t index,
                                               uint32_t next_offset,
                                               bool next_wrap)
{
    uint32_t next_ow;

    virtio_msg_pack_header(msg, VIRTIO_MSG_EVENT_AVAIL, 0, dev_num, 0,
                           sizeof msg->event_avail);

    /* next_offset is 31b wide.  */
    assert((next_offset & 0x80000000U) == 0);

    /* Pack the next_offset_wrap field. */
    next_ow = next_wrap << 31;
    next_ow |= next_offset;

    msg->event_avail.index = cpu_to_le32(index);
    msg->event_avail.next_offset_wrap = cpu_to_le32(next_ow);
}

static inline void virtio_msg_pack_event_used(VirtIOMSG *msg,
                                              uint16_t dev_num,
                                              uint32_t index)
{
    virtio_msg_pack_header(msg, VIRTIO_MSG_EVENT_USED, 0, dev_num, 0,
                           sizeof msg->event_used);

    msg->event_used.index = cpu_to_le32(index);
}

static inline void virtio_msg_pack_event_config(VirtIOMSG *msg,
                                                uint16_t dev_num,
                                                uint32_t status,
                                                uint32_t generation,
                                                uint32_t offset,
                                                uint32_t size,
                                                uint8_t *value)
{
    unsigned int max_size;

    virtio_msg_pack_header(msg, VIRTIO_MSG_EVENT_CONFIG, 0, dev_num, 0,
                           sizeof msg->event_config);

    msg->event_config.status = cpu_to_le32(status);
    msg->event_config.generation = cpu_to_le32(generation);
    msg->event_config.offset = cpu_to_le32(offset);
    msg->event_config.size = cpu_to_le32(size);

    max_size = VIRTIO_MSG_MAX_SIZE;
    max_size -= offsetof(VirtIOMSG, event_config.config_value);
    assert(size <= max_size);

    if (size > 0 && size <= max_size) {
        memcpy(&msg->event_config.config_value[0], value, size);
    }
}

static inline void virtio_msg_pack_bus_get_devices(VirtIOMSG *msg,
                                                   uint16_t offset,
                                                   uint16_t num)
{
    virtio_msg_pack_header(msg,
                           VIRTIO_MSG_BUS_GET_DEVICES, VIRTIO_MSG_TYPE_BUS,
                           0, 0, sizeof msg->bus_get_devices);

    msg->bus_get_devices.offset = cpu_to_le16(offset);
    msg->bus_get_devices.num = cpu_to_le16(num);
}

static inline void virtio_msg_pack_bus_get_devices_resp(VirtIOMSG *msg,
                                                        uint16_t offset,
                                                        uint16_t num,
                                                        uint16_t next_offset,
                                                        uint8_t *data)
{
    virtio_msg_pack_header(msg,
                           VIRTIO_MSG_BUS_GET_DEVICES,
                           VIRTIO_MSG_TYPE_BUS | VIRTIO_MSG_TYPE_RESPONSE,
                           0, 0, sizeof msg->bus_get_devices_resp + num);

    msg->bus_get_devices_resp.offset = cpu_to_le16(offset);
    msg->bus_get_devices_resp.num = cpu_to_le16(num);
    msg->bus_get_devices_resp.next_offset = cpu_to_le16(next_offset);

    memcpy(msg->bus_get_devices_resp.data, data, num / 8);
}

static inline const char *virtio_msg_id_to_str(unsigned int type)
{
#define VIRTIO_MSG_TYPE2STR(x) [VIRTIO_MSG_ ## x] = stringify(x)
    static const char *type2str[VIRTIO_MSG_MAX + 1] = {
        VIRTIO_MSG_TYPE2STR(DEVICE_INFO),
        VIRTIO_MSG_TYPE2STR(GET_FEATURES),
        VIRTIO_MSG_TYPE2STR(SET_FEATURES),
        VIRTIO_MSG_TYPE2STR(GET_CONFIG),
        VIRTIO_MSG_TYPE2STR(SET_CONFIG),
        VIRTIO_MSG_TYPE2STR(GET_DEVICE_STATUS),
        VIRTIO_MSG_TYPE2STR(SET_DEVICE_STATUS),
        VIRTIO_MSG_TYPE2STR(GET_VQUEUE),
        VIRTIO_MSG_TYPE2STR(SET_VQUEUE),
        VIRTIO_MSG_TYPE2STR(RESET_VQUEUE),
        VIRTIO_MSG_TYPE2STR(EVENT_CONFIG),
        VIRTIO_MSG_TYPE2STR(EVENT_AVAIL),
        VIRTIO_MSG_TYPE2STR(EVENT_USED),
    };

    return type2str[type];
}

static inline void virtio_msg_print_status(uint32_t status)
{
    printf("status %x", status);

    if (status & VIRTIO_CONFIG_S_ACKNOWLEDGE) {
        printf(" ACKNOWLEDGE");
    }
    if (status & VIRTIO_CONFIG_S_DRIVER) {
        printf(" DRIVER");
    }
    if (status & VIRTIO_CONFIG_S_DRIVER_OK) {
        printf(" DRIVER_OK");
    }
    if (status & VIRTIO_CONFIG_S_FEATURES_OK) {
        printf(" FEATURES_OK");
    }
    if (status & VIRTIO_CONFIG_S_NEEDS_RESET) {
        printf(" NEEDS_RESET");
    }
    if (status & VIRTIO_CONFIG_S_FAILED) {
        printf(" FAILED");
    }

    printf("\n");
}

static inline void virtio_msg_print(VirtIOMSG *msg)
{
    bool resp = msg->type & VIRTIO_MSG_TYPE_RESPONSE;
    size_t payload_size;
    int i;

    assert(msg);
    printf("virtio-msg: id %s 0x%x type 0x%x dev_num 0x%x msg_size 0x%x\n",
           virtio_msg_id_to_str(msg->msg_id), msg->msg_id,
           msg->type, msg->dev_num, msg->msg_size);

    payload_size = msg->msg_size - offsetof(VirtIOMSG, payload_u8);
    if (payload_size > ARRAY_SIZE(msg->payload_u8)) {
        printf("Size overflow! %zu > %zu\n",
                payload_size, ARRAY_SIZE(msg->payload_u8));
        payload_size = ARRAY_SIZE(msg->payload_u8);
    }

    for (i = 0; i < payload_size; i++) {
        printf("%2.2x ", msg->payload_u8[i]);
        if (((i + 1) %  16) == 0) {
            printf("\n");
        }
    }

    switch (msg->msg_id) {
    case VIRTIO_MSG_GET_DEVICE_STATUS:
        if (resp) {
            virtio_msg_print_status(msg->get_device_status_resp.status);
        }
        break;
    case VIRTIO_MSG_SET_DEVICE_STATUS:
        virtio_msg_print_status(msg->set_device_status.status);
        break;
    case VIRTIO_MSG_SET_VQUEUE:
        printf("set-vqueue: index=%d size=%d desc-addr=%lx "
               "driver-addr=%lx device-addr=%lx\n",
               msg->set_vqueue.index, msg->set_vqueue.size,
               msg->set_vqueue.descriptor_addr,
               msg->set_vqueue.driver_addr,
               msg->set_vqueue.device_addr);
        break;
    }
    printf("\n");
}
#endif
