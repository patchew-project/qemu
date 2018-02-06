/*
 * QEMU Hyper-V VMBus
 *
 * Copyright (c) 2017-2018 Virtuozzo International GmbH.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_VMBUS_H
#define QEMU_VMBUS_H

#include "hw/qdev.h"
#include "sysemu/sysemu.h"
#include "sysemu/dma.h"
#include "target/i386/hyperv.h"
#include "target/i386/hyperv-proto.h"
#include "hw/vmbus/vmbus-proto.h"
#include "qemu/uuid.h"

#define TYPE_VMBUS_DEVICE "vmbus-dev"

#define VMBUS_DEVICE(obj) \
    OBJECT_CHECK(VMBusDevice, (obj), TYPE_VMBUS_DEVICE)
#define VMBUS_DEVICE_CLASS(klass) \
    OBJECT_CLASS_CHECK(VMBusDeviceClass, (klass), TYPE_VMBUS_DEVICE)
#define VMBUS_DEVICE_GET_CLASS(obj) \
    OBJECT_GET_CLASS(VMBusDeviceClass, (obj), TYPE_VMBUS_DEVICE)

typedef struct VMBus VMBus;
typedef struct VMBusChannel VMBusChannel;
typedef struct VMBusDevice VMBusDevice;
typedef struct VMBusGpadl VMBusGpadl;

typedef void(*VMBusChannelNotifyCb)(struct VMBusChannel *chan);

typedef struct VMBusDeviceClass {
    DeviceClass parent;

    QemuUUID classid;
    QemuUUID instanceid;     /* Fixed UUID for singleton devices */
    uint16_t channel_flags;
    uint16_t mmio_size_mb;

    void (*vmdev_realize)(VMBusDevice *vdev, Error **errp);
    void (*vmdev_unrealize)(VMBusDevice *vdev, Error **errp);
    void (*vmdev_reset)(VMBusDevice *vdev);
    uint16_t (*num_channels)(VMBusDevice *vdev);
    int (*open_channel) (VMBusDevice *vdev);
    void (*close_channel) (VMBusDevice *vdev);
    VMBusChannelNotifyCb chan_notify_cb;
} VMBusDeviceClass;

typedef struct VMBusDevice {
    DeviceState parent;
    QemuUUID instanceid;
    uint16_t num_channels;
    VMBusChannel *channels;
    AddressSpace *dma_as;
} VMBusDevice;

extern const VMStateDescription vmstate_vmbus_dev;

typedef struct VMBusChanReq {
    VMBusChannel *chan;
    uint16_t pkt_type;
    uint32_t msglen;
    void *msg;
    uint64_t transaction_id;
    void *comp;
    QEMUSGList sgl;
} VMBusChanReq;

VMBusDevice *vmbus_channel_device(VMBusChannel *chan);
VMBusChannel *vmbus_device_channel(VMBusDevice *dev, uint32_t chan_idx);
uint32_t vmbus_channel_idx(VMBusChannel *chan);
void vmbus_notify_channel(VMBusChannel *chan);

void vmbus_create(void);
bool vmbus_exists(void);

int vmbus_channel_send(VMBusChannel *chan, uint16_t pkt_type,
                       void *desc, uint32_t desclen,
                       void *msg, uint32_t msglen,
                       bool need_comp, uint64_t transaction_id);
int vmbus_chan_send_completion(VMBusChanReq *req);
int vmbus_channel_reserve(VMBusChannel *chan,
                          uint32_t desclen, uint32_t msglen);
void *vmbus_channel_recv(VMBusChannel *chan, uint32_t size);
void vmbus_release_req(void *req);

void vmbus_save_req(QEMUFile *f, VMBusChanReq *req);
void *vmbus_load_req(QEMUFile *f, VMBusDevice *dev, uint32_t size);


VMBusGpadl *vmbus_get_gpadl(VMBusChannel *chan, uint32_t gpadl_id);
void vmbus_put_gpadl(VMBusGpadl *gpadl);
uint32_t vmbus_gpadl_len(VMBusGpadl *gpadl);
ssize_t vmbus_iov_to_gpadl(VMBusChannel *chan, VMBusGpadl *gpadl, uint32_t off,
                           const struct iovec *iov, size_t iov_cnt);
int vmbus_map_sgl(QEMUSGList *sgl, DMADirection dir, struct iovec *iov,
                  unsigned iov_cnt, size_t len, size_t off);
void vmbus_unmap_sgl(QEMUSGList *sgl, DMADirection dir, struct iovec *iov,
                     unsigned iov_cnt, size_t accessed);

#endif
