/*
 * QOM field property types
 */
#ifndef QOM_PROPERTY_TYPES_H
#define QOM_PROPERTY_TYPES_H

#include "qom/field-property.h"
#include "qapi/qmp/qlit.h"

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
extern const PropertyInfo prop_info_arraylen;
extern const PropertyInfo prop_info_link;

#define DEFINE_PROP(_name, _state, _field, _prop, _type, ...) {  \
        .qdev_prop_name      = (_name),                          \
        .info      = &(_prop),                                   \
        .offset    = offsetof(_state, _field)                    \
            + type_check(_type, typeof_field(_state, _field)),   \
        .defval = QLIT_QNULL,                                    \
        /* Note that __VA_ARGS__ can still override .defval */   \
        __VA_ARGS__                                              \
        }

#define DEFINE_PROP_SIGNED(_name, _state, _field, _defval, _prop, _type) \
    DEFINE_PROP(_name, _state, _field, _prop, _type,                     \
                .defval = QLIT_QNUM_INT(_defval))

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
                .defval = QLIT_QBOOL(_defval))

#define DEFINE_PROP_UNSIGNED(_name, _state, _field, _defval, _prop, _type) \
    DEFINE_PROP(_name, _state, _field, _prop, _type,                       \
                .defval = QLIT_QNUM_UINT(_defval))

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
                .defval = QLIT_QBOOL(_defval))

/**
 * DEFINE_PROP_BOOL:
 * @_name: name of the property
 * @_state: name of the object state structure type
 * @_field: name of ``bool`` field in @_state
 * @_defval: default value of property
 */
#define DEFINE_PROP_BOOL(_name, _state, _field, _defval)     \
    DEFINE_PROP(_name, _state, _field, prop_info_bool, bool, \
                .defval = QLIT_QBOOL(_defval))

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
                .defval = QLIT_QNUM_UINT(0),                   \
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
 * DEFINE_PROP_END_OF_LIST: Mark end of property array
 *
 * This must be the last entry in #Property arrays when calling
 * object_class_add_static_props().
 */
#define DEFINE_PROP_END_OF_LIST()               \
    {}

/*
 * The PROP_* macros can be used as arguments for
 * object_class_property_add_field().  They will evaluate to a
 * pointer to a static variable.
 */

#define FIELD_PROP(def) \
    ({ static Property _p = def; &_p; })

#define PROP_SIGNED(...) \
    FIELD_PROP(DEFINE_PROP_SIGNED(NULL, __VA_ARGS__))
#define PROP_SIGNED_NODEFAULT(...) \
    FIELD_PROP(DEFINE_PROP_SIGNED_NODEFAULT(NULL, __VA_ARGS__))
#define PROP_BIT(...) \
    FIELD_PROP(DEFINE_PROP_BIT(NULL, __VA_ARGS__))
#define PROP_UNSIGNED(...) \
    FIELD_PROP(DEFINE_PROP_UNSIGNED(NULL, __VA_ARGS__))
#define PROP_UNSIGNED_NODEFAULT(...) \
    FIELD_PROP(DEFINE_PROP_UNSIGNED_NODEFAULT(NULL, __VA_ARGS__))
#define PROP_BIT64(...) \
    FIELD_PROP(DEFINE_PROP_BIT64(NULL, __VA_ARGS__))
#define PROP_BOOL(...) \
    FIELD_PROP(DEFINE_PROP_BOOL(NULL, __VA_ARGS__))
#define PROP_ARRAY(...) \
    FIELD_PROP(DEFINE_PROP_ARRAY(NULL, __VA_ARGS__))
#define PROP_LINK(...) \
    FIELD_PROP(DEFINE_PROP_LINK(NULL, __VA_ARGS__))
#define PROP_UINT8(...) \
    FIELD_PROP(DEFINE_PROP_UINT8(NULL, __VA_ARGS__))
#define PROP_UINT16(...) \
    FIELD_PROP(DEFINE_PROP_UINT16(NULL, __VA_ARGS__))
#define PROP_UINT32(...) \
    FIELD_PROP(DEFINE_PROP_UINT32(NULL, __VA_ARGS__))
#define PROP_INT32(...) \
    FIELD_PROP(DEFINE_PROP_INT32(NULL, __VA_ARGS__))
#define PROP_UINT64(...) \
    FIELD_PROP(DEFINE_PROP_UINT64(NULL, __VA_ARGS__))
#define PROP_INT64(...) \
    FIELD_PROP(DEFINE_PROP_INT64(NULL, __VA_ARGS__))
#define PROP_SIZE(...) \
    FIELD_PROP(DEFINE_PROP_SIZE(NULL, __VA_ARGS__))
#define PROP_STRING(...) \
    FIELD_PROP(DEFINE_PROP_STRING(NULL, __VA_ARGS__))
#define PROP_ON_OFF_AUTO(...) \
    FIELD_PROP(DEFINE_PROP_ON_OFF_AUTO(NULL, __VA_ARGS__))
#define PROP_SIZE32(...) \
    FIELD_PROP(DEFINE_PROP_SIZE32(NULL, __VA_ARGS__))
#define PROP_UUID(...) \
    FIELD_PROP(DEFINE_PROP_UUID(NULL, __VA_ARGS__))
#define PROP_UUID_NODEFAULT(...) \
    FIELD_PROP(DEFINE_PROP_UUID_NODEFAULT(NULL, __VA_ARGS__))
#define PROP_END_OF_LIST(...) \
    FIELD_PROP(DEFINE_PROP_END_OF_LIST(NULL, __VA_ARGS__))

#endif
