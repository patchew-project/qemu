/*
 * QOM field property API implementation
 */
#include "qemu/osdep.h"
#include "qom/field-property.h"
#include "qom/field-property-internal.h"

void *object_field_prop_ptr(Object *obj, Property *prop)
{
    void *ptr = obj;
    ptr += prop->offset;
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

/*
 * Property release callback for dynamically-created properties:
 * We call the underlying element's property release hook, and
 * then free the memory we allocated when we added the property.
 */
static void static_prop_release_dynamic_prop(Object *obj, const char *name,
                                             void *opaque)
{
    Property *prop = opaque;
    if (prop->info->release) {
        prop->info->release(obj, name, opaque);
    }
    g_free(prop);
}

ObjectProperty *
object_property_add_field(Object *obj, const char *name,
                          Property *prop,
                          ObjectPropertyAllowSet allow_set)
{
    ObjectProperty *op;
    Property *newprop = g_new0(Property, 1);

    assert(allow_set);
    assert(!prop->info->create);

    *newprop = *prop;
    op = object_property_add(obj, name, newprop->info->name,
                             field_prop_getter(newprop->info),
                             field_prop_setter(newprop->info),
                             static_prop_release_dynamic_prop,
                             newprop);

    object_property_set_description(obj, name,
                                    newprop->info->description);

    if (newprop->set_default) {
        newprop->info->set_default_value(op, newprop);
        if (op->init) {
            op->init(obj, op);
        }
    }

    op->allow_set = allow_set;
    return op;
}

ObjectProperty *
object_class_property_add_field_static(ObjectClass *oc, const char *name,
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
                                       prop->info->release,
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

ObjectProperty *
object_class_property_add_field(ObjectClass *oc, const char *name,
                                Property prop,
                                ObjectPropertyAllowSet allow_set)
{
    /*
     * QOM classes and class properties are never deallocated, so we don't
     * have a corresponding release function that will free newprop.
     */
    Property *newprop = g_new0(Property, 1);
    *newprop = prop;
    return object_class_property_add_field_static(oc, name, newprop, allow_set);
}

void object_class_add_field_properties(ObjectClass *oc, Property *props,
                                       ObjectPropertyAllowSet allow_set)
{
    Property *prop;

    for (prop = props; prop && prop->name_template; prop++) {
        object_class_property_add_field_static(oc, prop->name_template, prop,
                                               allow_set);
    }
}
