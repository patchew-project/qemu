/*
 * vmnet-common.m - network client wrapper for Apple vmnet.framework
 *
 * Copyright(c) 2021 Vladislav Yaroshchuk <vladislav.yaroshchuk@jetbrains.com>
 * Copyright(c) 2021 Phillip Tennen <phillip@axleos.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qemu/log.h"
#include "qapi/qapi-types-net.h"
#include "vmnet_int.h"
#include "clients.h"
#include "qemu/error-report.h"
#include "qapi/error.h"

#include <vmnet/vmnet.h>
#include <dispatch/dispatch.h>


static inline void vmnet_set_send_bh_scheduled(VmnetCommonState *s,
                                               bool enable)
{
    qatomic_set(&s->send_scheduled, enable);
}


static inline bool vmnet_is_send_bh_scheduled(VmnetCommonState *s)
{
    return qatomic_load_acquire(&s->send_scheduled);
}


static inline void vmnet_set_send_enabled(VmnetCommonState *s,
                                          bool enable)
{
    if (enable) {
        vmnet_interface_set_event_callback(
            s->vmnet_if,
            VMNET_INTERFACE_PACKETS_AVAILABLE,
            s->if_queue,
            ^(interface_event_t event_id, xpc_object_t event) {
                assert(event_id == VMNET_INTERFACE_PACKETS_AVAILABLE);
                /*
                 * This function is being called from a non qemu thread, so
                 * we only schedule a BH, and do the rest of the io completion
                 * handling from vmnet_send_bh() which runs in a qemu context.
                 *
                 * Avoid scheduling multiple bottom halves
                 */
                if (!vmnet_is_send_bh_scheduled(s)) {
                    vmnet_set_send_bh_scheduled(s, true);
                    qemu_bh_schedule(s->send_bh);
                }
            });
    } else {
        vmnet_interface_set_event_callback(
            s->vmnet_if,
            VMNET_INTERFACE_PACKETS_AVAILABLE,
            NULL,
            NULL);
    }
}


static void vmnet_send_completed(NetClientState *nc, ssize_t len)
{
    VmnetCommonState *s = DO_UPCAST(VmnetCommonState, nc, nc);
    vmnet_set_send_enabled(s, true);
}


static void vmnet_send_bh(void *opaque)
{
    NetClientState *nc = (NetClientState *) opaque;
    VmnetCommonState *s = DO_UPCAST(VmnetCommonState, nc, nc);

    struct iovec *iov = s->iov_buf;
    struct vmpktdesc *packets = s->packets_buf;
    int pkt_cnt;
    int i;

    vmnet_return_t status;
    ssize_t size;

    /* read as many packets as present */
    pkt_cnt = VMNET_PACKETS_LIMIT;
    for (i = 0; i < pkt_cnt; ++i) {
        packets[i].vm_pkt_size = s->max_packet_size;
        packets[i].vm_pkt_iovcnt = 1;
        packets[i].vm_flags = 0;
    }

    status = vmnet_read(s->vmnet_if, packets, &pkt_cnt);
    if (status != VMNET_SUCCESS) {
        error_printf("vmnet: read failed: %s\n",
                     vmnet_status_map_str(status));
        goto done;
    }

    for (i = 0; i < pkt_cnt; ++i) {
        size = qemu_send_packet_async(nc,
                                      iov[i].iov_base,
                                      packets[i].vm_pkt_size,
                                      vmnet_send_completed);
        if (size == 0) {
            vmnet_set_send_enabled(s, false);
            goto done;
        } else if (size < 0) {
            break;
        }
    }

done:
    vmnet_set_send_bh_scheduled(s, false);
}


static void vmnet_bufs_init(VmnetCommonState *s)
{
    struct vmpktdesc *packets = s->packets_buf;
    struct iovec *iov = s->iov_buf;
    int i;

    for (i = 0; i < VMNET_PACKETS_LIMIT; ++i) {
        iov[i].iov_len = s->max_packet_size;
        iov[i].iov_base = g_malloc0(iov[i].iov_len);
        packets[i].vm_pkt_iov = iov + i;
    }
}


const char *vmnet_status_map_str(vmnet_return_t status)
{
    switch (status) {
    case VMNET_SUCCESS:
        return "success";
    case VMNET_FAILURE:
        return "general failure (possibly not enough privileges)";
    case VMNET_MEM_FAILURE:
        return "memory allocation failure";
    case VMNET_INVALID_ARGUMENT:
        return "invalid argument specified";
    case VMNET_SETUP_INCOMPLETE:
        return "interface setup is not complete";
    case VMNET_INVALID_ACCESS:
        return "invalid access, permission denied";
    case VMNET_PACKET_TOO_BIG:
        return "packet size is larger than MTU";
    case VMNET_BUFFER_EXHAUSTED:
        return "buffers exhausted in kernel";
    case VMNET_TOO_MANY_PACKETS:
        return "packet count exceeds limit";
#if defined(MAC_OS_VERSION_11_0) && \
    MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_VERSION_11_0
    case VMNET_SHARING_SERVICE_BUSY:
        return "conflict, sharing service is in use";
#endif
    default:
        return "unknown vmnet error";
    }
}


int vmnet_if_create(NetClientState *nc,
                    xpc_object_t if_desc,
                    Error **errp)
{
    VmnetCommonState *s = DO_UPCAST(VmnetCommonState, nc, nc);;
    dispatch_semaphore_t if_created_sem = dispatch_semaphore_create(0);
    __block vmnet_return_t if_status;

    s->if_queue = dispatch_queue_create(
        "org.qemu.vmnet.if_queue",
        DISPATCH_QUEUE_SERIAL
    );

    xpc_dictionary_set_bool(
        if_desc,
        vmnet_allocate_mac_address_key,
        false
    );
#ifdef DEBUG
    qemu_log("vmnet.start.interface_desc:\n");
    xpc_dictionary_apply(if_desc,
                         ^bool(const char *k, xpc_object_t v) {
                             char *desc = xpc_copy_description(v);
                             qemu_log("  %s=%s\n", k, desc);
                             free(desc);
                             return true;
                         });
#endif /* DEBUG */

    s->vmnet_if = vmnet_start_interface(
        if_desc,
        s->if_queue,
        ^(vmnet_return_t status, xpc_object_t interface_param) {
            if_status = status;
            if (status != VMNET_SUCCESS || !interface_param) {
                dispatch_semaphore_signal(if_created_sem);
                return;
            }

#ifdef DEBUG
            qemu_log("vmnet.start.interface_param:\n");
            xpc_dictionary_apply(interface_param,
                                 ^bool(const char *k, xpc_object_t v) {
                                     char *desc = xpc_copy_description(v);
                                     qemu_log("  %s=%s\n", k, desc);
                                     free(desc);
                                     return true;
                                 });
#endif /* DEBUG */

            s->mtu = xpc_dictionary_get_uint64(
                interface_param,
                vmnet_mtu_key);
            s->max_packet_size = xpc_dictionary_get_uint64(
                interface_param,
                vmnet_max_packet_size_key);

            dispatch_semaphore_signal(if_created_sem);
        });

    if (s->vmnet_if == NULL) {
        error_setg(errp,
                   "unable to create interface "
                   "with requested params");
        return -1;
    }

    dispatch_semaphore_wait(if_created_sem, DISPATCH_TIME_FOREVER);

    if (if_status != VMNET_SUCCESS) {
        error_setg(errp,
                   "cannot create vmnet interface: %s",
                   vmnet_status_map_str(if_status));
        return -1;
    }

    s->send_bh = aio_bh_new(qemu_get_aio_context(), vmnet_send_bh, nc);
    vmnet_bufs_init(s);
    vmnet_set_send_bh_scheduled(s, false);
    vmnet_set_send_enabled(s, true);
    return 0;
}


ssize_t vmnet_receive_common(NetClientState *nc,
                             const uint8_t *buf,
                             size_t size)
{
    VmnetCommonState *s = DO_UPCAST(VmnetCommonState, nc, nc);
    struct vmpktdesc packet;
    struct iovec iov;
    int pkt_cnt;
    vmnet_return_t if_status;

    if (size > s->max_packet_size) {
        warn_report("vmnet: packet is too big, %zu > %llu\n",
                    packet.vm_pkt_size,
                    s->max_packet_size);
        return -1;
    }

    iov.iov_base = (char *) buf;
    iov.iov_len = size;

    packet.vm_pkt_iovcnt = 1;
    packet.vm_flags = 0;
    packet.vm_pkt_size = size;
    packet.vm_pkt_iov = &iov;
    pkt_cnt = 1;

    if_status = vmnet_write(s->vmnet_if, &packet, &pkt_cnt);
    if (if_status != VMNET_SUCCESS) {
        error_report("vmnet: write error: %s\n",
                     vmnet_status_map_str(if_status));
    }

    if (if_status == VMNET_SUCCESS && pkt_cnt) {
        return size;
    }
    return 0;
}


void vmnet_cleanup_common(NetClientState *nc)
{
    VmnetCommonState *s = DO_UPCAST(VmnetCommonState, nc, nc);;
    dispatch_semaphore_t if_created_sem;

    qemu_purge_queued_packets(nc);
    vmnet_set_send_bh_scheduled(s, true);
    vmnet_set_send_enabled(s, false);

    if (s->vmnet_if == NULL) {
        return;
    }

    if_created_sem = dispatch_semaphore_create(0);
    vmnet_stop_interface(
        s->vmnet_if,
        s->if_queue,
        ^(vmnet_return_t status) {
            assert(status == VMNET_SUCCESS);
            dispatch_semaphore_signal(if_created_sem);
        });
    dispatch_semaphore_wait(if_created_sem, DISPATCH_TIME_FOREVER);

    qemu_bh_delete(s->send_bh);
    dispatch_release(if_created_sem);
    dispatch_release(s->if_queue);

    for (int i = 0; i < VMNET_PACKETS_LIMIT; ++i) {
        g_free(s->iov_buf[i].iov_base);
    }
}
