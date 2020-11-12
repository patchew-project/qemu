/*
 * QOM field property internal API (for implementing custom types)
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QOM_STATIC_PROPERTY_INTERNAL_H
#define QOM_STATIC_PROPERTY_INTERNAL_H

void field_prop_get_enum(Object *obj, Visitor *v, const char *name,
                         void *opaque, Error **errp);
void field_prop_set_enum(Object *obj, Visitor *v, const char *name,
                         void *opaque, Error **errp);

void field_prop_set_default_value_enum(ObjectProperty *op,
                                       const Property *prop);
void field_prop_set_default_value_int(ObjectProperty *op,
                                      const Property *prop);
void field_prop_set_default_value_uint(ObjectProperty *op,
                                       const Property *prop);

void field_prop_get_int32(Object *obj, Visitor *v, const char *name,
                          void *opaque, Error **errp);
void field_prop_get_size32(Object *obj, Visitor *v, const char *name,
                           void *opaque, Error **errp);

/**
 * object_property_add_field: Add a field property to an object instance
 * @obj: object instance
 * @name: property name
 * @prop: property definition
 * @allow_set: check function called when property is set
 *
 * This function should not be used in new code.  Please add class properties
 * instead, using object_class_add_field().
 */
ObjectProperty *
object_property_add_field(Object *obj, const char *name,
                          Property *prop,
                          ObjectPropertyAllowSet allow_set);

/**
 * object_class_property_add_field_static: Add a field property to object class
 * @oc: object class
 * @name: property name
 * @prop: property definition
 * @allow_set: check function called when property is set
 *
 * Add a field property to an object class.  A field property is
 * a property that will change a field at a specific offset of the
 * object instance struct.
 *
 * *@prop must have static life time.
 *
 * @allow_set should not be NULL.  If the property can always be
 * set, `prop_allow_set_always` can be used.  If the property can
 * never be set, `prop_allow_set_never` can be used.
 */
ObjectProperty *
object_class_property_add_field_static(ObjectClass *oc, const char *name,
                                       Property *prop,
                                       ObjectPropertyAllowSet allow_set);

/**
 * object_class_add_field_properties: Add field properties from array to a class
 * @oc: object class
 * @props: array of property definitions
 * @allow_set: check function called when property is set
 *
 * Register an array of field properties to a class, using
 * object_class_property_add_field_static() for each array element.
 *
 * The array at @props must end with DEFINE_PROP_END_OF_LIST(), and
 * must have static life time.
 */
void object_class_add_field_properties(ObjectClass *oc, Property *props,
                                       ObjectPropertyAllowSet allow_set);

#endif
