/*
 * QOM static property API
 */
#ifndef QOM_STATIC_PROPERTY_H
#define QOM_STATIC_PROPERTY_H

#include "qom/object.h"
#include "qapi/util.h"

/**
 * struct Property: Definition of a static Property
 *
 * #Property structs should be always initialized using the
 * ``DEFINE_PROP`` family of macros.
 */
struct Property {
    /* private: */
    const char   *name;
    const PropertyInfo *info;
    ptrdiff_t    offset;
    uint8_t      bitnr;
    /**
     *  @set_default: true if the default value should be set
     *                from @defval, in which case
     *                @info->set_default_value must not be NULL
     *                (if false then no default value is set by
     *                the property system and the field retains
     *                whatever value it was given by
     *                instance_init).
     */
    bool         set_default;
    /**
     * @defval: default value for the property. This is used only
     *          if @set_default is true.
     */
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

extern const PropertyInfo prop_info_bit;
extern const PropertyInfo prop_info_bit64;
extern const PropertyInfo prop_info_bool;
extern const PropertyInfo prop_info_enum;
extern const PropertyInfo prop_info_uint8;
extern const PropertyInfo prop_info_uint16;
extern const PropertyInfo prop_info_uint32;
extern const PropertyInfo prop_info_int32;
extern const PropertyInfo prop_info_uint64;
extern const PropertyInfo prop_info_int64;
extern const PropertyInfo prop_info_size;
extern const PropertyInfo prop_info_string;
extern const PropertyInfo prop_info_on_off_auto;
extern const PropertyInfo prop_info_size32;
extern const PropertyInfo prop_info_uuid;
extern const PropertyInfo prop_info_arraylen;
extern const PropertyInfo prop_info_link;

#define DEFINE_PROP(_name, _state, _field, _prop, _type, ...) {  \
        .name      = (_name),                                    \
        .info      = &(_prop),                                   \
        .offset    = offsetof(_state, _field)                    \
            + type_check(_type, typeof_field(_state, _field)),   \
        __VA_ARGS__                                              \
        }

#define DEFINE_PROP_SIGNED(_name, _state, _field, _defval, _prop, _type) \
    DEFINE_PROP(_name, _state, _field, _prop, _type,                     \
                .set_default = true,                                     \
                .defval.i    = (_type)_defval)

#define DEFINE_PROP_SIGNED_NODEFAULT(_name, _state, _field, _prop, _type) \
    DEFINE_PROP(_name, _state, _field, _prop, _type)

/**
 * DEFINE_PROP_BIT: Define bit property in uint32_t field
 * @_name: name of the property
 * @_state: name of the object state structure type
 * @_field: name of ``uint32_t`` field in @_state
 * @_bit: bit offset in @_field
 * @_defval: default value for bit
 */
#define DEFINE_PROP_BIT(_name, _state, _field, _bit, _defval)   \
    DEFINE_PROP(_name, _state, _field, prop_info_bit, uint32_t, \
                .bitnr       = (_bit),                          \
                .set_default = true,                            \
                .defval.u    = (bool)_defval)

#define DEFINE_PROP_UNSIGNED(_name, _state, _field, _defval, _prop, _type) \
    DEFINE_PROP(_name, _state, _field, _prop, _type,                       \
                .set_default = true,                                       \
                .defval.u  = (_type)_defval)

#define DEFINE_PROP_UNSIGNED_NODEFAULT(_name, _state, _field, _prop, _type) \
    DEFINE_PROP(_name, _state, _field, _prop, _type)

/**
 * DEFINE_PROP_BIT64: Define bit property in uint64_t field
 * @_name: name of the property
 * @_state: name of the object state structure type
 * @_field: name of ``uint64_t`` field in @_state
 * @_bit: bit offset in @_field
 * @_defval: default value for bit
 */
#define DEFINE_PROP_BIT64(_name, _state, _field, _bit, _defval)   \
    DEFINE_PROP(_name, _state, _field, prop_info_bit64, uint64_t, \
                .bitnr    = (_bit),                               \
                .set_default = true,                              \
                .defval.u  = (bool)_defval)

/**
 * DEFINE_PROP_BOOL:
 * @_name: name of the property
 * @_state: name of the object state structure type
 * @_field: name of ``bool`` field in @_state
 * @_defval: default value of property
 */
#define DEFINE_PROP_BOOL(_name, _state, _field, _defval)     \
    DEFINE_PROP(_name, _state, _field, prop_info_bool, bool, \
                .set_default = true,                         \
                .defval.u    = (bool)_defval)

#define PROP_ARRAY_LEN_PREFIX "len-"

/**
 * DEFINE_PROP_ARRAY:
 * @_name: name of the array
 * @_state: name of the device state structure type
 * @_field: uint32_t field in @_state to hold the array length
 * @_arrayfield: field in @_state (of type ``_arraytype *``) which
 *               will point to the array
 * @_arrayprop: #PropertyInfo variable defining property type of
 *              array elements
 * @_arraytype: C type of the array elements
 *
 * Define device properties for a variable-length array _name.  A
 * static property "len-arrayname" is defined. When the device creator
 * sets this property to the desired length of array, further dynamic
 * properties "arrayname[0]", "arrayname[1]", ...  are defined so the
 * device creator can set the array element values. Setting the
 * "len-arrayname" property more than once is an error.
 *
 * When the array length is set, the @_field member of the device
 * struct is set to the array length, and @_arrayfield is set to point
 * to (zero-initialised) memory allocated for the array.  For a zero
 * length array, @_field will be set to 0 and @_arrayfield to NULL.
 * It is the responsibility of the device deinit code to free the
 * @_arrayfield memory.
 */
#define DEFINE_PROP_ARRAY(_name, _state, _field,               \
                          _arrayfield, _arrayprop, _arraytype) \
    DEFINE_PROP((PROP_ARRAY_LEN_PREFIX _name),                 \
                _state, _field, prop_info_arraylen, uint32_t,  \
                .set_default = true,                           \
                .defval.u = 0,                                 \
                .arrayinfo = &(_arrayprop),                    \
                .arrayfieldsize = sizeof(_arraytype),          \
                .arrayoffset = offsetof(_state, _arrayfield))

/**
 * DEFINE_PROP_LINK: Define object link property
 * @_name: name of the property
 * @_state: name of the object state structure type
 * @_field: name of field in @_state holding the property value
 * @_type: QOM type name of link target
 * @_ptr_type: Type of field @_field in struct @_state
 */
#define DEFINE_PROP_LINK(_name, _state, _field, _type, _ptr_type)     \
    DEFINE_PROP(_name, _state, _field, prop_info_link, _ptr_type,     \
                .link_type  = _type)

/**
 * DEFINE_PROP_UINT8: Define uint8 property
 * @_n: name of the property
 * @_s: name of the object state structure type
 * @_f: name of ``uint8_t`` field in @_s
 * @_d: default value of property
 */
#define DEFINE_PROP_UINT8(_n, _s, _f, _d)                       \
    DEFINE_PROP_UNSIGNED(_n, _s, _f, _d, prop_info_uint8, uint8_t)
/**
 * DEFINE_PROP_UINT16: Define uint16 property
 * @_n: name of the property
 * @_s: name of the object state structure type
 * @_f: name of ``uint16_t`` field in @_s
 * @_d: default value of property
 */
#define DEFINE_PROP_UINT16(_n, _s, _f, _d)                      \
    DEFINE_PROP_UNSIGNED(_n, _s, _f, _d, prop_info_uint16, uint16_t)
/**
 * DEFINE_PROP_UINT32: Define uint32 property
 * @_n: name of the property
 * @_s: name of the object state structure type
 * @_f: name of ``uint32_t`` field in @_s
 * @_d: default value of property
 */
#define DEFINE_PROP_UINT32(_n, _s, _f, _d)                      \
    DEFINE_PROP_UNSIGNED(_n, _s, _f, _d, prop_info_uint32, uint32_t)
/**
 * DEFINE_PROP_INT32: Define int32 property
 * @_n: name of the property
 * @_s: name of the object state structure type
 * @_f: name of ``int32_t`` field in @_s
 * @_d: default value of property
 */
#define DEFINE_PROP_INT32(_n, _s, _f, _d)                      \
    DEFINE_PROP_SIGNED(_n, _s, _f, _d, prop_info_int32, int32_t)
/**
 * DEFINE_PROP_UINT64: Define uint64 property
 * @_n: name of the property
 * @_s: name of the object state structure type
 * @_f: name of ``uint64_t`` field in @_s
 * @_d: default value of property
 */
#define DEFINE_PROP_UINT64(_n, _s, _f, _d)                      \
    DEFINE_PROP_UNSIGNED(_n, _s, _f, _d, prop_info_uint64, uint64_t)
/**
 * DEFINE_PROP_INT64: Define int64 property
 * @_n: name of the property
 * @_s: name of the object state structure type
 * @_f: name of ``int64_t`` field in @_s
 * @_d: default value of property
 */
#define DEFINE_PROP_INT64(_n, _s, _f, _d)                      \
    DEFINE_PROP_SIGNED(_n, _s, _f, _d, prop_info_int64, int64_t)
/**
 * DEFINE_PROP_SIZE: Define uint64 property
 * @_n: name of the property
 * @_s: name of the object state structure type
 * @_f: name of ``uint64_t`` field in @_s
 * @_d: default value of property
 */
#define DEFINE_PROP_SIZE(_n, _s, _f, _d)                       \
    DEFINE_PROP_UNSIGNED(_n, _s, _f, _d, prop_info_size, uint64_t)
/**
 * DEFINE_PROP_STRING:
 * @_n: name of the property
 * @_s: name of the object state structure type
 * @_f: name of ``char *`` field in @_state
 */
#define DEFINE_PROP_STRING(_n, _s, _f)             \
    DEFINE_PROP(_n, _s, _f, prop_info_string, char*)
/**
 * DEFINE_PROP_ON_OFF_AUTO: Define OnOffAuto property
 * @_n: name of the property
 * @_s: name of the object state structure type
 * @_f: name of ``OnOffAuto`` field in @_s
 * @_d: default value of property
 */
#define DEFINE_PROP_ON_OFF_AUTO(_n, _s, _f, _d) \
    DEFINE_PROP_SIGNED(_n, _s, _f, _d, prop_info_on_off_auto, OnOffAuto)
/**
 * DEFINE_PROP_SIZE32: Define uint32 property
 * @_n: name of the property
 * @_s: name of the object state structure type
 * @_f: name of ``uint32_t`` field in @_s
 * @_d: default value of property
 */
#define DEFINE_PROP_SIZE32(_n, _s, _f, _d)                       \
    DEFINE_PROP_UNSIGNED(_n, _s, _f, _d, prop_info_size32, uint32_t)
/**
 * DEFINE_PROP_UUID: Define UUID property
 * @_name: name of the property
 * @_state: name of the object state structure type
 * @_field: name of field in @_state holding the property value
 *
 * The @_f field in struct @_s must be of type ``QemuUUID``.
 * The default value of the property will be "auto".
 */
#define DEFINE_PROP_UUID(_name, _state, _field)                      \
    DEFINE_PROP(_name, _state, _field, prop_info_uuid, QemuUUID,     \
                .set_default = true)
/**
 * DEFINE_PROP_UUID_NODEFAULT: Define UUID property with no default
 * @_name: name of the property
 * @_state: name of the object state structure type
 * @_field: name of field in @_state holding the property value
 *
 * The @_f field in struct @_s must be of type ``QemuUUID``.
 * No default value will be set for the property.
 */
#define DEFINE_PROP_UUID_NODEFAULT(_name, _state, _field) \
    DEFINE_PROP(_name, _state, _field, prop_info_uuid, QemuUUID)

/**
 * DEFINE_PROP_END_OF_LIST: Mark end of property array
 *
 * This must be the last entry in #Property arrays when calling
 * object_class_add_static_props().
 */
#define DEFINE_PROP_END_OF_LIST()               \
    {}

#endif
