/*
 * vmnet-shared.c
 *
 * Copyright(c) 2021 Vladislav Yaroshchuk <yaroshchuk2000@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi-types-net.h"
#include "vmnet_int.h"
#include "clients.h"
#include "qemu/error-report.h"
#include "qapi/error.h"

#include <vmnet/vmnet.h>

typedef struct VmnetSharedState {
  VmnetCommonState common;

} VmnetSharedState;

static xpc_object_t create_if_desc(const Netdev *netdev, Error **errp);

static NetClientInfo net_vmnet_shared_info = {
    .type = NET_CLIENT_DRIVER_VMNET_SHARED,
    .size = sizeof(VmnetSharedState),
    .receive = vmnet_receive_common,
    .cleanup = vmnet_cleanup_common,
};

int net_init_vmnet_shared(const Netdev *netdev, const char *name,
                          NetClientState *peer, Error **errp)
{
    NetClientState *nc;
    xpc_object_t if_desc;

    nc = qemu_new_net_client(&net_vmnet_shared_info,
                             peer, "vmnet-shared", name);
    if_desc = create_if_desc(netdev, errp);
    return vmnet_if_create(nc, if_desc, errp, NULL);
}

static xpc_object_t create_if_desc(const Netdev *netdev, Error **errp)
{
    const NetdevVmnetSharedOptions *options;
    xpc_object_t if_desc;

    if_desc = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_uint64(
        if_desc,
        vmnet_operation_mode_key,
        VMNET_SHARED_MODE
    );

    xpc_dictionary_set_bool(
        if_desc,
        vmnet_allocate_mac_address_key,
        false
    );

    options = &(netdev->u.vmnet_shared);

    if (options->has_dhcpstart ||
        options->has_dhcpend ||
        options->has_subnetmask) {

        if (options->has_dhcpstart &&
            options->has_dhcpend &&
            options->has_subnetmask) {

            xpc_dictionary_set_string(if_desc,
                                      vmnet_start_address_key,
                                      options->dhcpstart);
            xpc_dictionary_set_string(if_desc,
                                      vmnet_end_address_key,
                                      options->dhcpend);
            xpc_dictionary_set_string(if_desc,
                                      vmnet_subnet_mask_key,
                                      options->subnetmask);
        } else {
            error_setg(
                errp,
                "'dhcpstart', 'dhcpend', 'subnetmask' "
                "must be provided together"
            );
        }
    }

    return if_desc;
}

