/*
 * CPU Topology
 *
 * Copyright 2021 IBM Corp.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */
#ifndef HW_S390X_CPU_TOPOLOGY_H
#define HW_S390X_CPU_TOPOLOGY_H

#include "hw/qdev-core.h"
#include "qom/object.h"

#define S390_TOPOLOGY_CPU_TYPE    0x03

#define S390_TOPOLOGY_POLARITY_H  0x00
#define S390_TOPOLOGY_POLARITY_VL 0x01
#define S390_TOPOLOGY_POLARITY_VM 0x02
#define S390_TOPOLOGY_POLARITY_VH 0x03

#define TYPE_S390_TOPOLOGY_CORES "topology cores"
struct S390TopologyCores {
    DeviceState parent_obj;
    uint8_t id;
    bool dedicated;
    uint8_t polarity;
    uint8_t cputype;
    uint16_t origin;
    uint64_t mask;
    int cnt;
};
typedef struct S390TopologyCores S390TopologyCores;
OBJECT_DECLARE_SIMPLE_TYPE(S390TopologyCores, S390_TOPOLOGY_CORES)

#define TYPE_S390_TOPOLOGY_SOCKET "topology socket"
#define TYPE_S390_TOPOLOGY_SOCKET_BUS "socket-bus"
struct S390TopologySocket {
    DeviceState parent_obj;
    BusState *bus;
    uint8_t socket_id;
    int cnt;
};
typedef struct S390TopologySocket S390TopologySocket;
OBJECT_DECLARE_SIMPLE_TYPE(S390TopologySocket, S390_TOPOLOGY_SOCKET)
#define S390_MAX_SOCKETS 4

#define TYPE_S390_TOPOLOGY_BOOK "topology book"
#define TYPE_S390_TOPOLOGY_BOOK_BUS "book-bus"
struct S390TopologyBook {
    SysBusDevice parent_obj;
    BusState *bus;
    uint8_t book_id;
    int cnt;
};
typedef struct S390TopologyBook S390TopologyBook;
OBJECT_DECLARE_SIMPLE_TYPE(S390TopologyBook, S390_TOPOLOGY_BOOK)
#define S390_MAX_BOOKS 1

S390TopologyBook *s390_init_topology(void);

S390TopologyBook *s390_get_topology(void);
void s390_topology_setup(MachineState *ms);
void s390_topology_new_cpu(int core_id);

#endif
