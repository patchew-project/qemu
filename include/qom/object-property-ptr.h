/*
 * Object property helpers to operate on a pointer.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_OBJECT_PROPERTY_PTR_H
#define QEMU_OBJECT_PROPERTY_PTR_H

typedef enum {
    /* Automatically add a getter to the property */
    OBJ_PROP_FLAG_READ = 1 << 0,
    /* Automatically add a setter to the property */
    OBJ_PROP_FLAG_WRITE = 1 << 1,
    /* Automatically add a getter and a setter to the property */
    OBJ_PROP_FLAG_READWRITE = (OBJ_PROP_FLAG_READ | OBJ_PROP_FLAG_WRITE),
} ObjectPropertyFlags;

/**
 * object_property_add_bool_ptr:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @v: pointer to value
 * @flags: bitwise-or'd ObjectPropertyFlags
 *
 * Add a property of type 'bool' to the object.
 *
 * Returns: The newly added property on success, or %NULL on failure.
 */
ObjectProperty *
object_property_add_bool_ptr(Object *obj, const char *name, const bool *v,
                             ObjectPropertyFlags flags);

/**
 * object_property_add_uint8_ptr:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @v: pointer to value
 * @flags: bitwise-or'd ObjectPropertyFlags
 *
 * Add an integer property in memory.  This function will add a
 * property of type 'uint8'.
 *
 * Returns: The newly added property on success, or %NULL on failure.
 */
ObjectProperty *object_property_add_uint8_ptr(Object *obj, const char *name,
                                              const uint8_t *v,
                                              ObjectPropertyFlags flags);

ObjectProperty *object_class_property_add_uint8_ptr(ObjectClass *klass,
                                         const char *name,
                                         const uint8_t *v,
                                         ObjectPropertyFlags flags);

/**
 * object_property_add_uint16_ptr:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @v: pointer to value
 * @flags: bitwise-or'd ObjectPropertyFlags
 *
 * Add an integer property in memory.  This function will add a
 * property of type 'uint16'.
 *
 * Returns: The newly added property on success, or %NULL on failure.
 */
ObjectProperty *object_property_add_uint16_ptr(Object *obj, const char *name,
                                    const uint16_t *v,
                                    ObjectPropertyFlags flags);

ObjectProperty *object_class_property_add_uint16_ptr(ObjectClass *klass,
                                          const char *name,
                                          const uint16_t *v,
                                          ObjectPropertyFlags flags);

/**
 * object_property_add_uint32_ptr:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @v: pointer to value
 * @flags: bitwise-or'd ObjectPropertyFlags
 *
 * Add an integer property in memory.  This function will add a
 * property of type 'uint32'.
 *
 * Returns: The newly added property on success, or %NULL on failure.
 */
ObjectProperty *object_property_add_uint32_ptr(Object *obj, const char *name,
                                    const uint32_t *v,
                                    ObjectPropertyFlags flags);

ObjectProperty *object_class_property_add_uint32_ptr(ObjectClass *klass,
                                          const char *name,
                                          const uint32_t *v,
                                          ObjectPropertyFlags flags);

/**
 * object_property_add_uint64_ptr:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @v: pointer to value
 * @flags: bitwise-or'd ObjectPropertyFlags
 *
 * Add an integer property in memory.  This function will add a
 * property of type 'uint64'.
 *
 * Returns: The newly added property on success, or %NULL on failure.
 */
ObjectProperty *object_property_add_uint64_ptr(Object *obj, const char *name,
                                    const uint64_t *v,
                                    ObjectPropertyFlags flags);

ObjectProperty *object_class_property_add_uint64_ptr(ObjectClass *klass,
                                          const char *name,
                                          const uint64_t *v,
                                          ObjectPropertyFlags flags);

/**
 * object_property_add_size_ptr:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @v: pointer to value
 * @flags: bitwise-or'd ObjectPropertyFlags
 *
 * Add a property of type 'size' to the object.
 *
 * Returns: The newly added property on success, or %NULL on failure.
 */
ObjectProperty *
object_property_add_size_ptr(Object *obj, const char *name,
                             const uint64_t *v,
                             ObjectPropertyFlags flags);

#endif
