/*
 * vmnet.framework backed netdev for macOS 10.15+ hosts
 *
 * Copyright (c) 2021 Phillip Tennen <phillip@axleos.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qemu/error-report.h"
#include "qapi/qapi-types-net.h"
#include "net/net.h"
/* macOS vmnet framework header */
#include <vmnet/vmnet.h>

typedef struct vmnet_state {
    NetClientState nc;
    interface_ref vmnet_iface_ref;
    /* Switched on after vmnet informs us that the interface has started */
    bool link_up;
    /*
     * If qemu_send_packet_async returns 0, this is switched off until our
     * delivery callback is invoked
     */
    bool qemu_ready_to_receive;
} vmnet_state_t;

int net_init_vmnet_macos(const Netdev *netdev, const char *name,
                         NetClientState *peer, Error **errp);

static const char *_vmnet_status_repr(vmnet_return_t status)
{
    switch (status) {
    case VMNET_SUCCESS:
        return "success";
    case VMNET_FAILURE:
        return "generic failure";
    case VMNET_MEM_FAILURE:
        return "out of memory";
    case VMNET_INVALID_ARGUMENT:
        return "invalid argument";
    case VMNET_SETUP_INCOMPLETE:
        return "setup is incomplete";
    case VMNET_INVALID_ACCESS:
        return "insufficient permissions";
    case VMNET_PACKET_TOO_BIG:
        return "packet size exceeds MTU";
    case VMNET_BUFFER_EXHAUSTED:
        return "kernel buffers temporarily exhausted";
    case VMNET_TOO_MANY_PACKETS:
        return "number of packets exceeds system limit";
    /* This error code was introduced in macOS 11.0 */
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 110000
    case VMNET_SHARING_SERVICE_BUSY:
        return "sharing service busy";
#endif
    default:
        return "unknown status code";
    }
}

static operating_modes_t _vmnet_operating_mode_enum_compat(
    VmnetOperatingMode mode)
{
    switch (mode) {
    case VMNET_OPERATING_MODE_HOST:
        return VMNET_HOST_MODE;
    case VMNET_OPERATING_MODE_SHARED:
        return VMNET_SHARED_MODE;
    case VMNET_OPERATING_MODE_BRIDGED:
        return VMNET_BRIDGED_MODE;
    default:
        /* Should never happen as the modes are parsed before we get here */
        assert(false);
    }
}

static bool vmnet_can_receive(NetClientState *nc)
{
    vmnet_state_t *s = DO_UPCAST(vmnet_state_t, nc, nc);
    return s->link_up;
}

static ssize_t vmnet_receive_iov(NetClientState *nc,
                                 const struct iovec *iovs,
                                 int iovcnt)
{
    vmnet_state_t *s = DO_UPCAST(vmnet_state_t, nc, nc);

    /* Combine the provided iovs into a single vmnet packet */
    struct vmpktdesc *packet = g_new0(struct vmpktdesc, 1);
    packet->vm_pkt_iov = g_new0(struct iovec, iovcnt);
    memcpy(packet->vm_pkt_iov, iovs, sizeof(struct iovec) * iovcnt);
    packet->vm_pkt_iovcnt = iovcnt;
    packet->vm_flags = 0;

    /* Figure out the packet size by iterating the iov's */
    for (int i = 0; i < iovcnt; i++) {
        const struct iovec *iov = iovs + i;
        packet->vm_pkt_size += iov->iov_len;
    }

    /* Finally, write the packet to the vmnet interface */
    int packet_count = 1;
    vmnet_return_t result = vmnet_write(s->vmnet_iface_ref, packet,
                                        &packet_count);
    if (result != VMNET_SUCCESS || packet_count != 1) {
        error_printf("Failed to send packet to host: %s\n",
            _vmnet_status_repr(result));
    }
    ssize_t wrote_bytes = packet->vm_pkt_size;
    g_free(packet->vm_pkt_iov);
    g_free(packet);
    return wrote_bytes;
}

static void vmnet_send_completed(NetClientState *nc, ssize_t len)
{
    vmnet_state_t *vmnet_client_state = DO_UPCAST(vmnet_state_t, nc, nc);
    /* Ready to receive more packets! */
    vmnet_client_state->qemu_ready_to_receive = true;
}

static NetClientInfo net_vmnet_macos_info = {
    .type = NET_CLIENT_DRIVER_VMNET_MACOS,
    .size = sizeof(vmnet_state_t),
    .receive_iov = vmnet_receive_iov,
    .can_receive = vmnet_can_receive,
};

static bool _validate_ifname_is_valid_bridge_target(const char *ifname)
{
    /* Iterate available bridge interfaces, ensure the provided one is valid */
    xpc_object_t bridge_interfaces = vmnet_copy_shared_interface_list();
    bool failed_to_match_iface_name = xpc_array_apply(
        bridge_interfaces,
        ^bool(size_t index, xpc_object_t  _Nonnull value) {
        if (!strcmp(xpc_string_get_string_ptr(value), ifname)) {
            /* The interface name is valid! Stop iterating */
            return false;
        }
        return true;
    });

    if (failed_to_match_iface_name) {
        error_printf("Invalid bridge interface name provided: %s\n", ifname);
        error_printf("Valid bridge interfaces:\n");
        xpc_array_apply(
            vmnet_copy_shared_interface_list(),
            ^bool(size_t index, xpc_object_t  _Nonnull value) {
            error_printf("\t%s\n", xpc_string_get_string_ptr(value));
            /* Keep iterating */
            return true;
        });
        exit(1);
        return false;
    }

    return true;
}

static xpc_object_t _construct_vmnet_interface_description(
    const NetdevVmnetModeOptions *vmnet_opts)
{
    operating_modes_t mode = _vmnet_operating_mode_enum_compat(
        vmnet_opts->mode);

    /* Validate options */
    if (mode == VMNET_HOST_MODE || mode == VMNET_SHARED_MODE) {
        NetdevVmnetModeOptionsHostOrShared mode_opts = vmnet_opts->u.host;
        /* If one DHCP parameter is configured, all 3 are required */
        if (mode_opts.has_dhcp_start_address ||
            mode_opts.has_dhcp_end_address ||
            mode_opts.has_dhcp_subnet_mask) {
            if (!(mode_opts.has_dhcp_start_address &&
                  mode_opts.has_dhcp_end_address &&
                  mode_opts.has_dhcp_subnet_mask)) {
                error_printf("Incomplete DHCP configuration provided\n");
                exit(1);
            }
        }
    } else if (mode == VMNET_BRIDGED_MODE) {
        /* Nothing to validate */
    } else {
        error_printf("Unknown vmnet mode %d\n", mode);
        exit(1);
    }

    xpc_object_t interface_desc = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_uint64(
        interface_desc,
        vmnet_operation_mode_key,
        mode
    );

    if (mode == VMNET_BRIDGED_MODE) {
        /*
         * Configure the provided physical interface to act
         * as a bridge with QEMU
         */
        NetdevVmnetModeOptionsBridged mode_opts = vmnet_opts->u.bridged;
        /* Bridge with en0 by default */
        const char *physical_ifname = mode_opts.has_ifname ? mode_opts.ifname :
                                                             "en0";
        _validate_ifname_is_valid_bridge_target(physical_ifname);
        xpc_dictionary_set_string(interface_desc,
                                  vmnet_shared_interface_name_key,
                                  physical_ifname);
    } else if (mode == VMNET_HOST_MODE || mode == VMNET_SHARED_MODE) {
        /* Pass the DHCP configuration to vmnet, if the user provided one */
        NetdevVmnetModeOptionsHostOrShared mode_opts = vmnet_opts->u.host;
        if (mode_opts.has_dhcp_start_address) {
            /* All DHCP arguments are available, as per the checks above */
            xpc_dictionary_set_string(interface_desc,
                                      vmnet_start_address_key,
                                      mode_opts.dhcp_start_address);
            xpc_dictionary_set_string(interface_desc,
                                      vmnet_end_address_key,
                                      mode_opts.dhcp_end_address);
            xpc_dictionary_set_string(interface_desc,
                                      vmnet_subnet_mask_key,
                                      mode_opts.dhcp_subnet_mask);
        }
    }

    return interface_desc;
}

int net_init_vmnet_macos(const Netdev *netdev, const char *name,
                        NetClientState *peer, Error **errp)
{
    assert(netdev->type == NET_CLIENT_DRIVER_VMNET_MACOS);

    NetdevVmnetModeOptions *vmnet_opts = netdev->u.vmnet_macos.options;
    xpc_object_t iface_desc = _construct_vmnet_interface_description(vmnet_opts);

    NetClientState *nc = qemu_new_net_client(&net_vmnet_macos_info, peer,
                                             "vmnet", name);
    vmnet_state_t *vmnet_client_state = DO_UPCAST(vmnet_state_t, nc, nc);

    dispatch_queue_t vmnet_dispatch_queue = dispatch_queue_create(
        "org.qemu.vmnet.iface_queue",
        DISPATCH_QUEUE_SERIAL
    );

    __block vmnet_return_t vmnet_start_status = 0;
    __block uint64_t vmnet_iface_mtu = 0;
    __block uint64_t vmnet_max_packet_size = 0;
    __block const char *vmnet_mac_address = NULL;
    /*
     * We can't refer to an array type directly within a block,
     * so hold a pointer instead.
     */
    uuid_string_t vmnet_iface_uuid = {0};
    __block uuid_string_t *vmnet_iface_uuid_ptr = &vmnet_iface_uuid;
    /* These are only provided in VMNET_HOST_MODE and VMNET_SHARED_MODE */
    bool vmnet_provides_dhcp_info = (
        vmnet_opts->mode == VMNET_OPERATING_MODE_HOST ||
        vmnet_opts->mode == VMNET_OPERATING_MODE_SHARED);
    __block const char *vmnet_subnet_mask = NULL;
    __block const char *vmnet_dhcp_range_start = NULL;
    __block const char *vmnet_dhcp_range_end = NULL;

    /* Create the vmnet interface */
    dispatch_semaphore_t vmnet_iface_sem = dispatch_semaphore_create(0);
    interface_ref vmnet_iface_ref = vmnet_start_interface(
        iface_desc,
        vmnet_dispatch_queue,
        ^(vmnet_return_t status, xpc_object_t  _Nullable interface_param) {
        vmnet_start_status = status;
        if (vmnet_start_status != VMNET_SUCCESS || !interface_param) {
            /* Early return if the interface couldn't be started */
            dispatch_semaphore_signal(vmnet_iface_sem);
            return;
        }

        /*
         * Read the configuration that vmnet provided us.
         * The provided dictionary is owned by XPC and may be freed
         * shortly after this block's execution.
         * So, copy data buffers now.
         */
        vmnet_iface_mtu = xpc_dictionary_get_uint64(
            interface_param,
            vmnet_mtu_key
        );
        vmnet_max_packet_size = xpc_dictionary_get_uint64(
            interface_param,
            vmnet_max_packet_size_key
        );
        vmnet_mac_address = strdup(xpc_dictionary_get_string(
            interface_param,
            vmnet_mac_address_key
        ));

        const uint8_t *iface_uuid = xpc_dictionary_get_uuid(
            interface_param,
            vmnet_interface_id_key
        );
        uuid_unparse_upper(iface_uuid, *vmnet_iface_uuid_ptr);

        /* If we're in a mode that provides DHCP info, read it out now */
        if (vmnet_provides_dhcp_info) {
            vmnet_dhcp_range_start = strdup(xpc_dictionary_get_string(
                interface_param,
                vmnet_start_address_key
            ));
            vmnet_dhcp_range_end = strdup(xpc_dictionary_get_string(
                interface_param,
                vmnet_end_address_key
            ));
            vmnet_subnet_mask = strdup(xpc_dictionary_get_string(
                interface_param,
                vmnet_subnet_mask_key
            ));
        }
        dispatch_semaphore_signal(vmnet_iface_sem);
    });

    /* And block until we receive a response from vmnet */
    dispatch_semaphore_wait(vmnet_iface_sem, DISPATCH_TIME_FOREVER);

    /* Did we manage to start the interface? */
    if (vmnet_start_status != VMNET_SUCCESS || !vmnet_iface_ref) {
        error_printf("Failed to start interface: %s\n",
            _vmnet_status_repr(vmnet_start_status));
        if (vmnet_start_status == VMNET_FAILURE) {
            error_printf("Hint: vmnet requires running with root access\n");
        }
        return -1;
    }

    info_report("Started vmnet interface with configuration:");
    info_report("MTU:              %llu", vmnet_iface_mtu);
    info_report("Max packet size:  %llu", vmnet_max_packet_size);
    info_report("MAC:              %s", vmnet_mac_address);
    if (vmnet_provides_dhcp_info) {
        info_report("DHCP IPv4 start:  %s", vmnet_dhcp_range_start);
        info_report("DHCP IPv4 end:    %s", vmnet_dhcp_range_end);
        info_report("IPv4 subnet mask: %s", vmnet_subnet_mask);
    }
    info_report("UUID:             %s", vmnet_iface_uuid);

    /* The interface is up! Set a block to run when packets are received */
    vmnet_client_state->vmnet_iface_ref = vmnet_iface_ref;
    vmnet_return_t event_cb_stat = vmnet_interface_set_event_callback(
        vmnet_iface_ref,
        VMNET_INTERFACE_PACKETS_AVAILABLE,
        vmnet_dispatch_queue,
        ^(interface_event_t event_mask, xpc_object_t  _Nonnull event) {
        if (event_mask != VMNET_INTERFACE_PACKETS_AVAILABLE) {
            error_printf("Unknown vmnet interface event 0x%08x\n", event_mask);
            return;
        }

        /* If we're unable to handle more packets now, drop this packet */
        if (!vmnet_client_state->qemu_ready_to_receive) {
            return;
        }

        /*
         * TODO(Phillip Tennen <phillip@axleos.com>): There may be more than
         * one packet available.
         * As an optimization, we could read
         * vmnet_estimated_packets_available_key packets now.
         */
        char *packet_buf = g_malloc0(vmnet_max_packet_size);
        struct iovec *iov = g_new0(struct iovec, 1);
        iov->iov_base = packet_buf;
        iov->iov_len = vmnet_max_packet_size;

        int pktcnt = 1;
        struct vmpktdesc *v = g_new0(struct vmpktdesc, pktcnt);
        v->vm_pkt_size = vmnet_max_packet_size;
        v->vm_pkt_iov = iov;
        v->vm_pkt_iovcnt = 1;
        v->vm_flags = 0;

        vmnet_return_t result = vmnet_read(vmnet_iface_ref, v, &pktcnt);
        if (result != VMNET_SUCCESS) {
            error_printf("Failed to read packet from host: %s\n",
                _vmnet_status_repr(result));
        }

        /* Ensure we read exactly one packet */
        assert(pktcnt == 1);

        /* Dispatch this block to a global queue instead of the main queue,
         * which is only created when the program has a Cocoa event loop.
         * If QEMU is started with -nographic, no Cocoa event loop will be
         * created and thus the main queue will be unavailable.
         */
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH,
                                                 0),
                       ^{
            qemu_mutex_lock_iothread();

            /*
             * Deliver the packet to the guest
             * If the delivery succeeded synchronously, this returns the length
             * of the sent packet.
             */
            if (qemu_send_packet_async(nc, iov->iov_base,
                                       v->vm_pkt_size,
                                       vmnet_send_completed) == 0) {
                vmnet_client_state->qemu_ready_to_receive = false;
            }

            /*
             * It's safe to free the packet buffers.
             * Even if delivery needs to wait, qemu_net_queue_append copies
             * the packet buffer.
             */
            g_free(v);
            g_free(iov);
            g_free(packet_buf);

            qemu_mutex_unlock_iothread();
        });
    });

    /* Did we manage to set an event callback? */
    if (event_cb_stat != VMNET_SUCCESS) {
        error_printf("Failed to set up a callback to receive packets: %s\n",
            _vmnet_status_repr(vmnet_start_status));
        exit(1);
    }

    /* We're now ready to receive packets */
    vmnet_client_state->qemu_ready_to_receive = true;
    vmnet_client_state->link_up = true;

    /* Include DHCP info if we're in a relevant mode */
    if (vmnet_provides_dhcp_info) {
        snprintf(nc->info_str, sizeof(nc->info_str),
                 "dhcp_start=%s,dhcp_end=%s,mask=%s",
                 vmnet_dhcp_range_start, vmnet_dhcp_range_end,
                 vmnet_subnet_mask);
    } else {
        snprintf(nc->info_str, sizeof(nc->info_str),
                 "mac=%s", vmnet_mac_address);
    }

    return 0;
}
