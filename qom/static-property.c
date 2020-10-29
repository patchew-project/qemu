/*
 * QOM static property API implementation
 */
#include "qemu/osdep.h"
#include "qom/static-property.h"
#include "qom/static-property-internal.h"

void *object_static_prop_ptr(Object *obj, Property *prop)
{
    void *ptr = obj;
    ptr += prop->offset;
    return ptr;
}

static void static_prop_get(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    Property *prop = opaque;
    return prop->info->get(obj, v, name, opaque, errp);
}

/**
 * static_prop_getter: Return getter function to be used for property
 *
 * Return value can be NULL if @info has no getter function.
 */
static ObjectPropertyAccessor *static_prop_getter(const PropertyInfo *info)
{
    return info->get ? static_prop_get : NULL;
}

static void static_prop_set(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    Property *prop = opaque;

    return prop->info->set(obj, v, name, opaque, errp);
}

/**
 * static_prop_setter: Return setter function to be used for property
 *
 * Return value can be NULL if @info has not setter function.
 */
static ObjectPropertyAccessor *static_prop_setter(const PropertyInfo *info)
{
    return info->set ? static_prop_set : NULL;
}

ObjectProperty *
object_property_add_static(Object *obj, Property *prop,
                           ObjectPropertyAllowSet allow_set)
{
    ObjectProperty *op;

    assert(!prop->info->create);

    op = object_property_add(obj, prop->name, prop->info->name,
                             static_prop_getter(prop->info),
                             static_prop_setter(prop->info),
                             prop->info->release,
                             prop);

    object_property_set_description(obj, prop->name,
                                    prop->info->description);

    if (prop->set_default) {
        prop->info->set_default_value(op, prop);
        if (op->init) {
            op->init(obj, op);
        }
    }

    op->allow_set = allow_set;
    return op;
}

ObjectProperty *
object_class_property_add_static(ObjectClass *oc, Property *prop,
                                 ObjectPropertyAllowSet allow_set)
{
    ObjectProperty *op;

    if (prop->info->create) {
        op = prop->info->create(oc, prop);
    } else {
        op = object_class_property_add(oc,
                                       prop->name, prop->info->name,
                                       static_prop_getter(prop->info),
                                       static_prop_setter(prop->info),
                                       prop->info->release,
                                       prop);
    }
    if (prop->set_default) {
        prop->info->set_default_value(op, prop);
    }
    if (prop->info->description) {
        object_class_property_set_description(oc, prop->name,
                                            prop->info->description);
    }

    op->allow_set = allow_set;
    return op;
}

void object_class_add_static_props(ObjectClass *oc, Property *props,
                                   ObjectPropertyAllowSet allow_set)
{
    Property *prop;

    for (prop = props; prop && prop->name; prop++) {
        object_class_property_add_static(oc, prop, allow_set);
    }
}
