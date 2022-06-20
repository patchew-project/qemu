/*
 * CPU Topology
 *
 * Copyright 2022 IBM Corp.
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

/*
 * s390_create_cores:
 * @ms: Machine state
 * @socket: the socket on which to create the core set
 * @origin: the origin offset of the first core of the set
 * @errp: Error pointer
 *
 * returns a pointer to the created S390TopologyCores structure
 *
 * On error: return NULL
 */
static S390TopologyCores *s390_create_cores(MachineState *ms,
                                            S390TopologySocket *socket,
                                            int origin, Error **errp)
{
    DeviceState *dev;
    S390TopologyCores *cores;

    if (socket->bus->num_children >= ms->smp.cores * ms->smp.threads) {
        error_setg(errp, "Unable to create more cores.");
        return NULL;
    }

    dev = qdev_new(TYPE_S390_TOPOLOGY_CORES);
    qdev_realize_and_unref(dev, socket->bus, &error_fatal);

    cores = S390_TOPOLOGY_CORES(dev);
    cores->origin = origin;
    socket->cnt += 1;

    return cores;
}

/*
 * s390_create_socket:
 * @ms: Machine state
 * @book: the book on which to create the socket
 * @id: the socket id
 * @errp: Error pointer
 *
 * returns a pointer to the created S390TopologySocket structure
 *
 * On error: return NULL
 */
static S390TopologySocket *s390_create_socket(MachineState *ms,
                                              S390TopologyBook *book,
                                              int id, Error **errp)
{
    DeviceState *dev;
    S390TopologySocket *socket;

    if (book->bus->num_children >= ms->smp.sockets) {
        error_setg(errp, "Unable to create more sockets.");
        return NULL;
    }

    dev = qdev_new(TYPE_S390_TOPOLOGY_SOCKET);
    qdev_realize_and_unref(dev, book->bus, &error_fatal);

    socket = S390_TOPOLOGY_SOCKET(dev);
    socket->socket_id = id;
    book->cnt++;

    return socket;
}

/*
 * s390_create_book:
 * @ms: Machine state
 * @drawer: the drawer on which to create the book
 * @id: the book id
 *
 * returns a pointer to the created S390TopologyBook structure
 *
 * On error: return NULL
 */
static S390TopologyBook *s390_create_book(MachineState *ms,
                                          S390TopologyDrawer *drawer,
                                          int id, Error **errp)
{
    DeviceState *dev;
    S390TopologyBook *book;

    if (drawer->bus->num_children >= ms->smp.books) {
        error_setg(errp, "Unable to create more books.");
        return NULL;
    }

    dev = qdev_new(TYPE_S390_TOPOLOGY_BOOK);
    qdev_realize_and_unref(dev, drawer->bus, &error_fatal);

    book = S390_TOPOLOGY_BOOK(dev);
    book->book_id = id;
    drawer->cnt++;

    return book;
}

/*
 * s390_get_cores:
 * @ms: Machine state
 * @socket: the socket to search into
 * @origin: the origin specified for the S390TopologyCores
 * @errp: Error pointer
 *
 * returns a pointer to a S390TopologyCores structure within a socket having
 * the specified origin.
 * First search if the socket is already containing the S390TopologyCores
 * structure and if not create one with this origin.
 */
static S390TopologyCores *s390_get_cores(MachineState *ms,
                                         S390TopologySocket *socket,
                                         int origin, Error **errp)
{
    S390TopologyCores *cores;
    BusChild *kid;

    QTAILQ_FOREACH(kid, &socket->bus->children, sibling) {
        cores = S390_TOPOLOGY_CORES(kid->child);
        if (cores->origin == origin) {
            return cores;
        }
    }
    return s390_create_cores(ms, socket, origin, errp);
}

/*
 * s390_get_socket:
 * @ms: Machine state
 * @book: The book to search into
 * @socket_id: the identifier of the socket to search for
 * @errp: Error pointer
 *
 * returns a pointer to a S390TopologySocket structure within a book having
 * the specified socket_id.
 * First search if the book is already containing the S390TopologySocket
 * structure and if not create one with this socket_id.
 */
static S390TopologySocket *s390_get_socket(MachineState *ms,
                                           S390TopologyBook *book,
                                           int socket_id, Error **errp)
{
    S390TopologySocket *socket;
    BusChild *kid;

    QTAILQ_FOREACH(kid, &book->bus->children, sibling) {
        socket = S390_TOPOLOGY_SOCKET(kid->child);
        if (socket->socket_id == socket_id) {
            return socket;
        }
    }
    return s390_create_socket(ms, book, socket_id, errp);
}

/*
 * s390_get_book:
 * @ms: Machine state
 * @drawer: The drawer to search into
 * @book_id: the identifier of the book to search for
 * @errp: Error pointer
 *
 * returns a pointer to a S390TopologySocket structure within a drawer having
 * the specified book_id.
 * First search if the drawer is already containing the S390TopologySocket
 * structure and if not create one with this book_id.
 */
static S390TopologyBook *s390_get_book(MachineState *ms,
                                       S390TopologyDrawer *drawer,
                                       int book_id, Error **errp)
{
    S390TopologyBook *book;
    BusChild *kid;

    QTAILQ_FOREACH(kid, &drawer->bus->children, sibling) {
        book = S390_TOPOLOGY_BOOK(kid->child);
        if (book->book_id == book_id) {
            return book;
        }
    }
    return s390_create_book(ms, drawer, book_id, errp);
}

/*
 * s390_topology_new_cpu:
 * @core_id: the core ID is machine wide
 *
 * We have a single book returned by s390_get_topology(),
 * then we build the hierarchy on demand.
 * Note that we do not destroy the hierarchy on error creating
 * an entry in the topology, we just keep it empty.
 * We do not need to worry about not finding a topology level
 * entry this would have been caught during smp parsing.
 */
bool s390_topology_new_cpu(MachineState *ms, int core_id, Error **errp)
{
    S390TopologyDrawer *drawer;
    S390TopologyBook *book;
    S390TopologySocket *socket;
    S390TopologyCores *cores;
    int nb_cores_per_socket;
    int nb_cores_per_book;
    int origin, bit;

    drawer = s390_get_topology();

    nb_cores_per_socket = ms->smp.cores * ms->smp.threads;
    nb_cores_per_book = ms->smp.sockets * nb_cores_per_socket;

    book = s390_get_book(ms, drawer, core_id / nb_cores_per_book, errp);
    if (!book) {
        return false;
    }
    socket = s390_get_socket(ms, book, core_id / nb_cores_per_socket, errp);
    if (!socket) {
        return false;
    }

    /*
     * At the core level, each CPU is represented by a bit in a 64bit
     * unsigned long. Set on plug and clear on unplug of a CPU.
     * The firmware assume that all CPU in the core description have the same
     * type, polarization and are all dedicated or shared.
     * In the case a socket contains CPU with different type, polarization
     * or dedication then they will be defined in different CPU containers.
     * Currently we assume all CPU are identical and the only reason to have
     * several S390TopologyCores inside a socket is to have more than 64 CPUs
     * in that case the origin field, representing the offset of the first CPU
     * in the CPU container allows to represent up to the maximal number of
     * CPU inside several CPU containers inside the socket container.
     */
    origin = 64 * (core_id / 64);

    cores = s390_get_cores(ms, socket, origin, errp);
    if (!cores) {
        return false;
    }

    bit = 63 - (core_id - origin);
    set_bit(bit, &cores->mask);
    cores->origin = origin;

    return true;
}

/*
 * Setting the first topology: 1 book, 1 socket
 * This is enough for 64 cores if the topology is flat (single socket)
 */
void s390_topology_setup(MachineState *ms)
{
    DeviceState *dev;

    /* Create BOOK bridge device */
    dev = qdev_new(TYPE_S390_TOPOLOGY_DRAWER);
    object_property_add_child(qdev_get_machine(),
                              TYPE_S390_TOPOLOGY_DRAWER, OBJECT(dev));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
}

S390TopologyDrawer *s390_get_topology(void)
{
    static S390TopologyDrawer *drawer;

    if (!drawer) {
        drawer = S390_TOPOLOGY_DRAWER(object_resolve_path(
                                      TYPE_S390_TOPOLOGY_DRAWER, NULL));
        assert(drawer != NULL);
    }

    return drawer;
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
        ret = g_strdup_printf("_:%02d", socket->socket_id);
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

    bus = qbus_new(TYPE_S390_TOPOLOGY_SOCKET_BUS, dev,
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
    return g_strdup("00");
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

    bus = qbus_new(TYPE_S390_TOPOLOGY_BOOK_BUS, dev,
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
    dc->bus_type = TYPE_S390_TOPOLOGY_DRAWER_BUS;
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
        ret = g_strdup_printf("_:%02d", drawer->drawer_id);
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

    bus = qbus_new(TYPE_S390_TOPOLOGY_DRAWER_BUS, dev,
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
    dc->realize = s390_drawer_device_realize;
    device_class_set_props(dc, s390_topology_drawer_properties);
    dc->desc = "topology drawer";
}

static const TypeInfo drawer_info = {
    .name          = TYPE_S390_TOPOLOGY_DRAWER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(S390TopologyDrawer),
    .class_init    = drawer_class_init,
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
}

type_init(topology_register);
