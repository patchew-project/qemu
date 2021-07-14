/*
 * CPU Topology
 *
 * Copyright 2021 IBM Corp.
 * Author(s): Pierre Morel <pmorel@linux.ibm.com>

 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/sysbus.h"
#include "hw/s390x/cpu-topology.h"
#include "hw/qdev-properties.h"
#include "hw/boards.h"
#include "qemu/typedefs.h"
#include "hw/s390x/s390-virtio-ccw.h"

int s390_topology_changed(void)
{
    const MachineState *ms = MACHINE(qdev_get_machine());
    S390CcwMachineState *s390ms = S390_CCW_MACHINE(ms);

    if (s390ms->topology_changed) {
        s390ms->topology_changed = 0;
        return 1;
    }
    return 0;
}

static S390TopologyCores *s390_create_cores(S390TopologySocket *socket,
                                            int origin)
{
    DeviceState *dev;
    S390TopologyCores *cores;
    const MachineState *ms = MACHINE(qdev_get_machine());
    S390CcwMachineState *s390ms = S390_CCW_MACHINE(ms);

    if (socket->bus->num_children >= ms->smp.cores) {
        return NULL;
    }

    dev = qdev_new(TYPE_S390_TOPOLOGY_CORES);
    qdev_realize_and_unref(dev, socket->bus, &error_fatal);

    cores = S390_TOPOLOGY_CORES(dev);
    cores->origin = origin;
    socket->cnt += 1;
    s390ms->topology_changed = 1;

    return cores;
}

static S390TopologySocket *s390_create_socket(S390TopologyBook *book, int id)
{
    DeviceState *dev;
    S390TopologySocket *socket;
    const MachineState *ms = MACHINE(qdev_get_machine());
    S390CcwMachineState *s390ms = S390_CCW_MACHINE(ms);

    if (book->bus->num_children >= ms->smp.sockets) {
        return NULL;
    }

    dev = qdev_new(TYPE_S390_TOPOLOGY_SOCKET);
    qdev_realize_and_unref(dev, book->bus, &error_fatal);

    socket = S390_TOPOLOGY_SOCKET(dev);
    socket->socket_id = id;
    book->cnt++;
    s390ms->topology_changed = 1;

    return socket;
}

static S390TopologyBook *s390_create_book(S390TopologyDrawer *drawer, int id)
{
    S390TopologyBook *book;
    DeviceState *dev;
    const MachineState *ms = MACHINE(qdev_get_machine());
    S390CcwMachineState *s390ms = S390_CCW_MACHINE(ms);

    if (drawer->bus->num_children >= s390ms->books) {
        return NULL;
    }

    dev = qdev_new(TYPE_S390_TOPOLOGY_BOOK);
    qdev_realize_and_unref(dev, drawer->bus, &error_fatal);

    book = S390_TOPOLOGY_BOOK(dev);
    book->book_id = id;
    drawer->cnt++;
    s390ms->topology_changed = 1;

    return book;
}

static S390TopologyDrawer *s390_create_drawer(S390TopologyNode *node, int id)
{
    S390TopologyDrawer *drawer;
    DeviceState *dev;
    const MachineState *ms = MACHINE(qdev_get_machine());
    S390CcwMachineState *s390ms = S390_CCW_MACHINE(ms);

    if (node->bus->num_children >= s390ms->drawers) {
        return NULL;
    }

    dev = qdev_new(TYPE_S390_TOPOLOGY_DRAWER);
    qdev_realize_and_unref(dev, node->bus, &error_fatal);

    drawer = S390_TOPOLOGY_DRAWER(dev);
    drawer->drawer_id = id;
    node->cnt++;
    s390ms->topology_changed = 1;

    return drawer;
}

static S390TopologyCores *s390_get_cores(S390TopologySocket *socket, int origin)
{
    S390TopologyCores *cores;
    BusChild *kid;

    QTAILQ_FOREACH(kid, &socket->bus->children, sibling) {
        cores = S390_TOPOLOGY_CORES(kid->child);
        if (cores->origin == origin) {
            return cores;
        }
    }
    return s390_create_cores(socket, origin);
}

static S390TopologySocket *s390_get_socket(S390TopologyBook *book,
                                           int socket_id)
{
    S390TopologySocket *socket;
    BusChild *kid;

    QTAILQ_FOREACH(kid, &book->bus->children, sibling) {
        socket = S390_TOPOLOGY_SOCKET(kid->child);
        if (socket->socket_id == socket_id) {
            return socket;
        }
    }
    return s390_create_socket(book, socket_id);
}

static S390TopologyBook *s390_get_book(S390TopologyDrawer *drawer, int book_id)
{
    S390TopologyBook *book;
    BusChild *kid;

    QTAILQ_FOREACH(kid, &drawer->bus->children, sibling) {
        book = S390_TOPOLOGY_BOOK(kid->child);
        if (book->book_id == book_id) {
            return book;
        }
    }
    return s390_create_book(drawer, book_id);
}

static S390TopologyDrawer *s390_get_drawer(S390TopologyNode *node,
                                           int drawer_id)
{
    S390TopologyDrawer *drawer;
    BusChild *kid;

    QTAILQ_FOREACH(kid, &node->bus->children, sibling) {
        drawer = S390_TOPOLOGY_DRAWER(kid->child);
        if (drawer->drawer_id == drawer_id) {
            return drawer;
        }
    }
    return s390_create_drawer(node, drawer_id);
}

/*
 * s390_topology_new_cpu:
 * @core_id: the core ID is machine wide
 *
 * We have a single node returned by s390_get_topology(),
 * then we build the hierarchy on demand.
 * Note that we do not destroy the hierarchy on error creating
 * an entry in the topology, we just keept it empty.
 * We do not need to worry about not finding a topology level
 * entry this would have been catched during smp parsing.
 */
void s390_topology_new_cpu(int core_id)
{
    const MachineState *ms = MACHINE(qdev_get_machine());
    S390CcwMachineState *s390ms = S390_CCW_MACHINE(ms);
    S390TopologyDrawer *drawer;
    S390TopologyBook *book;
    S390TopologySocket *socket;
    S390TopologyCores *cores;
    S390TopologyNode *node;
    int cores_per_drawer, cores_per_book;
    int drawer_idx, book_idx, sock_idx;
    int origin, bit;

    cores_per_drawer = ms->smp.max_cpus / s390ms->drawers;
    cores_per_book = cores_per_drawer / s390ms->books;

    node = s390_get_topology();

    drawer_idx = core_id / cores_per_drawer;
    drawer = s390_get_drawer(node, drawer_idx);

    book_idx = (core_id % cores_per_drawer) / cores_per_book;
    book = s390_get_book(drawer, book_idx);

    sock_idx = (core_id % cores_per_book) / ms->smp.cores;
    socket = s390_get_socket(book, sock_idx);

    /*
     * We assert that all CPU are identical IFL, shared and
     * horizontal topology, the only reason to have several
     * S390TopologyCores is to have more than 64 CPUs.
     */
    origin = 64 * (core_id / 64);

    cores = s390_get_cores(socket, origin);

    bit = 63 - (core_id - origin);
    set_bit(bit, &cores->mask);
    cores->origin = origin;
    s390ms->topology_changed = 1;
}

/*
 * Setting the first topology: 1 node, 1 drawer, 1 book, 1 socket
 * This is enough for 64 cores if the topology is flat (single socket)
 */
void s390_topology_setup(MachineState *ms)
{
    DeviceState *dev;

    /* Create NODE bridge device */
    dev = qdev_new(TYPE_S390_TOPOLOGY_NODE);
    object_property_add_child(qdev_get_machine(),
                              TYPE_S390_TOPOLOGY_NODE, OBJECT(dev));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
}

S390TopologyNode *s390_get_topology(void)
{
    static S390TopologyNode *node;

    if (!node) {
        node = S390_TOPOLOGY_NODE(
            object_resolve_path(TYPE_S390_TOPOLOGY_NODE, NULL));
        assert(node != NULL);
    }

    return node;
}

/* --- CORES Definitions --- */

static Property s390_topology_cores_properties[] = {
    DEFINE_PROP_BOOL("dedicated", S390TopologyCores, dedicated, false),
    DEFINE_PROP_UINT8("polarity", S390TopologyCores, polarity,
                      S390_TOPOLOGY_POLARITY_H),
    DEFINE_PROP_UINT8("cputype", S390TopologyCores, cputype,
                      S390_TOPOLOGY_CPU_TYPE),
    DEFINE_PROP_UINT16("origin", S390TopologyCores, origin, 0),
    DEFINE_PROP_UINT64("mask", S390TopologyCores, mask, 0),
    DEFINE_PROP_UINT8("id", S390TopologyCores, id, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void cpu_cores_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(oc);

    device_class_set_props(dc, s390_topology_cores_properties);
    hc->unplug = qdev_simple_device_unplug_cb;
    dc->bus_type = TYPE_S390_TOPOLOGY_SOCKET_BUS;
    dc->desc = "topology cpu entry";
}

static const TypeInfo cpu_cores_info = {
    .name          = TYPE_S390_TOPOLOGY_CORES,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(S390TopologyCores),
    .class_init    = cpu_cores_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_HOTPLUG_HANDLER },
        { }
    }
};

/* --- SOCKETS Definitions --- */
static Property s390_topology_socket_properties[] = {
    DEFINE_PROP_UINT8("socket_id", S390TopologySocket, socket_id, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static char *socket_bus_get_dev_path(DeviceState *dev)
{
    S390TopologySocket *socket = S390_TOPOLOGY_SOCKET(dev);
    DeviceState *book = dev->parent_bus->parent;
    char *id = qdev_get_dev_path(book);
    char *ret;

    if (id) {
        ret = g_strdup_printf("%s:%02d", id, socket->socket_id);
        g_free(id);
    } else {
        ret = g_malloc(6);
        snprintf(ret, 6, "_:%02d", socket->socket_id);
    }

    return ret;
}

static void socket_bus_class_init(ObjectClass *oc, void *data)
{
    BusClass *k = BUS_CLASS(oc);

    k->get_dev_path = socket_bus_get_dev_path;
    k->max_dev = S390_MAX_SOCKETS;
}

static const TypeInfo socket_bus_info = {
    .name = TYPE_S390_TOPOLOGY_SOCKET_BUS,
    .parent = TYPE_BUS,
    .instance_size = 0,
    .class_init = socket_bus_class_init,
};

static void s390_socket_device_realize(DeviceState *dev, Error **errp)
{
    S390TopologySocket *socket = S390_TOPOLOGY_SOCKET(dev);
    BusState *bus;

    bus = qbus_create(TYPE_S390_TOPOLOGY_SOCKET_BUS, dev,
                      TYPE_S390_TOPOLOGY_SOCKET_BUS);
    qbus_set_hotplug_handler(bus, OBJECT(dev));
    socket->bus = bus;
}

static void socket_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(oc);

    hc->unplug = qdev_simple_device_unplug_cb;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->bus_type = TYPE_S390_TOPOLOGY_BOOK_BUS;
    dc->realize = s390_socket_device_realize;
    device_class_set_props(dc, s390_topology_socket_properties);
    dc->desc = "topology socket";
}

static const TypeInfo socket_info = {
    .name          = TYPE_S390_TOPOLOGY_SOCKET,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(S390TopologySocket),
    .class_init    = socket_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_HOTPLUG_HANDLER },
        { }
    }
};

/* --- BOOKS Definitions --- */
static Property s390_topology_book_properties[] = {
    DEFINE_PROP_UINT8("book_id", S390TopologyBook, book_id, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static char *book_bus_get_dev_path(DeviceState *dev)
{
    S390TopologyBook *book = S390_TOPOLOGY_BOOK(dev);
    DeviceState *drawer = dev->parent_bus->parent;
    char *id = qdev_get_dev_path(drawer);
    char *ret;

    if (id) {
        ret = g_strdup_printf("%s:%02d", id, book->book_id);
        g_free(id);
    } else {
        ret = g_malloc(6);
        snprintf(ret, 6, "_:%02d", book->book_id);
    }

    return ret;
}

static void book_bus_class_init(ObjectClass *oc, void *data)
{
    BusClass *k = BUS_CLASS(oc);

    k->get_dev_path = book_bus_get_dev_path;
    k->max_dev = S390_MAX_BOOKS;
}

static const TypeInfo book_bus_info = {
    .name = TYPE_S390_TOPOLOGY_BOOK_BUS,
    .parent = TYPE_BUS,
    .instance_size = 0,
    .class_init = book_bus_class_init,
};

static void s390_book_device_realize(DeviceState *dev, Error **errp)
{
    S390TopologyBook *book = S390_TOPOLOGY_BOOK(dev);
    BusState *bus;

    bus = qbus_create(TYPE_S390_TOPOLOGY_BOOK_BUS, dev,
                      TYPE_S390_TOPOLOGY_BOOK_BUS);
    qbus_set_hotplug_handler(bus, OBJECT(dev));
    book->bus = bus;
}

static void book_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(oc);

    hc->unplug = qdev_simple_device_unplug_cb;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->bus_type = TYPE_S390_TOPOLOGY_DRAWER_BUS;
    dc->realize = s390_book_device_realize;
    device_class_set_props(dc, s390_topology_book_properties);
    dc->desc = "topology book";
}

static const TypeInfo book_info = {
    .name          = TYPE_S390_TOPOLOGY_BOOK,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(S390TopologyBook),
    .class_init    = book_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_HOTPLUG_HANDLER },
        { }
    }
};

/* --- DRAWER Definitions --- */
static Property s390_topology_drawer_properties[] = {
    DEFINE_PROP_UINT8("drawer_id", S390TopologyDrawer, drawer_id, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static char *drawer_bus_get_dev_path(DeviceState *dev)
{
    S390TopologyDrawer *drawer = S390_TOPOLOGY_DRAWER(dev);
    DeviceState *node = dev->parent_bus->parent;
    char *id = qdev_get_dev_path(node);
    char *ret;

    if (id) {
        ret = g_strdup_printf("%s:%02d", id, drawer->drawer_id);
        g_free(id);
    } else {
        ret = g_malloc(6);
        snprintf(ret, 6, "_:%02d", drawer->drawer_id);
    }

    return ret;
}

static void drawer_bus_class_init(ObjectClass *oc, void *data)
{
    BusClass *k = BUS_CLASS(oc);

    k->get_dev_path = drawer_bus_get_dev_path;
    k->max_dev = S390_MAX_DRAWERS;
}

static const TypeInfo drawer_bus_info = {
    .name = TYPE_S390_TOPOLOGY_DRAWER_BUS,
    .parent = TYPE_BUS,
    .instance_size = 0,
    .class_init = drawer_bus_class_init,
};

static void s390_drawer_device_realize(DeviceState *dev, Error **errp)
{
    S390TopologyDrawer *drawer = S390_TOPOLOGY_DRAWER(dev);
    BusState *bus;

    bus = qbus_create(TYPE_S390_TOPOLOGY_DRAWER_BUS, dev,
                      TYPE_S390_TOPOLOGY_DRAWER_BUS);
    qbus_set_hotplug_handler(bus, OBJECT(dev));
    drawer->bus = bus;
}

static void drawer_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(oc);

    hc->unplug = qdev_simple_device_unplug_cb;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->bus_type = TYPE_S390_TOPOLOGY_NODE_BUS;
    dc->realize = s390_drawer_device_realize;
    device_class_set_props(dc, s390_topology_drawer_properties);
    dc->desc = "topology drawer";
}

static const TypeInfo drawer_info = {
    .name          = TYPE_S390_TOPOLOGY_DRAWER,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(S390TopologyDrawer),
    .class_init    = drawer_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_HOTPLUG_HANDLER },
        { }
    }
};

/* --- NODE Definitions --- */

/*
 * Nodes are the first level of CPU topology we support
 * only one NODE for the moment.
 */
static char *node_bus_get_dev_path(DeviceState *dev)
{
    return g_strdup_printf("00");
}

static void node_bus_class_init(ObjectClass *oc, void *data)
{
    BusClass *k = BUS_CLASS(oc);

    k->get_dev_path = node_bus_get_dev_path;
    k->max_dev = S390_MAX_NODES;
}

static const TypeInfo node_bus_info = {
    .name = TYPE_S390_TOPOLOGY_NODE_BUS,
    .parent = TYPE_BUS,
    .instance_size = 0,
    .class_init = node_bus_class_init,
};

static void s390_node_device_realize(DeviceState *dev, Error **errp)
{
    S390TopologyNode *node = S390_TOPOLOGY_NODE(dev);
    BusState *bus;

    /* Create NODE bus on NODE bridge device */
    bus = qbus_create(TYPE_S390_TOPOLOGY_NODE_BUS, dev,
                      TYPE_S390_TOPOLOGY_NODE_BUS);
    node->bus = bus;

    /* Enable hotplugging */
    qbus_set_hotplug_handler(bus, OBJECT(dev));
}

static void node_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(oc);

    hc->unplug = qdev_simple_device_unplug_cb;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->realize = s390_node_device_realize;
    dc->desc = "topology node";
}

static const TypeInfo node_info = {
    .name          = TYPE_S390_TOPOLOGY_NODE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(S390TopologyNode),
    .class_init    = node_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_HOTPLUG_HANDLER },
        { }
    }
};

static void topology_register(void)
{
    type_register_static(&cpu_cores_info);
    type_register_static(&socket_bus_info);
    type_register_static(&socket_info);
    type_register_static(&book_bus_info);
    type_register_static(&book_info);
    type_register_static(&drawer_bus_info);
    type_register_static(&drawer_info);
    type_register_static(&node_bus_info);
    type_register_static(&node_info);
}

type_init(topology_register);
