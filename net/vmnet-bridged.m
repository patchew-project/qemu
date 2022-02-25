/*
 * vmnet-bridged.m
 *
 * Copyright(c) 2021 Vladislav Yaroshchuk <vladislav.yaroshchuk@jetbrains.com>
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
    bool match = false;
    if (!xpc_array_get_count(shared_if_list)) {
        goto done;
    }

    match = !xpc_array_apply(
        shared_if_list,
        ^bool(size_t index, xpc_object_t value) {
            return strcmp(xpc_string_get_string_ptr(value), ifname) != 0;
        });

done:
    xpc_release(shared_if_list);
    return match;
}


static bool get_valid_ifnames(char *output_buf)
{
    xpc_object_t shared_if_list = vmnet_copy_shared_interface_list();
    __block const char *ifname = NULL;
    __block int str_offset = 0;
    bool interfaces_available = true;

    if (!xpc_array_get_count(shared_if_list)) {
        interfaces_available = false;
        goto done;
    }

    xpc_array_apply(
        shared_if_list,
        ^bool(size_t index, xpc_object_t value) {
            /* build list of strings like "en0 en1 en2 " */
            ifname = xpc_string_get_string_ptr(value);
            strcpy(output_buf + str_offset, ifname);
            strcpy(output_buf + str_offset + strlen(ifname), " ");
            str_offset += strlen(ifname) + 1;
            return true;
        });

done:
    xpc_release(shared_if_list);
    return interfaces_available;
}

static bool validate_options(const Netdev *netdev, Error **errp)
{
    const NetdevVmnetBridgedOptions *options = &(netdev->u.vmnet_bridged);
    char ifnames[256];

    if (!validate_ifname(options->ifname)) {
        if (get_valid_ifnames(ifnames)) {
            error_setg(errp,
                       "unsupported ifname '%s', expected one of [ %s]",
                       options->ifname,
                       ifnames);
            return false;
        }
        error_setg(errp,
                   "unsupported ifname '%s', no supported "
                   "interfaces available",
                   options->ifname);
        return false;
    }

#if !defined(MAC_OS_VERSION_11_0) || \
    MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_VERSION_11_0
    if (options->has_isolated) {
        error_setg(errp,
                   "vmnet-bridged.isolated feature is "
                   "unavailable: outdated vmnet.framework API");
        return false;
    }
#endif
    return true;
}

static xpc_object_t build_if_desc(const Netdev *netdev)
{
    const NetdevVmnetBridgedOptions *options = &(netdev->u.vmnet_bridged);
    xpc_object_t if_desc = xpc_dictionary_create(NULL, NULL, 0);

    xpc_dictionary_set_uint64(if_desc,
                              vmnet_operation_mode_key,
                              VMNET_BRIDGED_MODE
    );

    xpc_dictionary_set_string(if_desc,
                              vmnet_shared_interface_name_key,
                              options->ifname);

#if defined(MAC_OS_VERSION_11_0) && \
    MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_VERSION_11_0
    xpc_dictionary_set_bool(if_desc,
                            vmnet_enable_isolation_key,
                            options->isolated);
#endif
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
    if (!validate_options(netdev, errp)) {
        g_assert_not_reached();
    }
    return vmnet_if_create(nc, build_if_desc(netdev), errp);
}
