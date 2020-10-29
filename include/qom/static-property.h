/*
 * QOM static property API
 */
#ifndef QOM_STATIC_PROPERTY_H
#define QOM_STATIC_PROPERTY_H

#include "qom/object.h"
#include "qapi/util.h"

/**
 * Property:
 * @set_default: true if the default value should be set from @defval,
 *    in which case @info->set_default_value must not be NULL
 *    (if false then no default value is set by the property system
 *     and the field retains whatever value it was given by instance_init).
 * @defval: default value for the property. This is used only if @set_default
 *     is true.
 */
struct Property {
    const char   *name;
    const PropertyInfo *info;
    ptrdiff_t    offset;
    uint8_t      bitnr;
    bool         set_default;
    union {
        int64_t i;
        uint64_t u;
    } defval;
    int          arrayoffset;
    const PropertyInfo *arrayinfo;
    int          arrayfieldsize;
    const char   *link_type;
};

struct PropertyInfo {
    const char *name;
    const char *description;
    const QEnumLookup *enum_table;
    int (*print)(Object *obj, Property *prop, char *dest, size_t len);
    void (*set_default_value)(ObjectProperty *op, const Property *prop);
    ObjectProperty *(*create)(ObjectClass *oc, Property *prop);
    ObjectPropertyAccessor *get;
    ObjectPropertyAccessor *set;
    ObjectPropertyRelease *release;
};

/**
 * object_class_property_add_static: Add a static property to object class
 * @oc: object class
 * @prop: property definition
 * @allow_set: optional check function
 *
 * Add a property to an object class based on the property definition
 * at @prop.
 *
 * If @allow_set is NULL, the property will always be allowed to be set.
 *
 * The property definition at @prop should be defined using the
 * ``DEFINE_PROP`` family of macros.  *@prop must exist for the
 * life time of @oc.
 */
ObjectProperty *
object_class_property_add_static(ObjectClass *oc, Property *prop,
                                 ObjectPropertyAllowSet allow_set);

/**
 * object_class_add_static_props: Add multiple static properties to object class
 * @oc: object class
 * @props: property definition array, terminated by DEFINED_PROP_END_OF_LIST()
 * @allow_set: optional check function
 *
 * Add properties from @props using object_class_property_add_static()
 */
void object_class_add_static_props(ObjectClass *oc, Property *props,
                                   ObjectPropertyAllowSet allow_set);

void *object_static_prop_ptr(Object *obj, Property *prop);

#endif
