/*
 * vmnet-host.c
 *
 * Copyright(c) 2021 Vladislav Yaroshchuk <vladislav.yaroshchuk@jetbrains.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/uuid.h"
#include "qapi/qapi-types-net.h"
#include "qapi/error.h"
#include "clients.h"
#include "vmnet_int.h"

#include <vmnet/vmnet.h>

typedef struct VmnetHostState {
    VmnetCommonState cs;
    QemuUUID network_uuid;
} VmnetHostState;

static bool validate_options(const Netdev *netdev, Error **errp)
{
    const NetdevVmnetHostOptions *options = &(netdev->u.vmnet_host);
    QemuUUID uuid;

#if defined(MAC_OS_VERSION_11_0) && \
    MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_VERSION_11_0

    if (options->has_net_uuid &&
        qemu_uuid_parse(options->net_uuid, &uuid) < 0) {
        error_setg(errp, "Invalid UUID provided in 'net-uuid'");
        return false;
    }
#else
    if (options->has_isolated) {
        error_setg(errp,
                   "vmnet-host.isolated feature is "
                   "unavailable: outdated vmnet.framework API");
        return false;
    }

    if (options->has_net_uuid) {
        error_setg(errp,
                   "vmnet-host.net-uuid feature is "
                   "unavailable: outdated vmnet.framework API");
        return false;
    }
#endif

    if ((options->has_start_address ||
         options->has_end_address ||
         options->has_subnet_mask) &&
        !(options->has_start_address &&
          options->has_end_address &&
          options->has_subnet_mask)) {
        error_setg(errp,
                   "'start-address', 'end-address', 'subnet-mask' "
                   "should be provided together");
        return false;
    }

    return true;
}

static xpc_object_t build_if_desc(const Netdev *netdev,
                                  NetClientState *nc)
{
    const NetdevVmnetHostOptions *options = &(netdev->u.vmnet_host);
    VmnetCommonState *cs = DO_UPCAST(VmnetCommonState, nc, nc);
    VmnetHostState *hs = DO_UPCAST(VmnetHostState, cs, cs);
    xpc_object_t if_desc = xpc_dictionary_create(NULL, NULL, 0);

    xpc_dictionary_set_uint64(if_desc,
                              vmnet_operation_mode_key,
                              VMNET_HOST_MODE);

#if defined(MAC_OS_VERSION_11_0) && \
    MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_VERSION_11_0

    xpc_dictionary_set_bool(if_desc,
                            vmnet_enable_isolation_key,
                            options->isolated);

    if (options->has_net_uuid) {
        qemu_uuid_parse(options->net_uuid, &hs->network_uuid);
        xpc_dictionary_set_uuid(if_desc,
                                vmnet_network_identifier_key,
                                hs->network_uuid.data);
    }
#endif

    if (options->has_start_address) {
        xpc_dictionary_set_string(if_desc,
                                  vmnet_start_address_key,
                                  options->start_address);
        xpc_dictionary_set_string(if_desc,
                                  vmnet_end_address_key,
                                  options->end_address);
        xpc_dictionary_set_string(if_desc,
                                  vmnet_subnet_mask_key,
                                  options->subnet_mask);
    }

    return if_desc;
}

static NetClientInfo net_vmnet_host_info = {
    .type = NET_CLIENT_DRIVER_VMNET_HOST,
    .size = sizeof(VmnetHostState),
    .receive = vmnet_receive_common,
    .cleanup = vmnet_cleanup_common,
};

int net_init_vmnet_host(const Netdev *netdev, const char *name,
                        NetClientState *peer, Error **errp)
{
    NetClientState *nc = qemu_new_net_client(&net_vmnet_host_info,
                                             peer, "vmnet-host", name);
    if (!validate_options(netdev, errp)) {
        g_assert_not_reached();
    }
    return vmnet_if_create(nc, build_if_desc(netdev, nc), errp);
}
