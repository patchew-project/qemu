/*
 * qdev property parsing
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_CORE_QDEV_PROP_INTERNAL_H
#define HW_CORE_QDEV_PROP_INTERNAL_H

void object_propinfo_get_enum(Object *obj, Visitor *v, const char *name,
                              void *opaque, Error **errp);
void object_propinfo_set_enum(Object *obj, Visitor *v, const char *name,
                              void *opaque, Error **errp);

void object_propinfo_set_default_value_enum(ObjectProperty *op,
                                            const Property *prop);
void object_propinfo_set_default_value_int(ObjectProperty *op,
                                           const Property *prop);
void object_propinfo_set_default_value_uint(ObjectProperty *op,
                                            const Property *prop);

void object_propinfo_get_int32(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp);
void object_propinfo_get_size32(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp);

/**
 * object_property_add_static: Add a static property to an object instance
 * @obj: object instance
 * @prop: property definition
 * @allow_set: optional check function
 *
 * This function should not be used in new code.  Please add class properties
 * instead, using object_class_add_static_props().
 */
ObjectProperty *
object_property_add_static(Object *obj, Property *prop,
                           ObjectPropertyAllowSet allow_set);

#endif
