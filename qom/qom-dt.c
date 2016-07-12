/*
 * Device Tree (DeviceClass hierarchy)
 *
 * Copyright ISP RAS, 2016
 *
 * Created on: Jul 6, 2016
 *     Author: Oleg Goremykin <goremykin@ispras.ru>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qom/qom-dt.h"

#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qint.h"
#include "hw/qdev-core.h"

#define DT_PROPERTY "property"
#define DT_NAME "name"
#define DT_TYPE "type"
#define DT_CHILDREN "children"

typedef struct _QOMTreeData QOMTreeData;

struct _QOMTreeData {
    ObjectClass *root_class;
    QOMTreeData *prev;
    QList *list;
};

static void dt_put_props(Property *props, QDict *device)
{
    if (!props) {
        return;
    }

    QList *props_list = qlist_new();
    QString *prop_name;
    QInt *prop_type;
    QDict *prop;

    while (props->name) {
        prop = qdict_new();
        prop_name = qstring_from_str(props->name);
        qdict_put_obj(prop, DT_NAME, QOBJECT(prop_name));
        prop_type = qint_from_int(props->qtype);
        qdict_put_obj(prop, DT_TYPE, QOBJECT(prop_type));
        qlist_append_obj(props_list, QOBJECT(prop));

        props++;
    }

    qdict_put_obj(device, DT_PROPERTY, QOBJECT(props_list));
}

static void dt_create_tree(ObjectClass *cur_class, void *opaque)
{
    QOMTreeData *cur_data = (QOMTreeData *) opaque;
    QOMTreeData data;
    QString *qstring;
    const char *dev_type;

    if (object_class_get_parent(cur_class) != cur_data->root_class) {
        return;
    }

    dev_type = object_class_get_name(cur_class);

    if (object_class_dynamic_cast(cur_class, TYPE_DEVICE)) {
        QList *child_list = qlist_new();
        QDict *device = qdict_new();

        qstring = qstring_from_str(dev_type);

        qdict_put_obj(device, DT_TYPE, QOBJECT(qstring));
        qdict_put_obj(device, DT_CHILDREN, QOBJECT(child_list));
        dt_put_props(DEVICE_CLASS(cur_class)->props, device);

        qlist_append_obj(cur_data->list, QOBJECT(device));

        data.list = child_list;
    } else {
        data.list = cur_data->list;
    }

    data.prev = cur_data;
    data.root_class = cur_class;

    object_class_foreach(dt_create_tree, object_class_get_name(data.root_class),
                         1, (void *) &data);
}

static void dt_del_empty_child(QList *device_list, QDict *device)
{
    const QListEntry *entry;
    if (device) {
        if (qlist_size(device_list) == 0) {
            qdict_del(device, DT_CHILDREN);
            return;
        }
    }

    if (device_list) {
        entry = qlist_first(device_list);
        while (entry) {
            device = qobject_to_qdict(qlist_entry_obj(entry));
            dt_del_empty_child(qobject_to_qlist(qdict_get(device, DT_CHILDREN)),
                               device);
            entry = qlist_next(entry);
        }
    }
}

int dt_printf(const char *file_name)
{
    FILE *output_file = fopen(file_name, "w");

    if (!output_file) {
        fprintf(stderr, "Couldn't open \"%s\": %s", file_name, strerror(errno));
        return 1;
    }

    QOMTreeData data;
    QString *str_json;

    data.prev = 0;
    data.root_class = NULL;
    data.list = qlist_new();

    object_class_foreach(dt_create_tree, NULL, 1, (void *) &data);
    dt_del_empty_child(data.list, NULL);

    str_json = qobject_to_json_pretty(QOBJECT(data.list));
    fprintf(output_file, "%s\n", qstring_get_str(str_json));

    QDECREF(data.list);

    fclose(output_file);
    return 0;
}
