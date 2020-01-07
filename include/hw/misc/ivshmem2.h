/*
 * Inter-VM Shared Memory PCI device, Version 2.
 *
 * Copyright (c) Siemens AG, 2019
 *
 * Authors:
 *  Jan Kiszka <jan.kiszka@siemens.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.
 */
#ifndef IVSHMEM2_H
#define IVSHMEM2_H

#define IVSHMEM_PROTOCOL_VERSION    2

#define IVSHMEM_MSG_INIT            0
#define IVSHMEM_MSG_EVENT_FD        1
#define IVSHMEM_MSG_PEER_GONE       2

typedef struct IvshmemMsgHeader {
    uint32_t type;
    uint32_t len;
} IvshmemMsgHeader;

typedef struct IvshmemInitialInfo {
    IvshmemMsgHeader header;
    uint32_t version;
    uint32_t compatible_version;
    uint32_t id;
    uint32_t max_peers;
    uint32_t vectors;
    uint32_t protocol;
    uint64_t output_section_size;
} IvshmemInitialInfo;

typedef struct IvshmemEventFd {
    IvshmemMsgHeader header;
    uint32_t id;
    uint32_t vector;
} IvshmemEventFd;

typedef struct IvshmemPeerGone {
    IvshmemMsgHeader header;
    uint32_t id;
} IvshmemPeerGone;

#endif /* IVSHMEM2_H */
