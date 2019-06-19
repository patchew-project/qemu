/*
 * HMP commands related to QOM
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "monitor/hmp.h"
#include "qapi/qapi-commands-qom.h"
#include "qapi/qmp/qdict.h"
#include "monitor/monitor.h"
#include "qom/object.h"
#include "qapi/error.h"

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
    hmp_handle_error(mon, &err);
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
    hmp_handle_error(mon, &err);
}
