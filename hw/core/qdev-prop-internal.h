/*
 * qdev property parsing
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_CORE_QDEV_PROP_INTERNAL_H
#define HW_CORE_QDEV_PROP_INTERNAL_H

void qdev_propinfo_get_enum(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp);
void qdev_propinfo_set_enum(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp);

void qdev_propinfo_set_default_value_enum(ObjectProperty *op,
                                          const Property *prop);
void qdev_propinfo_set_default_value_int(ObjectProperty *op,
                                         const Property *prop);
void qdev_propinfo_set_default_value_uint(ObjectProperty *op,
                                          const Property *prop);

void qdev_propinfo_get_int32(Object *obj, Visitor *v, const char *name,
                             void *opaque, Error **errp);
void qdev_propinfo_get_size32(Object *obj, Visitor *v, const char *name,
                              void *opaque, Error **errp);

/**
 * object_property_add_field: Add a field property to an object instance
 * @obj: object instance
 * @name: property name
 * @prop: property definition
 *
 * This function should not be used in new code.  Please add class properties
 * instead, using object_class_add_field().
 */
ObjectProperty *
object_property_add_field(Object *obj, const char *name,
                          Property *prop);

/**
 * object_class_property_add_field_static: Add a field property to object class
 * @oc: object class
 * @name: property name
 * @prop: property definition
 *
 * Add a field property to an object class.  A field property is
 * a property that will change a field at a specific offset of the
 * object instance struct.
 *
 * *@prop must have static life time.
 */
ObjectProperty *
object_class_property_add_field_static(ObjectClass *oc, const char *name,
                                       Property *prop);

/**
 * object_class_add_field_properties: Add field properties from array to a class
 * @oc: object class
 * @props: array of property definitions
 *
 * Register an array of field properties to a class, using
 * object_class_property_add_field_static() for each array element.
 *
 * The array at @props must end with DEFINE_PROP_END_OF_LIST(), and
 * must have static life time.
 */
void object_class_add_field_properties(ObjectClass *oc, Property *props);

#endif
