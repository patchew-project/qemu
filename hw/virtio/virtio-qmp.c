#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qmp-commands.h"

#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-net.h"
#include "hw/virtio/virtio-blk.h"
#include "hw/virtio/virtio-serial.h"
#include "hw/virtio/virtio-balloon.h"
#include "hw/virtio/virtio-scsi.h"
#ifdef CONFIG_VIRTFS
#include "hw/9pfs/virtio-9p.h"
#endif
#include "hw/virtio/virtio-gpu.h"

#define qmp_query_virtio_features(__vdev, __info, __offset, __count,\
                                  __name_kind, __name_type, __name_field) ({ \
    unsigned __idx; \
    for (__idx = (__count); __idx--; ) { \
        unsigned int __fbit = __idx + (__offset); \
        bool __host = virtio_host_has_feature(__vdev, __fbit); \
        bool __guest = virtio_vdev_has_feature(__vdev, __fbit); \
        VirtioFeatureList *__feature; \
 \
        if (!__host && !__guest) { \
            continue; \
        } \
 \
        __feature = g_new0(VirtioFeatureList, 1); \
        __feature->value = g_new0(VirtioFeature, 1); \
        __feature->value->name = g_new0(VirtioFeatureName, 1); \
 \
        __feature->value->host = __host; \
        __feature->value->guest = __guest; \
 \
        __feature->value->name->type = (__name_kind); \
        __feature->value->name->u.__name_field.data = (__name_type)__idx; \
 \
        __feature->next = (__info)->features; \
        (__info)->features = __feature; \
    } \
})

#define qmp_query_virtio_features_common(__vdev, __info) \
    qmp_query_virtio_features((__vdev), (__info), VIRTIO_FEATURE_OTHER__MAX, \
        VIRTIO_FEATURE_COMMON__MAX, VIRTIO_FEATURE_NAME_KIND_COMMON, \
        VirtioFeatureCommon, common)

#define qmp_query_virtio_features_net(__vdev, __info) \
    qmp_query_virtio_features((__vdev), (__info), 0, \
        VIRTIO_FEATURE_NET__MAX, VIRTIO_FEATURE_NAME_KIND_NET, \
        VirtioFeatureNet, net)

#define qmp_query_virtio_features_blk(__vdev, __info) \
    qmp_query_virtio_features((__vdev), (__info), 0, \
        VIRTIO_FEATURE_BLK__MAX, VIRTIO_FEATURE_NAME_KIND_BLK, \
        VirtioFeatureBlk, blk)

#define qmp_query_virtio_features_serial(__vdev, __info) \
    qmp_query_virtio_features((__vdev), (__info), 0, \
        VIRTIO_FEATURE_SERIAL__MAX, VIRTIO_FEATURE_NAME_KIND_SERIAL, \
        VirtioFeatureSerial, serial)

#define qmp_query_virtio_features_balloon(__vdev, __info) \
    qmp_query_virtio_features((__vdev), (__info), 0, \
        VIRTIO_FEATURE_BALLOON__MAX, VIRTIO_FEATURE_NAME_KIND_BALLOON, \
        VirtioFeatureBalloon, balloon)

#define qmp_query_virtio_features_scsi(__vdev, __info) \
    qmp_query_virtio_features((__vdev), (__info), 0, \
        VIRTIO_FEATURE_SCSI__MAX, VIRTIO_FEATURE_NAME_KIND_SCSI, \
        VirtioFeatureScsi, scsi)

#ifdef CONFIG_VIRTFS
#define qmp_query_virtio_features_virtfs(__vdev, __info) \
    qmp_query_virtio_features((__vdev), (__info), 0, \
        VIRTIO_FEATURE_VIRTFS__MAX, VIRTIO_FEATURE_NAME_KIND_VIRTFS, \
        VirtioFeatureVirtfs, virtfs)
#endif

#define qmp_query_virtio_features_gpu(__vdev, __info) \
    qmp_query_virtio_features((__vdev), (__info), 0, \
        VIRTIO_FEATURE_GPU__MAX, VIRTIO_FEATURE_NAME_KIND_GPU, \
        VirtioFeatureGpu, gpu)

#define qmp_query_virtio_features_other(__vdev, __info) \
    qmp_query_virtio_features((__vdev), (__info), 0, \
        VIRTIO_FEATURE_OTHER__MAX, VIRTIO_FEATURE_NAME_KIND_OTHER, \
        VirtioFeatureOther, other)


static VirtioInfoList *qmp_query_virtio_one(VirtIODevice *vdev)
{
    VirtioInfoList *list;
    VirtioInfo *info;
    unsigned int idx;

    QEMU_BUILD_BUG_ON(VIRTIO_FEATURE_COMMON__MAX != 40);
    QEMU_BUILD_BUG_ON(VIRTIO_FEATURE_NET__MAX != 24);
    QEMU_BUILD_BUG_ON(VIRTIO_FEATURE_BLK__MAX != 24);
    QEMU_BUILD_BUG_ON(VIRTIO_FEATURE_SCSI__MAX != 24);
    QEMU_BUILD_BUG_ON(VIRTIO_FEATURE_BALLOON__MAX != 24);
    QEMU_BUILD_BUG_ON(VIRTIO_FEATURE_SERIAL__MAX != 24);
    QEMU_BUILD_BUG_ON(VIRTIO_FEATURE_GPU__MAX != 24);
    QEMU_BUILD_BUG_ON(VIRTIO_FEATURE_OTHER__MAX != 24);

    list = g_new0(VirtioInfoList, 1);
    list->value = g_new0(VirtioInfo, 1);

    info = list->value;
    info->qom_path = object_get_canonical_path(OBJECT(vdev));
    info->status = vdev->status;
    info->host_features = vdev->host_features;
    info->guest_features = vdev->guest_features;

    /* device status */
    for (idx = VIRTIO_STATUS__MAX; idx--; ) {
        VirtioStatusList *status;

        if (!(vdev->status & (1 << idx))) {
            continue;
        }

        status = g_new0(VirtioStatusList, 1);
        status->value = (VirtioStatus)idx;

        status->next = info->status_names;
        info->status_names = status;
    }

    /* device-specific features */
    if (object_dynamic_cast(OBJECT(vdev), TYPE_VIRTIO_NET)) {
        qmp_query_virtio_features_net(vdev, info);
    } else if (object_dynamic_cast(OBJECT(vdev), TYPE_VIRTIO_BLK)) {
        qmp_query_virtio_features_blk(vdev, info);
    } else if (object_dynamic_cast(OBJECT(vdev), TYPE_VIRTIO_SERIAL)) {
        qmp_query_virtio_features_serial(vdev, info);
    } else if (object_dynamic_cast(OBJECT(vdev), TYPE_VIRTIO_BALLOON)) {
        qmp_query_virtio_features_balloon(vdev, info);
    } else if (object_dynamic_cast(OBJECT(vdev), TYPE_VIRTIO_SCSI)) {
        qmp_query_virtio_features_scsi(vdev, info);
#ifdef CONFIG_VIRTFS
    } else if (object_dynamic_cast(OBJECT(vdev), TYPE_VIRTIO_9P)) {
        qmp_query_virtio_features_virtfs(vdev, info);
#endif
    } else if (object_dynamic_cast(OBJECT(vdev), TYPE_VIRTIO_GPU)) {
        qmp_query_virtio_features_gpu(vdev, info);
    } else {
        qmp_query_virtio_features_other(vdev, info);
    }

    /* common features */
    qmp_query_virtio_features_common(vdev, info);

    return list;
}

typedef struct QueryVirtioEntry {
    VirtIODevice *vdev;
    QTAILQ_ENTRY(QueryVirtioEntry) link;
} QueryVirtioEntry;

typedef QTAILQ_HEAD(, QueryVirtioEntry) QueryVirtioHead;

static void qmp_query_virtio_recursive(QueryVirtioHead *head, BusState *bus)
{
    BusChild *kid;

    QTAILQ_FOREACH(kid, &bus->children, sibling) {
        DeviceState *dev = kid->child;
        BusState *child;

        if (object_dynamic_cast(OBJECT(dev), TYPE_VIRTIO_DEVICE)) {
            QueryVirtioEntry *entry = g_new0(QueryVirtioEntry, 1);

            entry->vdev = VIRTIO_DEVICE(dev);
            QTAILQ_INSERT_TAIL(head, entry, link);
        }
        QLIST_FOREACH(child, &dev->child_bus, sibling) {
            qmp_query_virtio_recursive(head, child);
        }
    }
}

VirtioInfoList *qmp_query_virtio(bool has_path, const char *path, Error **errp)
{
    BusState *bus = sysbus_get_default();
    VirtioInfoList *list = NULL;

    if (!bus) {
        return NULL;
    }

    if (has_path) {
        Object *obj = object_resolve_path(path, NULL);
        if (!obj) {
            return NULL;
        }
        if (!object_dynamic_cast(OBJECT(obj), TYPE_VIRTIO_DEVICE)) {
            return NULL;
        }

        list = qmp_query_virtio_one(VIRTIO_DEVICE(obj));
    } else {
        QueryVirtioHead head;
        QueryVirtioEntry *query, *tmp;

        QTAILQ_INIT(&head);
        qmp_query_virtio_recursive(&head, bus);

        QTAILQ_FOREACH_SAFE(query, &head, link, tmp) {
            VirtioInfoList *entry = qmp_query_virtio_one(query->vdev);

            QTAILQ_REMOVE(&head, query, link);
            g_free(query);

            entry->next = list;
            list = entry;
        }
    }

    return list;
}
