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

void get_prop_array(Object *obj, Visitor *v, const char *name,
                    void *opaque, Error **errp);
void set_prop_array(Object *obj, Visitor *v, const char *name,
                    void *opaque, Error **errp);
void release_prop_array(Object *obj, const char *name, void *opaque);
void default_prop_array(ObjectProperty *op, const Property *prop);

#define DEFINE_PROP_ARRAY_INFO(_prop_info, _c_type, _type_info) \
    {                                                           \
        .type = "list",                                         \
        .qapi_type = (_type_info),                              \
        .element_info = &(_prop_info),                          \
        .element_size = sizeof(_c_type),                        \
        .get = get_prop_array,                                  \
        .set = set_prop_array,                                  \
        .release = release_prop_array,                          \
        .set_default_value = default_prop_array,                \
    }

#endif
