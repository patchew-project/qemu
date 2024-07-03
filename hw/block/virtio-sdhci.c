#include "qemu/osdep.h"
#include "qapi/error.h"

#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-sdhci.h"
#include "qemu/typedefs.h"
#include "sysemu/blockdev.h"
#include "standard-headers/linux/virtio_ids.h"

typedef struct sd_req {
    uint32_t opcode;
    uint32_t arg;
} sd_req;

typedef struct virtio_sdhci_req {
    uint8_t flags;

#define VIRTIO_SDHCI_REQUEST_DATA BIT(1)
#define VIRTIO_SDHCI_REQUEST_WRITE BIT(2)
#define VIRTIO_SDHCI_REQUEST_STOP BIT(3)
#define VIRTIO_SDHCI_REQUEST_SBC BIT(4)

    sd_req request;

    uint8_t buf[4096];
    size_t buf_len;

    sd_req stop_req;
    sd_req sbc_req;
} virtio_sdhci_req;

typedef struct virtio_sdhci_resp {
    uint32_t response[4];
    int resp_len;
    uint8_t buf[4096];
} virtio_sdhci_resp;

static void send_command(SDBus *sdbus, sd_req *mmc_request, uint8_t *response,
                         virtio_sdhci_resp *virtio_resp)
{
    SDRequest sdreq;
    int resp_len;

    sdreq.cmd = (uint8_t)mmc_request->opcode;
    sdreq.arg = mmc_request->arg;
    resp_len = sdbus_do_command(sdbus, &sdreq, response);
    virtio_resp->resp_len = resp_len;

    for (int i = 0; i < resp_len / sizeof(uint32_t); i++) {
        virtio_resp->response[i] = ldl_be_p(&virtio_resp->response[i]);
    }
}

static void send_command_without_response(SDBus *sdbus, sd_req *mmc_request)
{
    SDRequest sdreq;
    uint8_t response[4];

    sdreq.cmd = (uint8_t)mmc_request->opcode;
    sdreq.arg = mmc_request->arg;
    sdbus_do_command(sdbus, &sdreq, response);
}

static void handle_mmc_request(VirtIODevice *vdev, virtio_sdhci_req *virtio_req,
                               virtio_sdhci_resp *virtio_resp)
{
    VirtIOSDHCI *vsd = VIRTIO_SDHCI(vdev);
    SDBus *sdbus = &vsd->sdbus;

    if (virtio_req->flags & VIRTIO_SDHCI_REQUEST_SBC) {
        send_command_without_response(sdbus, &virtio_req->sbc_req);
    }

    send_command(sdbus, &virtio_req->request,
    (uint8_t *)virtio_resp->response, virtio_resp);

    if (virtio_req->flags & VIRTIO_SDHCI_REQUEST_DATA) {
        if (virtio_req->flags & VIRTIO_SDHCI_REQUEST_WRITE) {
            sdbus_write_data(sdbus, virtio_req->buf, virtio_req->buf_len);
        } else {
            sdbus_read_data(sdbus, virtio_resp->buf, virtio_req->buf_len);
        }
    }

    if (virtio_req->flags & VIRTIO_SDHCI_REQUEST_STOP) {
        send_command_without_response(sdbus, &virtio_req->stop_req);
    }
}

static void handle_request(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtQueueElement *elem;
    virtio_sdhci_req virtio_req;
    virtio_sdhci_resp virtio_resp;

    elem = virtqueue_pop(vq, sizeof(VirtQueueElement));

    iov_to_buf(elem->out_sg, elem->out_num, 0,
    &virtio_req, sizeof(virtio_sdhci_req));

    handle_mmc_request(vdev, &virtio_req, &virtio_resp);

    iov_from_buf(elem->in_sg, elem->in_num, 0,
    &virtio_resp, sizeof(virtio_sdhci_resp));

    virtqueue_push(vq, elem, 1);

    virtio_notify(vdev, vq);
}

static void virtio_sdhci_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOSDHCI *vsd = VIRTIO_SDHCI(dev);

    virtio_init(vdev, VIRTIO_ID_SDHCI, 0);

    vsd->vq = virtio_add_queue(vdev, 1, handle_request);

    BlockBackend *blk = vsd->blk;
    if (!blk) {
        error_setg(errp, "Block backend not found");
        return;
    }

    qbus_init(&vsd->sdbus, sizeof(vsd->sdbus), TYPE_SD_BUS, dev, "sd-bus");
    DeviceState *card = qdev_new(TYPE_SD_CARD);
    qdev_prop_set_drive_err(card, "drive", blk, &error_fatal);
    qdev_realize_and_unref(card,
    qdev_get_child_bus(dev, "sd-bus"), &error_fatal);
}

static void virtio_sdhci_unrealize(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    virtio_cleanup(vdev);
}

static uint64_t virtio_sdhci_get_features(VirtIODevice *vdev,
                                          uint64_t features, Error **errp)
{
    return features;
}

static void virtio_sdhci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *k = VIRTIO_DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);

    k->realize = virtio_sdhci_realize;
    k->unrealize = virtio_sdhci_unrealize;
    k->get_features = virtio_sdhci_get_features;
}

static const TypeInfo virtio_sdhci_info = {
    .name = TYPE_VIRTIO_SDHCI,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VirtIOSDHCI),
    .class_init = virtio_sdhci_class_init,
};

static void virtio_register_types(void)
{
    type_register_static(&virtio_sdhci_info);
}

type_init(virtio_register_types)
