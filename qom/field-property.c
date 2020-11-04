/*
 * QOM field property API implementation
 */
#include "qemu/osdep.h"
#include "qom/field-property.h"
#include "qom/field-property-internal.h"

void *object_field_prop_ptr(Object *obj, Property *prop, size_t expected_size)
{
    void *ptr = obj;
    ptr += prop->offset;
    assert(prop->size == expected_size);
    return ptr;
}

static void field_prop_get(Object *obj, Visitor *v, const char *name,
                           void *opaque, Error **errp)
{
    Property *prop = opaque;
    return prop->info->get(obj, v, name, opaque, errp);
}

/**
 * field_prop_getter: Return getter function to be used for property
 *
 * Return value can be NULL if @info has no getter function.
 */
static ObjectPropertyAccessor *field_prop_getter(const PropertyInfo *info)
{
    return info->get ? field_prop_get : NULL;
}

static void field_prop_set(Object *obj, Visitor *v, const char *name,
                           void *opaque, Error **errp)
{
    Property *prop = opaque;

    return prop->info->set(obj, v, name, opaque, errp);
}

/**
 * field_prop_setter: Return setter function to be used for property
 *
 * Return value can be NULL if @info has not setter function.
 */
static ObjectPropertyAccessor *field_prop_setter(const PropertyInfo *info)
{
    return info->set ? field_prop_set : NULL;
}

static void field_prop_release(Object *obj, const char *name, void *opaque)
{
    Property *prop = opaque;
    if (prop->info->release) {
        prop->info->release(obj, name, prop);
    }
}


ObjectProperty *
object_property_add_field(Object *obj, const char *name, Property *prop,
                          ObjectPropertyAllowSet allow_set)
{
    ObjectProperty *op;

    assert(allow_set);
    assert(!prop->info->create);

    op = object_property_add(obj, name, prop->info->name,
                             field_prop_getter(prop->info),
                             field_prop_setter(prop->info),
                             field_prop_release,
                             prop);

    object_property_set_description(obj, name,
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
object_class_property_add_field(ObjectClass *oc, const char *name,
                                Property *prop,
                                ObjectPropertyAllowSet allow_set)
{
    ObjectProperty *op;

    assert(allow_set);

    if (prop->info->create) {
        op = prop->info->create(oc, name, prop);
    } else {
        op = object_class_property_add(oc,
                                       name, prop->info->name,
                                       field_prop_getter(prop->info),
                                       field_prop_setter(prop->info),
                                       field_prop_release,
                                       prop);
    }
    if (prop->set_default) {
        prop->info->set_default_value(op, prop);
    }
    if (prop->info->description) {
        object_class_property_set_description(oc, name,
                                              prop->info->description);
    }

    op->allow_set = allow_set;
    return op;
}
