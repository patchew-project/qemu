#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qmp-commands.h"

#include "hw/virtio/virtio.h"

static VirtioInfoList *qmp_query_virtio_one(VirtIODevice *vdev)
{
    VirtioInfoList *info_list;
    VirtioInfo *info;
    unsigned int idx;

    info_list = g_new0(VirtioInfoList, 1);
    info_list->value = g_new0(VirtioInfo, 1);

    info = info_list->value;
    info->qom_path = object_get_canonical_path(OBJECT(vdev));
    info->status = vdev->status;
    info->host_features = vdev->host_features;
    info->guest_features = vdev->guest_features;

    for (idx = 8; idx--; ) {
        const char *name = virtio_tell_status_name(vdev, idx);
        strList *status;

        if (!name) {
            continue;
        }
        if (!(vdev->status & (1 << idx))) {
            continue;
        }

        status = g_new(strList, 1);
        status->value = g_strdup(name);

        status->next = info->status_names;
        info->status_names = status;
    }

    for (idx = 64; idx--; ) {
        const char *name = virtio_tell_device_feature_name(vdev, idx);
        VirtioFeatureList **head = &info->device_features_names;
        VirtioFeatureList *feature;

        if (!name) {
            name = virtio_tell_common_feature_name(vdev, idx);
            head = &info->common_features_names;
        }
        if (!name) {
            continue;
        }
        if (!virtio_host_has_feature(vdev, idx)) {
            continue;
        }

        feature = g_new0(VirtioFeatureList, 1);
        feature->value = g_new0(VirtioFeature, 1);

        feature->value->name = g_strdup(name);
        feature->value->acked = virtio_vdev_has_feature(vdev, idx);

        feature->next = *head;
        *head = feature;
    }

    return info_list;
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
