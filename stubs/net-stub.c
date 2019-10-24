#include "qemu/osdep.h"
#include "qemu-common.h"
#include "net/net.h"

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

