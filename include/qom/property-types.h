/*
 * QOM field property types
 */
#ifndef QOM_PROPERTY_TYPES_H
#define QOM_PROPERTY_TYPES_H

#include "qom/field-property.h"

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
        .name_template = (_name),                           \
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

#define DEFINE_PROP_BIT64(_name, _state, _field, _bit, _defval)   \
    DEFINE_PROP(_name, _state, _field, prop_info_bit64, uint64_t, \
                .bitnr    = (_bit),                               \
                .set_default = true,                              \
                .defval.u  = (bool)_defval)

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
 * @_arrayfield: field in @_state (of type '@_arraytype *') which
 *               will point to the array
 * @_arrayprop: PropertyInfo defining what property the array elements have
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

#define DEFINE_PROP_LINK(_name, _state, _field, _type, _ptr_type)     \
    DEFINE_PROP(_name, _state, _field, prop_info_link, _ptr_type,     \
                .link_type  = _type)

#define DEFINE_PROP_UINT8(_n, _s, _f, _d)                       \
    DEFINE_PROP_UNSIGNED(_n, _s, _f, _d, prop_info_uint8, uint8_t)
#define DEFINE_PROP_UINT16(_n, _s, _f, _d)                      \
    DEFINE_PROP_UNSIGNED(_n, _s, _f, _d, prop_info_uint16, uint16_t)
#define DEFINE_PROP_UINT32(_n, _s, _f, _d)                      \
    DEFINE_PROP_UNSIGNED(_n, _s, _f, _d, prop_info_uint32, uint32_t)
#define DEFINE_PROP_INT32(_n, _s, _f, _d)                      \
    DEFINE_PROP_SIGNED(_n, _s, _f, _d, prop_info_int32, int32_t)
#define DEFINE_PROP_UINT64(_n, _s, _f, _d)                      \
    DEFINE_PROP_UNSIGNED(_n, _s, _f, _d, prop_info_uint64, uint64_t)
#define DEFINE_PROP_INT64(_n, _s, _f, _d)                      \
    DEFINE_PROP_SIGNED(_n, _s, _f, _d, prop_info_int64, int64_t)
#define DEFINE_PROP_SIZE(_n, _s, _f, _d)                       \
    DEFINE_PROP_UNSIGNED(_n, _s, _f, _d, prop_info_size, uint64_t)
#define DEFINE_PROP_STRING(_n, _s, _f)             \
    DEFINE_PROP(_n, _s, _f, prop_info_string, char*)
#define DEFINE_PROP_ON_OFF_AUTO(_n, _s, _f, _d) \
    DEFINE_PROP_SIGNED(_n, _s, _f, _d, prop_info_on_off_auto, OnOffAuto)
#define DEFINE_PROP_SIZE32(_n, _s, _f, _d)                       \
    DEFINE_PROP_UNSIGNED(_n, _s, _f, _d, prop_info_size32, uint32_t)

#define DEFINE_PROP_END_OF_LIST()               \
    {}

#endif
