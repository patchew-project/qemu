/*
 * QOM field property API
 */
#ifndef QOM_FIELD_PROPERTY_H
#define QOM_FIELD_PROPERTY_H

#include "qom/object.h"
#include "qapi/util.h"
#include "qapi/qmp/qlit.h"

/**
 * struct Property: definition of a field property
 *
 * Field properties are used to read and write fields in object
 * instance structs.  Field properties are declared using
 * ``DEFINE_PROP_<type>`` (for property arrays registered using
 * device_class_set_props()), or using ``PROP_<type>`` (for
 * object_class_property_add_field() calls).
 */
struct Property {
    /* private: */
    /**
     * @qdev_prop_name: qdev property name
     *
     * qdev_prop_name is used only by TYPE_DEVICE code
     * (device_class_set_props(), qdev_class_add_property(), and
     * others).
     */
    const char   *qdev_prop_name;
    const PropertyInfo *info;
    ptrdiff_t    offset;
    uint8_t      bitnr;
    /** @defval: If not QNull, the default value for the property */
    QLitObject defval;
    /* private: */
    int          arrayoffset;
    const PropertyInfo *arrayinfo;
    int          arrayfieldsize;
    const char   *link_type;
};

/**
 * struct PropertyInfo: information on a specific QOM property type
 */
struct PropertyInfo {
    /** @name: property type name */
    const char *name;
    /** @description: Description for help text */
    const char *description;
    /** @enum_table: Used by field_prop_get_enum() and field_prop_set_enum() */
    const QEnumLookup *enum_table;
    /** @print: String formatting function, for the human monitor */
    int (*print)(Object *obj, Property *prop, char *dest, size_t len);
    /**
     * @set_default_value: Optional callback for initializing the default value
     *
     * Most property types don't need to set this, as by default
     * object_property_set_default() is called with the value at
     * Property.defval.
     */
    void (*set_default_value)(ObjectProperty *op, const Property *prop);
    /** @create: Optional callback for creation of property */
    ObjectProperty *(*create)(ObjectClass *oc, const char *name,
                              Property *prop);
    /**
     * @get: Property getter.  The opaque parameter will point to
     *        the &Property struct for the property.
     */
    ObjectPropertyAccessor *get;
    /**
     * @set: Property setter.  The opaque parameter will point to
     *        the &Property struct for the property.
     */
    ObjectPropertyAccessor *set;
    /**
     * @release: Optional release function, called when the object
     * is destroyed
     */
    ObjectPropertyRelease *release;
};

/**
 * object_class_property_add_field: Add a field property to object class
 * @oc: object class
 * @name: property name
 * @prop: property definition
 * @allow_set: check function called when property is set
 *
 * Add a field property to an object class.  A field property is
 * a property that will change a field at a specific offset of the
 * object instance struct.
 *
 * *@prop must exist for the life time of @oc.
 *
 * @allow_set should not be NULL.  If the property can always be
 * set, `prop_allow_set_always` can be used.  If the property can
 * never be set, `prop_allow_set_never` can be used.
 */
ObjectProperty *
object_class_property_add_field(ObjectClass *oc, const char *name,
                                Property *prop,
                                ObjectPropertyAllowSet allow_set);

void *object_field_prop_ptr(Object *obj, Property *prop);

#endif
