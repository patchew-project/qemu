/*
 * vmnet-common.m - network client wrapper for Apple vmnet.framework
 *
 * Copyright(c) 2021 Vladislav Yaroshchuk <yaroshchuk2000@gmail.com>
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

#ifdef DEBUG
#define D(x) x
#define D_LOG(...) qemu_log(__VA_ARGS__)
#else
#define D(x) do { } while (0)
#define D_LOG(...) do { } while (0)
#endif

typedef struct vmpktdesc vmpktdesc_t;
typedef struct iovec iovec_t;

static void vmnet_set_send_enabled(VmnetCommonState *s, bool enable)
{
    s->send_enabled = enable;
}


static void vmnet_send_completed(NetClientState *nc, ssize_t len)
{
    VmnetCommonState *s = DO_UPCAST(VmnetCommonState, nc, nc);
    vmnet_set_send_enabled(s, true);
}


static void vmnet_send(NetClientState *nc,
                       interface_event_t event_id,
                       xpc_object_t event)
{
    assert(event_id == VMNET_INTERFACE_PACKETS_AVAILABLE);

    VmnetCommonState *s;
    uint64_t packets_available;

    struct iovec *iov;
    struct vmpktdesc *packets;
    int pkt_cnt;
    int i;

    vmnet_return_t if_status;
    ssize_t size;

    s = DO_UPCAST(VmnetCommonState, nc, nc);

    packets_available = xpc_dictionary_get_uint64(
        event,
        vmnet_estimated_packets_available_key
    );

    pkt_cnt = (packets_available < VMNET_PACKETS_LIMIT) ?
              packets_available :
              VMNET_PACKETS_LIMIT;


    iov = s->iov_buf;
    packets = s->packets_buf;

    for (i = 0; i < pkt_cnt; ++i) {
        packets[i].vm_pkt_size = s->max_packet_size;
        packets[i].vm_pkt_iovcnt = 1;
        packets[i].vm_flags = 0;
    }

    if_status = vmnet_read(s->vmnet_if, packets, &pkt_cnt);
    if (if_status != VMNET_SUCCESS) {
        error_printf("vmnet: read failed: %s\n",
                     vmnet_status_map_str(if_status));
    }
    qemu_mutex_lock_iothread();
    for (i = 0; i < pkt_cnt; ++i) {
        size = qemu_send_packet_async(nc,
                                      iov[i].iov_base,
                                      packets[i].vm_pkt_size,
                                      vmnet_send_completed);
        if (size == 0) {
            vmnet_set_send_enabled(s, false);
        } else if (size < 0) {
            break;
        }
    }
    qemu_mutex_unlock_iothread();

}


static void vmnet_register_event_callback(VmnetCommonState *s)
{
    dispatch_queue_t avail_pkt_q = dispatch_queue_create(
        "org.qemu.vmnet.if_queue",
        DISPATCH_QUEUE_SERIAL
    );

    vmnet_interface_set_event_callback(
        s->vmnet_if,
        VMNET_INTERFACE_PACKETS_AVAILABLE,
        avail_pkt_q,
        ^(interface_event_t event_id, xpc_object_t event) {
          if (s->send_enabled) {
              vmnet_send(&s->nc, event_id, event);
          }
        });
}


static void vmnet_bufs_init(VmnetCommonState *s)
{
    int i;
    struct vmpktdesc *packets;
    struct iovec *iov;

    packets = s->packets_buf;
    iov = s->iov_buf;

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
        return "general failure";
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
                    Error **errp,
                    void (*completion_callback)(xpc_object_t interface_param))
{
    VmnetCommonState *s;

    dispatch_queue_t if_create_q;
    dispatch_semaphore_t if_created_sem;

    __block vmnet_return_t if_status;

    if_create_q = dispatch_queue_create("org.qemu.vmnet.create",
                                        DISPATCH_QUEUE_SERIAL);
    if_created_sem = dispatch_semaphore_create(0);

    xpc_dictionary_set_bool(
        if_desc,
        vmnet_allocate_mac_address_key,
        false
    );

    D(D_LOG("vmnet.start.interface_desc:\n");
    xpc_dictionary_apply(if_desc,
                         ^bool(const char *k, xpc_object_t v) {
                           char *desc = xpc_copy_description(v);
                           D_LOG("  %s=%s\n", k, desc);
                           free(desc);
                           return true;
                         }));

    s = DO_UPCAST(VmnetCommonState, nc, nc);
    s->vmnet_if = vmnet_start_interface(
        if_desc,
        if_create_q,
        ^(vmnet_return_t status, xpc_object_t interface_param) {
          if_status = status;
          if (status != VMNET_SUCCESS || !interface_param) {
              dispatch_semaphore_signal(if_created_sem);
              return;
          }

          D(D_LOG("vmnet.start.interface_param:\n");
                xpc_dictionary_apply(interface_param,
                                     ^bool(const char *k, xpc_object_t v) {
                                       char *desc = xpc_copy_description(v);
                                       D_LOG("  %s=%s\n", k, desc);
                                       free(desc);
                                       return true;
                                     }));

          s->mtu = xpc_dictionary_get_uint64(
              interface_param,
              vmnet_mtu_key);
          s->max_packet_size = xpc_dictionary_get_uint64(
              interface_param,
              vmnet_max_packet_size_key);

          if (completion_callback) {
              completion_callback(interface_param);
          }
          dispatch_semaphore_signal(if_created_sem);
        });

    if (s->vmnet_if == NULL) {
        error_setg(errp, "unable to create interface with requested params");
        return -1;
    }

    dispatch_semaphore_wait(if_created_sem, DISPATCH_TIME_FOREVER);
    dispatch_release(if_create_q);

    if (if_status != VMNET_SUCCESS) {
        error_setg(errp,
                   "cannot create vmnet interface: %s",
                   vmnet_status_map_str(if_status));
        return -1;
    }

    vmnet_register_event_callback(s);
    vmnet_bufs_init(s);
    vmnet_set_send_enabled(s, true);

    return 0;
}


ssize_t vmnet_receive_common(NetClientState *nc,
                             const uint8_t *buf,
                             size_t size)
{
    VmnetCommonState *s;
    vmpktdesc_t packet;
    iovec_t iov;
    int pkt_cnt;
    vmnet_return_t if_status;

    s = DO_UPCAST(VmnetCommonState, nc, nc);

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
    VmnetCommonState *s;
    dispatch_queue_t if_destroy_q;

    s = DO_UPCAST(VmnetCommonState, nc, nc);

    qemu_purge_queued_packets(nc);
    vmnet_set_send_enabled(s, false);

    if (s->vmnet_if == NULL) {
        return;
    }

    if_destroy_q = dispatch_queue_create(
        "org.qemu.vmnet.destroy",
        DISPATCH_QUEUE_SERIAL
    );

    vmnet_stop_interface(
        s->vmnet_if,
        if_destroy_q,
        ^(vmnet_return_t status) {
        });

    for (int i = 0; i < VMNET_PACKETS_LIMIT; ++i) {
        g_free(s->iov_buf[i].iov_base);
    }
}
