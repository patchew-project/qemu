/*
 * Object property helpers to operate on a pointer.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qom/object.h"
#include "qapi/visitor.h"

static void property_get_bool_ptr(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    bool value = *(bool *)opaque;
    visit_type_bool(v, name, &value, errp);
}

static void property_set_bool_ptr(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    bool *field = opaque;
    bool value;

    if (!visit_type_bool(v, name, &value, errp)) {
        return;
    }

    *field = value;
}

static void property_get_uint8_ptr(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    uint8_t value = *(uint8_t *)opaque;
    visit_type_uint8(v, name, &value, errp);
}

static void property_set_uint8_ptr(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    uint8_t *field = opaque;
    uint8_t value;

    if (!visit_type_uint8(v, name, &value, errp)) {
        return;
    }

    *field = value;
}

static void property_get_uint16_ptr(Object *obj, Visitor *v, const char *name,
                                    void *opaque, Error **errp)
{
    uint16_t value = *(uint16_t *)opaque;
    visit_type_uint16(v, name, &value, errp);
}

static void property_set_uint16_ptr(Object *obj, Visitor *v, const char *name,
                                    void *opaque, Error **errp)
{
    uint16_t *field = opaque;
    uint16_t value;

    if (!visit_type_uint16(v, name, &value, errp)) {
        return;
    }

    *field = value;
}

static void property_get_uint32_ptr(Object *obj, Visitor *v, const char *name,
                                    void *opaque, Error **errp)
{
    uint32_t value = *(uint32_t *)opaque;
    visit_type_uint32(v, name, &value, errp);
}

static void property_set_uint32_ptr(Object *obj, Visitor *v, const char *name,
                                    void *opaque, Error **errp)
{
    uint32_t *field = opaque;
    uint32_t value;

    if (!visit_type_uint32(v, name, &value, errp)) {
        return;
    }

    *field = value;
}

static void property_get_uint64_ptr(Object *obj, Visitor *v, const char *name,
                                    void *opaque, Error **errp)
{
    uint64_t value = *(uint64_t *)opaque;
    visit_type_uint64(v, name, &value, errp);
}

static void property_set_uint64_ptr(Object *obj, Visitor *v, const char *name,
                                    void *opaque, Error **errp)
{
    uint64_t *field = opaque;
    uint64_t value;

    if (!visit_type_uint64(v, name, &value, errp)) {
        return;
    }

    *field = value;
}

ObjectProperty *
object_property_add_bool_ptr(Object *obj, const char *name,
                             const bool *v, ObjectPropertyFlags flags)
{
    ObjectPropertyAccessor *getter = NULL;
    ObjectPropertyAccessor *setter = NULL;

    if ((flags & OBJ_PROP_FLAG_READ) == OBJ_PROP_FLAG_READ) {
        getter = property_get_bool_ptr;
    }

    if ((flags & OBJ_PROP_FLAG_WRITE) == OBJ_PROP_FLAG_WRITE) {
        setter = property_set_bool_ptr;
    }

    return object_property_add(obj, name, "bool",
                               getter, setter, NULL, (void *)v);
}

ObjectProperty *
object_property_add_uint8_ptr(Object *obj, const char *name,
                              const uint8_t *v,
                              ObjectPropertyFlags flags)
{
    ObjectPropertyAccessor *getter = NULL;
    ObjectPropertyAccessor *setter = NULL;

    if ((flags & OBJ_PROP_FLAG_READ) == OBJ_PROP_FLAG_READ) {
        getter = property_get_uint8_ptr;
    }

    if ((flags & OBJ_PROP_FLAG_WRITE) == OBJ_PROP_FLAG_WRITE) {
        setter = property_set_uint8_ptr;
    }

    return object_property_add(obj, name, "uint8",
                               getter, setter, NULL, (void *)v);
}

ObjectProperty *
object_class_property_add_uint8_ptr(ObjectClass *klass, const char *name,
                                    const uint8_t *v,
                                    ObjectPropertyFlags flags)
{
    ObjectPropertyAccessor *getter = NULL;
    ObjectPropertyAccessor *setter = NULL;

    if ((flags & OBJ_PROP_FLAG_READ) == OBJ_PROP_FLAG_READ) {
        getter = property_get_uint8_ptr;
    }

    if ((flags & OBJ_PROP_FLAG_WRITE) == OBJ_PROP_FLAG_WRITE) {
        setter = property_set_uint8_ptr;
    }

    return object_class_property_add(klass, name, "uint8",
                                     getter, setter, NULL, (void *)v);
}

ObjectProperty *
object_property_add_uint16_ptr(Object *obj, const char *name,
                               const uint16_t *v,
                               ObjectPropertyFlags flags)
{
    ObjectPropertyAccessor *getter = NULL;
    ObjectPropertyAccessor *setter = NULL;

    if ((flags & OBJ_PROP_FLAG_READ) == OBJ_PROP_FLAG_READ) {
        getter = property_get_uint16_ptr;
    }

    if ((flags & OBJ_PROP_FLAG_WRITE) == OBJ_PROP_FLAG_WRITE) {
        setter = property_set_uint16_ptr;
    }

    return object_property_add(obj, name, "uint16",
                               getter, setter, NULL, (void *)v);
}

ObjectProperty *
object_class_property_add_uint16_ptr(ObjectClass *klass, const char *name,
                                     const uint16_t *v,
                                     ObjectPropertyFlags flags)
{
    ObjectPropertyAccessor *getter = NULL;
    ObjectPropertyAccessor *setter = NULL;

    if ((flags & OBJ_PROP_FLAG_READ) == OBJ_PROP_FLAG_READ) {
        getter = property_get_uint16_ptr;
    }

    if ((flags & OBJ_PROP_FLAG_WRITE) == OBJ_PROP_FLAG_WRITE) {
        setter = property_set_uint16_ptr;
    }

    return object_class_property_add(klass, name, "uint16",
                                     getter, setter, NULL, (void *)v);
}

ObjectProperty *
object_property_add_uint32_ptr(Object *obj, const char *name,
                               const uint32_t *v,
                               ObjectPropertyFlags flags)
{
    ObjectPropertyAccessor *getter = NULL;
    ObjectPropertyAccessor *setter = NULL;

    if ((flags & OBJ_PROP_FLAG_READ) == OBJ_PROP_FLAG_READ) {
        getter = property_get_uint32_ptr;
    }

    if ((flags & OBJ_PROP_FLAG_WRITE) == OBJ_PROP_FLAG_WRITE) {
        setter = property_set_uint32_ptr;
    }

    return object_property_add(obj, name, "uint32",
                               getter, setter, NULL, (void *)v);
}

ObjectProperty *
object_class_property_add_uint32_ptr(ObjectClass *klass, const char *name,
                                     const uint32_t *v,
                                     ObjectPropertyFlags flags)
{
    ObjectPropertyAccessor *getter = NULL;
    ObjectPropertyAccessor *setter = NULL;

    if ((flags & OBJ_PROP_FLAG_READ) == OBJ_PROP_FLAG_READ) {
        getter = property_get_uint32_ptr;
    }

    if ((flags & OBJ_PROP_FLAG_WRITE) == OBJ_PROP_FLAG_WRITE) {
        setter = property_set_uint32_ptr;
    }

    return object_class_property_add(klass, name, "uint32",
                                     getter, setter, NULL, (void *)v);
}

ObjectProperty *
object_property_add_uint64_ptr(Object *obj, const char *name,
                               const uint64_t *v,
                               ObjectPropertyFlags flags)
{
    ObjectPropertyAccessor *getter = NULL;
    ObjectPropertyAccessor *setter = NULL;

    if ((flags & OBJ_PROP_FLAG_READ) == OBJ_PROP_FLAG_READ) {
        getter = property_get_uint64_ptr;
    }

    if ((flags & OBJ_PROP_FLAG_WRITE) == OBJ_PROP_FLAG_WRITE) {
        setter = property_set_uint64_ptr;
    }

    return object_property_add(obj, name, "uint64",
                               getter, setter, NULL, (void *)v);
}

ObjectProperty *
object_class_property_add_uint64_ptr(ObjectClass *klass, const char *name,
                                     const uint64_t *v,
                                     ObjectPropertyFlags flags)
{
    ObjectPropertyAccessor *getter = NULL;
    ObjectPropertyAccessor *setter = NULL;

    if ((flags & OBJ_PROP_FLAG_READ) == OBJ_PROP_FLAG_READ) {
        getter = property_get_uint64_ptr;
    }

    if ((flags & OBJ_PROP_FLAG_WRITE) == OBJ_PROP_FLAG_WRITE) {
        setter = property_set_uint64_ptr;
    }

    return object_class_property_add(klass, name, "uint64",
                                     getter, setter, NULL, (void *)v);
}
