#include "qemu/osdep.h"
#include "qapi/error.h"

#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-mmc.h"
#include "qemu/typedefs.h"
#include "sysemu/blockdev.h"

typedef struct mmc_req {
    uint32_t opcode;
    uint32_t arg;
} mmc_req;

typedef struct virtio_mmc_req {
    uint8_t flags;

#define VIRTIO_MMC_REQUEST_DATA BIT(1)
#define VIRTIO_MMC_REQUEST_WRITE BIT(2)
#define VIRTIO_MMC_REQUEST_STOP BIT(3)
#define VIRTIO_MMC_REQUEST_SBC BIT(4)

    mmc_req request;

    uint8_t buf[4096];
    size_t buf_len;

    mmc_req stop_req;
    mmc_req sbc_req;
} virtio_mmc_req;

typedef struct virtio_mmc_resp {
    uint32_t response[4];
    int resp_len;
    uint8_t buf[4096];
} virtio_mmc_resp;

static void send_command(SDBus *sdbus, mmc_req *mmc_request, uint8_t *response,
                         virtio_mmc_resp *virtio_resp)
{
    SDRequest sdreq;
    sdreq.cmd = (uint8_t)mmc_request->opcode;
    sdreq.arg = mmc_request->arg;
    int resp_len = sdbus_do_command(sdbus, &sdreq, response);
    virtio_resp->resp_len = resp_len;

    for (int i = 0; i < resp_len / sizeof(uint32_t); i++) {
        virtio_resp->response[i] = ldl_be_p(&virtio_resp->response[i]);
    }
}

static void send_command_without_response(SDBus *sdbus, mmc_req *mmc_request)
{
    SDRequest sdreq;
    sdreq.cmd = (uint8_t)mmc_request->opcode;
    sdreq.arg = mmc_request->arg;
    uint8_t response[4];
    sdbus_do_command(sdbus, &sdreq, response);
}

static void handle_mmc_request(VirtIODevice *vdev, virtio_mmc_req *virtio_req,
                               virtio_mmc_resp *virtio_resp)
{
    VirtIOMMC *vmmc = VIRTIO_MMC(vdev);
    SDBus *sdbus = &vmmc->sdbus;

    if (virtio_req->flags & VIRTIO_MMC_REQUEST_SBC) {
        send_command_without_response(sdbus, &virtio_req->sbc_req);
    }

    send_command(sdbus, &virtio_req->request,
    (uint8_t *)virtio_resp->response, virtio_resp);

    if (virtio_req->flags & VIRTIO_MMC_REQUEST_DATA) {
        if (virtio_req->flags & VIRTIO_MMC_REQUEST_WRITE) {
            sdbus_write_data(sdbus, virtio_req->buf, virtio_req->buf_len);
        } else {
            sdbus_read_data(sdbus, virtio_resp->buf, virtio_req->buf_len);
        }
    }

    if (virtio_req->flags & VIRTIO_MMC_REQUEST_STOP) {
        send_command_without_response(sdbus, &virtio_req->stop_req);
    }
}

static void handle_request(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtQueueElement *elem;
    virtio_mmc_req virtio_req;
    virtio_mmc_resp virtio_resp;

    elem = virtqueue_pop(vq, sizeof(VirtQueueElement));

    iov_to_buf(elem->out_sg, elem->out_num, 0,
    &virtio_req, sizeof(virtio_mmc_req));

    handle_mmc_request(vdev, &virtio_req, &virtio_resp);

    iov_from_buf(elem->in_sg, elem->in_num, 0,
    &virtio_resp, sizeof(virtio_mmc_resp));

    virtqueue_push(vq, elem, 1);

    virtio_notify(vdev, vq);
}

static void virtio_mmc_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOMMC *vmmc = VIRTIO_MMC(dev);

    virtio_init(vdev, VIRTIO_ID_MMC, 0);

    vmmc->vq = virtio_add_queue(vdev, 1, handle_request);

    BlockBackend *blk = vmmc->blk;
    if (!blk) {
        error_setg(errp, "Block backend not found");
        return;
    }

    qbus_init(&vmmc->sdbus, sizeof(vmmc->sdbus), TYPE_SD_BUS, dev, "sd-bus");
    DeviceState *card = qdev_new(TYPE_SD_CARD);
    qdev_prop_set_drive_err(card, "drive", blk, &error_fatal);
    qdev_realize_and_unref(card,
    qdev_get_child_bus(dev, "sd-bus"), &error_fatal);
}

static void virtio_mmc_unrealize(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    virtio_cleanup(vdev);
}

static uint64_t virtio_mmc_get_features(VirtIODevice *vdev,
                                        uint64_t features, Error **errp)
{
    return features;
}

static void virtio_mmc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *k = VIRTIO_DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);

    k->realize = virtio_mmc_realize;
    k->unrealize = virtio_mmc_unrealize;
    k->get_features = virtio_mmc_get_features;
}

static const TypeInfo virtio_mmc_info = {
    .name = TYPE_VIRTIO_MMC,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VirtIOMMC),
    .class_init = virtio_mmc_class_init,
};

static void virtio_register_types(void)
{
    type_register_static(&virtio_mmc_info);
}

type_init(virtio_register_types)
