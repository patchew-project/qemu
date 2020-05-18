/*
 * HMP commands related to QOM
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/qdev-core.h"
#include "monitor/hmp.h"
#include "monitor/monitor.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-qom.h"
#include "qapi/qmp/qdict.h"
#include "qom/object.h"

void hmp_qom_list(Monitor *mon, const QDict *qdict)
{
    const char *path = qdict_get_try_str(qdict, "path");
    ObjectPropertyInfoList *list;
    Error *err = NULL;

    if (path == NULL) {
        monitor_printf(mon, "/\n");
        return;
    }

    list = qmp_qom_list(path, &err);
    if (err == NULL) {
        ObjectPropertyInfoList *start = list;
        while (list != NULL) {
            ObjectPropertyInfo *value = list->value;

            monitor_printf(mon, "%s (%s)\n",
                           value->name, value->type);
            list = list->next;
        }
        qapi_free_ObjectPropertyInfoList(start);
    }
    hmp_handle_error(mon, err);
}

void hmp_qom_set(Monitor *mon, const QDict *qdict)
{
    const char *path = qdict_get_str(qdict, "path");
    const char *property = qdict_get_str(qdict, "property");
    const char *value = qdict_get_str(qdict, "value");
    Error *err = NULL;
    bool ambiguous = false;
    Object *obj;

    obj = object_resolve_path(path, &ambiguous);
    if (obj == NULL) {
        error_set(&err, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Device '%s' not found", path);
    } else {
        if (ambiguous) {
            monitor_printf(mon, "Warning: Path '%s' is ambiguous\n", path);
        }
        object_property_parse(obj, value, property, &err);
    }
    hmp_handle_error(mon, err);
}

typedef struct QOMCompositionState {
    Monitor *mon;
    int indent;
} QOMCompositionState;

static void print_qom_composition(Monitor *mon, Object *obj, int indent);

static int print_qom_composition_child(Object *obj, void *opaque)
{
    QOMCompositionState *s = opaque;

    print_qom_composition(s->mon, obj, s->indent);

    return 0;
}

static int qom_composition_compare(const void *a, const void *b, void *ignore)
{
    Object *obja = (void *)a, *objb = (void *)b;
    const char *namea, *nameb;

    if (obja == object_get_root()) {
        namea = g_strdup("");
    } else {
        namea = object_get_canonical_path_component(obja);
    }

    if (objb == object_get_root()) {
        nameb = g_strdup("");
    } else {
        nameb = object_get_canonical_path_component(objb);
    }


    return strcmp(namea, nameb);
}

static int insert_qom_composition_child(Object *obj, void *opaque)
{
    GQueue *children = opaque;

     g_queue_insert_sorted(children, obj, qom_composition_compare, NULL);
     return 0;
}

static void print_qom_composition(Monitor *mon, Object *obj, int indent)
{
    QOMCompositionState s = {
        .mon = mon,
        .indent = indent + 2,
    };
    char *name;

    if (obj == object_get_root()) {
        name = g_strdup("");
    } else {
        name = object_get_canonical_path_component(obj);
    }

    DeviceState *dev = (DeviceState *)object_dynamic_cast(obj, TYPE_DEVICE);
    DeviceClass *dc = (DeviceClass *)object_class_dynamic_cast(obj->class, TYPE_DEVICE);
    if (dev && !dev->realized) {
        monitor_printf(mon, "### unrealized: %s (%s)\n",
                       name, object_get_typename(obj));
    }
    if (dev && dc->bus_type && !dev->parent_bus) {
        monitor_printf(mon, "### no %s bus: %s (%s)\n",
                       dc->bus_type, name, object_get_typename(obj));
    }
    monitor_printf(mon, "%*s/%s (%s)\n", indent, "", name,
                   object_get_typename(obj));
    g_free(name);

    GQueue children;
    Object *child;
    g_queue_init(&children);
    object_child_foreach(obj, insert_qom_composition_child, &children);
    while ((child = g_queue_pop_head(&children))) {
        print_qom_composition(mon, child, indent + 2);
    }
    (void)s;
    (void)print_qom_composition_child;
}

void hmp_info_qom_tree(Monitor *mon, const QDict *dict)
{
    const char *path = qdict_get_try_str(dict, "path");
    Object *obj;
    bool ambiguous = false;

    if (path) {
        obj = object_resolve_path(path, &ambiguous);
        if (!obj) {
            monitor_printf(mon, "Path '%s' could not be resolved.\n", path);
            return;
        }
        if (ambiguous) {
            monitor_printf(mon, "Warning: Path '%s' is ambiguous.\n", path);
            return;
        }
    } else {
        obj = qdev_get_machine();
    }
    print_qom_composition(mon, obj, 0);
}
