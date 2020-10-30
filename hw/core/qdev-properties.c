#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qapi/qapi-types-misc.h"
#include "qapi/qmp/qerror.h"
#include "qemu/ctype.h"
#include "qemu/error-report.h"
#include "qapi/visitor.h"
#include "qemu/uuid.h"
#include "qemu/units.h"
#include "qemu/cutils.h"
#include "qom/static-property-internal.h"

void qdev_prop_set_after_realize(DeviceState *dev, const char *name,
                                  Error **errp)
{
    if (dev->id) {
        error_setg(errp, "Attempt to set property '%s' on device '%s' "
                   "(type '%s') after it was realized", name, dev->id,
                   object_get_typename(OBJECT(dev)));
    } else {
        error_setg(errp, "Attempt to set property '%s' on anonymous device "
                   "(type '%s') after it was realized", name,
                   object_get_typename(OBJECT(dev)));
    }
}

/* returns: true if property is allowed to be set, false otherwise */
static bool qdev_prop_allow_set(Object *obj, ObjectProperty *op,
                                Error **errp)
{
    DeviceState *dev = DEVICE(obj);

    if (dev->realized) {
        qdev_prop_set_after_realize(dev, op->name, errp);
        return false;
    }
    return true;
}

void qdev_prop_allow_set_link_before_realize(const Object *obj,
                                             const char *name,
                                             Object *val, Error **errp)
{
    DeviceState *dev = DEVICE(obj);

    if (dev->realized) {
        error_setg(errp, "Attempt to set link property '%s' on device '%s' "
                   "(type '%s') after it was realized",
                   name, dev->id, object_get_typename(obj));
    }
}

/* --- public helpers --- */

static Property *qdev_prop_walk(Property *props, const char *name)
{
    if (!props) {
        return NULL;
    }
    while (props->name) {
        if (strcmp(props->name, name) == 0) {
            return props;
        }
        props++;
    }
    return NULL;
}

static Property *qdev_prop_find(DeviceState *dev, const char *name)
{
    ObjectClass *class;
    Property *prop;

    /* device properties */
    class = object_get_class(OBJECT(dev));
    do {
        prop = qdev_prop_walk(DEVICE_CLASS(class)->props_, name);
        if (prop) {
            return prop;
        }
        class = object_class_get_parent(class);
    } while (class != object_class_by_name(TYPE_DEVICE));

    return NULL;
}

void qdev_prop_set_bit(DeviceState *dev, const char *name, bool value)
{
    object_property_set_bool(OBJECT(dev), name, value, &error_abort);
}

void qdev_prop_set_uint8(DeviceState *dev, const char *name, uint8_t value)
{
    object_property_set_int(OBJECT(dev), name, value, &error_abort);
}

void qdev_prop_set_uint16(DeviceState *dev, const char *name, uint16_t value)
{
    object_property_set_int(OBJECT(dev), name, value, &error_abort);
}

void qdev_prop_set_uint32(DeviceState *dev, const char *name, uint32_t value)
{
    object_property_set_int(OBJECT(dev), name, value, &error_abort);
}

void qdev_prop_set_int32(DeviceState *dev, const char *name, int32_t value)
{
    object_property_set_int(OBJECT(dev), name, value, &error_abort);
}

void qdev_prop_set_uint64(DeviceState *dev, const char *name, uint64_t value)
{
    object_property_set_int(OBJECT(dev), name, value, &error_abort);
}

void qdev_prop_set_string(DeviceState *dev, const char *name, const char *value)
{
    object_property_set_str(OBJECT(dev), name, value, &error_abort);
}

void qdev_prop_set_enum(DeviceState *dev, const char *name, int value)
{
    Property *prop;

    prop = qdev_prop_find(dev, name);
    object_property_set_str(OBJECT(dev), name,
                            qapi_enum_lookup(prop->info->enum_table, value),
                            &error_abort);
}

static GPtrArray *global_props(void)
{
    static GPtrArray *gp;

    if (!gp) {
        gp = g_ptr_array_new();
    }

    return gp;
}

void qdev_prop_register_global(GlobalProperty *prop)
{
    g_ptr_array_add(global_props(), prop);
}

const GlobalProperty *qdev_find_global_prop(Object *obj,
                                            const char *name)
{
    GPtrArray *props = global_props();
    const GlobalProperty *p;
    int i;

    for (i = 0; i < props->len; i++) {
        p = g_ptr_array_index(props, i);
        if (object_dynamic_cast(obj, p->driver)
            && !strcmp(p->property, name)) {
            return p;
        }
    }
    return NULL;
}

int qdev_prop_check_globals(void)
{
    int i, ret = 0;

    for (i = 0; i < global_props()->len; i++) {
        GlobalProperty *prop;
        ObjectClass *oc;
        DeviceClass *dc;

        prop = g_ptr_array_index(global_props(), i);
        if (prop->used) {
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

void qdev_prop_set_globals(DeviceState *dev)
{
    object_apply_global_props(OBJECT(dev), global_props(),
                              dev->hotplugged ? NULL : &error_fatal);
}

void qdev_property_add_static(DeviceState *dev, Property *prop)
{
    object_property_add_static(OBJECT(dev), prop, qdev_prop_allow_set);
}

/**
 * Legacy property handling
 */

static void qdev_get_legacy_property(Object *obj, Visitor *v,
                                     const char *name, void *opaque,
                                     Error **errp)
{
    Property *prop = opaque;

    char buffer[1024];
    char *ptr = buffer;

    prop->info->print(obj, prop, buffer, sizeof(buffer));
    visit_type_str(v, name, &ptr, errp);
}

/**
 * qdev_class_add_legacy_property:
 * @dev: Device to add the property to.
 * @prop: The qdev property definition.
 *
 * Add a legacy QOM property to @dev for qdev property @prop.
 *
 * Legacy properties are string versions of QOM properties.  The format of
 * the string depends on the property type.  Legacy properties are only
 * needed for "info qtree".
 *
 * Do not use this in new code!  QOM Properties added through this interface
 * will be given names in the "legacy" namespace.
 */
static void qdev_class_add_legacy_property(DeviceClass *dc, Property *prop)
{
    g_autofree char *name = NULL;

    /* Register pointer properties as legacy properties */
    if (!prop->info->print && prop->info->get) {
        return;
    }

    name = g_strdup_printf("legacy-%s", prop->name);
    object_class_property_add(OBJECT_CLASS(dc), name, "str",
        prop->info->print ? qdev_get_legacy_property : prop->info->get,
        NULL, NULL, prop);
}

static void qdev_class_add_legacy_properties(DeviceClass *dc, Property *props)
{
    Property *prop;
    for (prop = props; prop && prop->name; prop++) {
        qdev_class_add_legacy_property(dc, prop);
    }
}
void device_class_set_props(DeviceClass *dc, Property *props)
{
    dc->props_ = props;
    qdev_class_add_legacy_properties(dc, props);
    object_class_add_static_props(OBJECT_CLASS(dc), props, qdev_prop_allow_set);
}

void qdev_alias_all_properties(DeviceState *target, Object *source)
{
    ObjectClass *class;
    Property *prop;

    class = object_get_class(OBJECT(target));
    do {
        DeviceClass *dc = DEVICE_CLASS(class);

        for (prop = dc->props_; prop && prop->name; prop++) {
            object_property_add_alias(source, prop->name,
                                      OBJECT(target), prop->name);
        }
        class = object_class_get_parent(class);
    } while (class != object_class_by_name(TYPE_DEVICE));
}
