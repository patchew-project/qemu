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
#include "target/s390x/cpu.h"
#include "hw/s390x/s390-virtio-ccw.h"

static S390TopologyCores *s390_create_cores(S390TopologySocket *socket,
                                            int origin)
{
    DeviceState *dev;
    S390TopologyCores *cores;
    const MachineState *ms = MACHINE(qdev_get_machine());

    if (socket->bus->num_children >= ms->smp.cores) {
        return NULL;
    }

    dev = qdev_new(TYPE_S390_TOPOLOGY_CORES);
    qdev_realize_and_unref(dev, socket->bus, &error_fatal);

    cores = S390_TOPOLOGY_CORES(dev);
    cores->origin = origin;
    socket->cnt += 1;

    return cores;
}

static S390TopologySocket *s390_create_socket(S390TopologyBook *book, int id)
{
    DeviceState *dev;
    S390TopologySocket *socket;
    const MachineState *ms = MACHINE(qdev_get_machine());

    if (book->bus->num_children >= ms->smp.sockets) {
        return NULL;
    }

    dev = qdev_new(TYPE_S390_TOPOLOGY_SOCKET);
    qdev_realize_and_unref(dev, book->bus, &error_fatal);

    socket = S390_TOPOLOGY_SOCKET(dev);
    socket->socket_id = id;
    book->cnt++;

    return socket;
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

/*
 * s390_topology_new_cpu:
 * @core_id: the core ID is machine wide
 *
 * We have a single book returned by s390_get_topology(),
 * then we build the hierarchy on demand.
 * Note that we do not destroy the hierarchy on error creating
 * an entry in the topology, we just keept it empty.
 * We do not need to worry about not finding a topology level
 * entry this would have been catched during smp parsing.
 */
void s390_topology_new_cpu(int core_id)
{
    const MachineState *ms = MACHINE(qdev_get_machine());
    S390TopologyBook *book;
    S390TopologySocket *socket;
    S390TopologyCores *cores;
    int cores_per_socket, sock_idx;
    int origin, bit;

    book = s390_get_topology();

    cores_per_socket = ms->smp.max_cpus / ms->smp.sockets;

    sock_idx = (core_id / cores_per_socket);
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
}

/*
 * Setting the first topology: 1 book, 1 socket
 * This is enough for 64 cores if the topology is flat (single socket)
 */
void s390_topology_setup(MachineState *ms)
{
    DeviceState *dev;

    /* Create BOOK bridge device */
    dev = qdev_new(TYPE_S390_TOPOLOGY_BOOK);
    object_property_add_child(qdev_get_machine(),
                              TYPE_S390_TOPOLOGY_BOOK, OBJECT(dev));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
}

S390TopologyBook *s390_get_topology(void)
{
    static S390TopologyBook *book;

    if (!book) {
        book = S390_TOPOLOGY_BOOK(
            object_resolve_path(TYPE_S390_TOPOLOGY_BOOK, NULL));
        assert(book != NULL);
    }

    return book;
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

static char *book_bus_get_dev_path(DeviceState *dev)
{
    return g_strdup_printf("00");
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
    dc->realize = s390_book_device_realize;
    dc->desc = "topology book";
}

static const TypeInfo book_info = {
    .name          = TYPE_S390_TOPOLOGY_BOOK,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(S390TopologyBook),
    .class_init    = book_class_init,
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
}

type_init(topology_register);
