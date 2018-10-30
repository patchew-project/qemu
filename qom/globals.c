#include "qemu/osdep.h"

#include "hw/qdev.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qom/globals.h"
#include "qom/object_interfaces.h"

static GList *global_props;

void object_property_register_global(GlobalProperty *prop)
{
    global_props = g_list_append(global_props, prop);
}

void object_property_set_globals(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    GList *l;

    for (l = global_props; l; l = l->next) {
        GlobalProperty *prop = l->data;
        Error *err = NULL;

        if (object_dynamic_cast(OBJECT(dev), prop->driver) == NULL) {
            continue;
        }
        prop->used = true;
        object_property_parse(OBJECT(dev), prop->value, prop->property, &err);
        if (err != NULL) {
            error_prepend(&err, "can't apply global %s.%s=%s: ",
                          prop->driver, prop->property, prop->value);
            if (!dev->hotplugged && prop->errp) {
                error_propagate(prop->errp, err);
            } else {
                assert(prop->user_provided);
                warn_report_err(err);
            }
        }
    }
}

int object_property_check_globals(void)
{
    GList *l;
    int ret = 0;

    for (l = global_props; l; l = l->next) {
        GlobalProperty *prop = l->data;
        ObjectClass *oc;
        DeviceClass *dc;
        if (prop->used) {
            continue;
        }
        if (!prop->user_provided) {
            continue;
        }
        oc = object_class_by_name(prop->driver);
        oc = object_class_dynamic_cast(oc, TYPE_DEVICE);
        if (!oc) {
            warn_report("global %s.%s has invalid class name",
                        prop->driver, prop->property);
            ret = 1;
            continue;
        }
        dc = DEVICE_CLASS(oc);
        if (!dc->hotpluggable && !prop->used) {
            warn_report("global %s.%s=%s not used",
                        prop->driver, prop->property, prop->value);
            ret = 1;
            continue;
        }
    }
    return ret;
}
