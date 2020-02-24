#include "qemu/osdep.h"
#include "qemu-common.h"
#include "net/net.h"

#include "qapi/qapi-commands-net.h"
#include "qapi/qapi-commands-rocker.h"

#pragma weak qmp_announce_self

int qemu_find_net_clients_except(const char *id, NetClientState **ncs,
                                 NetClientDriver type, int max)
{
    return -ENOSYS;
}

NetClientState *net_hub_port_find(int hub_id)
{
    return NULL;
}

int net_hub_id_for_client(NetClientState *nc, int *id)
{
    return -ENOSYS;
}

int qemu_show_nic_models(const char *arg, const char *const *models)
{
    return -ENOSYS;
}

int qemu_find_nic_model(NICInfo *nd, const char * const *models,
                        const char *default_model)
{
    return -ENOSYS;
}

void qmp_set_link(const char *name, bool up, Error **errp)
{
    qemu_debug_assert(0);
}

void qmp_netdev_del(const char *id, Error **errp)
{
    qemu_debug_assert(0);
}

RxFilterInfoList *qmp_query_rx_filter(bool has_name, const char *name,
                                      Error **errp)
{
    qemu_debug_assert(0);

    return NULL;
}

void qmp_announce_self(AnnounceParameters *params, Error **errp)
{
    qemu_debug_assert(0);
}

RockerSwitch *qmp_query_rocker(const char *name, Error **errp)
{
    qemu_debug_assert(0);

    return NULL;
}

RockerPortList *qmp_query_rocker_ports(const char *name, Error **errp)
{
    qemu_debug_assert(0);

    return NULL;
}

RockerOfDpaFlowList *qmp_query_rocker_of_dpa_flows(const char *name,
                                                   bool has_tbl_id,
                                                   uint32_t tbl_id,
                                                   Error **errp)
{
    qemu_debug_assert(0);

    return NULL;
}

RockerOfDpaGroupList *qmp_query_rocker_of_dpa_groups(const char *name,
                                                     bool has_type,
                                                     uint8_t type,
                                                     Error **errp)
{
    qemu_debug_assert(0);

    return NULL;
}

void qmp_netdev_add(QDict *qdict, QObject **ret, Error **errp)
{
    qemu_debug_assert(0);
}

void netdev_add(QemuOpts *opts, Error **errp)
{
    qemu_debug_assert(0);
}

NetClientState *qemu_get_queue(NICState *nic)
{
    qemu_debug_assert(0);

    return NULL;
}

ssize_t qemu_send_packet_raw(NetClientState *nc, const uint8_t *buf, int size)
{
    qemu_debug_assert(0);

    return 0;
}

void qemu_foreach_nic(qemu_nic_foreach func, void *opaque)
{
    qemu_debug_assert(0);
}
