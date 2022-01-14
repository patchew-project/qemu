/*
 * vmnet-bridged.m
 *
 * Copyright(c) 2021 Vladislav Yaroshchuk <yaroshchuk2000@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/qapi-types-net.h"
#include "qapi/error.h"
#include "clients.h"
#include "vmnet_int.h"

#include <vmnet/vmnet.h>

typedef struct VmnetBridgedState {
  VmnetCommonState cs;
} VmnetBridgedState;

static bool validate_ifname(const char *ifname)
{
    xpc_object_t shared_if_list = vmnet_copy_shared_interface_list();
    __block bool match = false;

    xpc_array_apply(
        shared_if_list,
        ^bool(size_t index, xpc_object_t value) {
          if (strcmp(xpc_string_get_string_ptr(value), ifname) == 0) {
              match = true;
              return false;
          }
          return true;
        });

    return match;
}

static const char *get_valid_ifnames(void)
{
    xpc_object_t shared_if_list = vmnet_copy_shared_interface_list();
    __block char *if_list = NULL;

    xpc_array_apply(
        shared_if_list,
        ^bool(size_t index, xpc_object_t value) {
          if_list = g_strconcat(xpc_string_get_string_ptr(value),
                                " ",
                                if_list,
                                NULL);
          return true;
        });

    if (if_list) {
        return if_list;
    }
    return "[no interfaces]";
}

static xpc_object_t create_if_desc(const Netdev *netdev, Error **errp)
{
    const NetdevVmnetBridgedOptions *options = &(netdev->u.vmnet_bridged);
    xpc_object_t if_desc = xpc_dictionary_create(NULL, NULL, 0);

    xpc_dictionary_set_uint64(
        if_desc,
        vmnet_operation_mode_key,
        VMNET_BRIDGED_MODE
    );

#if defined(MAC_OS_VERSION_11_0) && \
    MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_VERSION_11_0
    xpc_dictionary_set_bool(
        if_desc,
        vmnet_enable_isolation_key,
        options->isolated
    );
#else
    if (options->has_isolated) {
        error_setg(errp,
                   "vmnet-bridged.isolated feature is "
                   "unavailable: outdated vmnet.framework API");
    }
#endif

    if (validate_ifname(options->ifname)) {
        xpc_dictionary_set_string(if_desc,
                                  vmnet_shared_interface_name_key,
                                  options->ifname);
    } else {
        return NULL;
    }
    return if_desc;
}

static NetClientInfo net_vmnet_bridged_info = {
    .type = NET_CLIENT_DRIVER_VMNET_BRIDGED,
    .size = sizeof(VmnetBridgedState),
    .receive = vmnet_receive_common,
    .cleanup = vmnet_cleanup_common,
};

int net_init_vmnet_bridged(const Netdev *netdev, const char *name,
                           NetClientState *peer, Error **errp)
{
    NetClientState *nc = qemu_new_net_client(&net_vmnet_bridged_info,
                                             peer, "vmnet-bridged", name);
    xpc_object_t if_desc = create_if_desc(netdev, errp);;

    if (!if_desc) {
        error_setg(errp,
                   "unsupported ifname, should be one of: %s",
                   get_valid_ifnames());
        return -1;
    }

    return vmnet_if_create(nc, if_desc, errp, NULL);
}
