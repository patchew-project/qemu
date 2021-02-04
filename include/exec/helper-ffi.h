/*
 * Helper file for declaring TCG helper functions.
 * This one defines data structures private to tcg.c.
 */

#ifndef HELPER_FFI_H
#define HELPER_FFI_H 1

#include "exec/helper-head.h"

#define dh_ffitype_i32  &ffi_type_uint32
#define dh_ffitype_s32  &ffi_type_sint32
#define dh_ffitype_int  &ffi_type_sint
#define dh_ffitype_i64  &ffi_type_uint64
#define dh_ffitype_s64  &ffi_type_sint64
#define dh_ffitype_f16  &ffi_type_uint32
#define dh_ffitype_f32  &ffi_type_uint32
#define dh_ffitype_f64  &ffi_type_uint64
#ifdef TARGET_LONG_BITS
# if TARGET_LONG_BITS == 32
#  define dh_ffitype_tl &ffi_type_uint32
# else
#  define dh_ffitype_tl &ffi_type_uint64
# endif
#endif
#define dh_ffitype_ptr  &ffi_type_pointer
#define dh_ffitype_cptr &ffi_type_pointer
#define dh_ffitype_void &ffi_type_void
#define dh_ffitype_noreturn &ffi_type_void
#define dh_ffitype_env  &ffi_type_pointer
#define dh_ffitype(t) glue(dh_ffitype_, t)

#define DEF_HELPER_FLAGS_0(NAME, FLAGS, ret)    \
    static ffi_cif glue(cif_,NAME) = {          \
        .rtype = dh_ffitype(ret), .nargs = 0,   \
    };

#define DEF_HELPER_FLAGS_1(NAME, FLAGS, ret, t1)                        \
    static ffi_type *glue(cif_args_,NAME)[1] = { dh_ffitype(t1) };      \
    static ffi_cif glue(cif_,NAME) = {                                  \
        .rtype = dh_ffitype(ret), .nargs = 1,                           \
        .arg_types = glue(cif_args_,NAME),                              \
    };

#define DEF_HELPER_FLAGS_2(NAME, FLAGS, ret, t1, t2)    \
    static ffi_type *glue(cif_args_,NAME)[2] = {        \
        dh_ffitype(t1), dh_ffitype(t2)                  \
    };                                                  \
    static ffi_cif glue(cif_,NAME) = {                  \
        .rtype = dh_ffitype(ret), .nargs = 2,           \
        .arg_types = glue(cif_args_,NAME),              \
    };

#define DEF_HELPER_FLAGS_3(NAME, FLAGS, ret, t1, t2, t3)        \
    static ffi_type *glue(cif_args_,NAME)[3] = {                \
        dh_ffitype(t1), dh_ffitype(t2), dh_ffitype(t3)          \
    };                                                          \
    static ffi_cif glue(cif_,NAME) = {                          \
        .rtype = dh_ffitype(ret), .nargs = 3,                   \
        .arg_types = glue(cif_args_,NAME),                      \
    };

#define DEF_HELPER_FLAGS_4(NAME, FLAGS, ret, t1, t2, t3, t4)            \
    static ffi_type *glue(cif_args_,NAME)[4] = {                        \
        dh_ffitype(t1), dh_ffitype(t2), dh_ffitype(t3), dh_ffitype(t4)  \
    };                                                                  \
    static ffi_cif glue(cif_,NAME) = {                                  \
        .rtype = dh_ffitype(ret), .nargs = 4,                           \
        .arg_types = glue(cif_args_,NAME),                              \
    };

#define DEF_HELPER_FLAGS_5(NAME, FLAGS, ret, t1, t2, t3, t4, t5)        \
    static ffi_type *glue(cif_args_,NAME)[5] = {                        \
        dh_ffitype(t1), dh_ffitype(t2), dh_ffitype(t3),                 \
        dh_ffitype(t4), dh_ffitype(t5)                                  \
    };                                                                  \
    static ffi_cif glue(cif_,NAME) = {                                  \
        .rtype = dh_ffitype(ret), .nargs = 5,                           \
        .arg_types = glue(cif_args_,NAME),                              \
    };

#define DEF_HELPER_FLAGS_6(NAME, FLAGS, ret, t1, t2, t3, t4, t5, t6)    \
    static ffi_type *glue(cif_args_,NAME)[6] = {                        \
        dh_ffitype(t1), dh_ffitype(t2), dh_ffitype(t3),                 \
        dh_ffitype(t4), dh_ffitype(t5), dh_ffitype(t6)                  \
    };                                                                  \
    static ffi_cif glue(cif_,NAME) = {                                  \
        .rtype = dh_ffitype(ret), .nargs = 6,                           \
        .arg_types = glue(cif_args_,NAME),                              \
    };

#define DEF_HELPER_FLAGS_7(NAME, FLAGS, ret, t1, t2, t3, t4, t5, t6, t7) \
    static ffi_type *glue(cif_args_,NAME)[7] = {                        \
        dh_ffitype(t1), dh_ffitype(t2), dh_ffitype(t3),                 \
        dh_ffitype(t4), dh_ffitype(t5), dh_ffitype(t6), dh_ffitype(t7)  \
    };                                                                  \
    static ffi_cif glue(cif_,NAME) = {                                  \
        .rtype = dh_ffitype(ret), .nargs = 7,                           \
        .arg_types = glue(cif_args_,NAME),                              \
    };

#include "helper.h"
#include "trace/generated-helpers.h"
#include "tcg-runtime.h"

#undef DEF_HELPER_FLAGS_0
#undef DEF_HELPER_FLAGS_1
#undef DEF_HELPER_FLAGS_2
#undef DEF_HELPER_FLAGS_3
#undef DEF_HELPER_FLAGS_4
#undef DEF_HELPER_FLAGS_5
#undef DEF_HELPER_FLAGS_6
#undef DEF_HELPER_FLAGS_7

#endif /* HELPER_FFI_H */
