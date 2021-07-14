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
#include "include/hw/sysbus.h"

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
#define S390_MAX_SOCKETS 8

#define TYPE_S390_TOPOLOGY_BOOK "topology book"
#define TYPE_S390_TOPOLOGY_BOOK_BUS "book-bus"
struct S390TopologyBook {
    DeviceState parent_obj;
    BusState *bus;
    uint8_t book_id;
    int cnt;
};
typedef struct S390TopologyBook S390TopologyBook;
OBJECT_DECLARE_SIMPLE_TYPE(S390TopologyBook, S390_TOPOLOGY_BOOK)
#define S390_MAX_BOOKS 8

#define TYPE_S390_TOPOLOGY_DRAWER "topology drawer"
#define TYPE_S390_TOPOLOGY_DRAWER_BUS "drawer-bus"
struct S390TopologyDrawer {
    DeviceState parent_obj;
    BusState *bus;
    uint8_t drawer_id;
    int cnt;
};
typedef struct S390TopologyDrawer S390TopologyDrawer;
OBJECT_DECLARE_SIMPLE_TYPE(S390TopologyDrawer, S390_TOPOLOGY_DRAWER)
#define S390_MAX_DRAWERS 5

#define TYPE_S390_TOPOLOGY_NODE "topology node"
#define TYPE_S390_TOPOLOGY_NODE_BUS "node-bus"
struct S390TopologyNode {
    SysBusDevice parent_obj;
    BusState *bus;
    uint8_t node_id;
    int cnt;
};
typedef struct S390TopologyNode S390TopologyNode;
OBJECT_DECLARE_SIMPLE_TYPE(S390TopologyNode, S390_TOPOLOGY_NODE)
#define S390_MAX_NODES 1

S390TopologyNode *s390_init_topology(void);

S390TopologyNode *s390_get_topology(void);
void s390_topology_setup(MachineState *ms);
void s390_topology_new_cpu(int core_id);

#define S390_PTF_REASON_NONE (0x00 << 8)
#define S390_PTF_REASON_DONE (0x01 << 8)
#define S390_PTF_REASON_BUSY (0x02 << 8)
extern int s390_topology_changed(void);

#define S390_TOPO_FC_MASK 0xffUL

#endif
