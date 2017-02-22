/*
 * QEMU Object Model - QObject wrappers
 *
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_QOM_QOBJECT_H
#define QEMU_QOM_QOBJECT_H

#include "qom/object.h"

/*
 * object_property_get_qobject:
 * @obj: the object
 * @name: the name of the property
 * @errp: returns an error if this function fails
 *
 * Returns: the value of the property, converted to QObject, or NULL if
 * an error occurs.
 */
struct QObject *object_property_get_qobject(Object *obj, const char *name,
                                            struct Error **errp);

/**
 * object_property_set_qobject:
 * @obj: the object
 * @ret: The value that will be written to the property
 * @name: the name of the property
 * @errp: returns an error if this function fails
 *
 * Writes a property to a object.
 */
void object_property_set_qobject(Object *obj, struct QObject *qobj,
                                 const char *name, struct Error **errp);

/**
 * object_property_get_ptr:
 * @obj: the object
 * @name: the name of the property
 * @visit_type: the visitor function for the returned object
 * @errp: returns an error if this function fails
 *
 * Return: the value of an object's property, unmarshaled into a C object
 * through a QAPI type visitor, or NULL if an error occurs.
 */
void *object_property_get_ptr(Object *obj, const char *name,
                              void (*visit_type)(Visitor *, const char *,
                                                 void **, Error **),
                              Error **errp);

/**
 * OBJECT_PROPERTY_GET_PTR:
 * @obj: the object
 * @name: the name of the property
 * @type: the name of the C struct type that is returned
 * @errp: returns an error if this function fails
 *
 * Return: the value of an object's property, unmarshaled into a C object
 * through a QAPI type visitor, or NULL if an error occurs.
 */
#define OBJECT_PROPERTY_GET_PTR(obj, name, type, errp)                      \
    ((type *)                                                               \
     object_property_get_ptr(obj, name,                                     \
                             (void (*)(Visitor *, const char *, void**,     \
                                       Error **))visit_type_ ## type,       \
                             errp))

/**
 * object_property_set_ptr:
 * @obj: the object
 * @ptr: The C struct that will be written to the property
 * @name: the name of the property
 * @visit_type: the visitor function for @ptr's type
 * @errp: returns an error if this function fails
 *
 * Sets an object's property to a C object's value, using a QAPI
 * type visitor to marshal the C struct into the property value.
 */
void object_property_set_ptr(Object *obj, void *ptr, const char *name,
                             void (*visit_type)(Visitor *, const char *,
                                                void **, Error **),
                             Error **errp);

/**
 * OBJECT_PROPERTY_SET_PTR:
 * @obj: the object
 * @ptr: The C struct that will be written to the property
 * @name: the name of the property
 * @type: the name of the C struct type pointed to by @ptr
 * @errp: returns an error if this function fails
 *
 * Sets an object's property to a C object's value, using a QAPI
 * type visitor to marshal the C struct into the property value.
 */
#define OBJECT_PROPERTY_SET_PTR(obj, ptr, name, type, errp)                 \
    object_property_set_ptr(obj, ptr + type_check(type, typeof(*ptr)),      \
                            name,                                           \
                            (void (*)(Visitor *, const char *, void**,      \
                                      Error **))visit_type_ ## type,        \
                            errp)

#endif
