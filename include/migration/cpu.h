/* Declarations for use for CPU state serialization.  */

#ifndef MIGRATION_CPU_H
#define MIGRATION_CPU_H

#include "exec/cpu-defs.h"
#include "migration/qemu-file-types.h"
#include "migration/vmstate.h"

#if TARGET_LONG_BITS == 64
#define VMSTATE_UINTTL_V(_f, _s, _v)                                  \
    VMSTATE_UINT64_V(_f, _s, _v)
#define VMSTATE_UINTTL_EQUAL_V(_f, _s, _v)                            \
    VMSTATE_UINT64_EQUAL_V(_f, _s, _v)
#define VMSTATE_UINTTL_ARRAY_V(_f, _s, _n, _v)                        \
    VMSTATE_UINT64_ARRAY_V(_f, _s, _n, _v)
#define VMSTATE_UINTTL_2DARRAY_V(_f, _s, _n1, _n2, _v)                \
    VMSTATE_UINT64_2DARRAY_V(_f, _s, _n1, _n2, _v)
#define VMSTATE_UINTTL_TEST(_f, _s, _t)                               \
    VMSTATE_UINT64_TEST(_f, _s, _t)
#define vmstate_info_uinttl vmstate_info_uint64
#else
#define VMSTATE_UINTTL_V(_f, _s, _v)                                  \
    VMSTATE_UINT32_V(_f, _s, _v)
#define VMSTATE_UINTTL_EQUAL_V(_f, _s, _v)                            \
    VMSTATE_UINT32_EQUAL_V(_f, _s, _v)
#define VMSTATE_UINTTL_ARRAY_V(_f, _s, _n, _v)                        \
    VMSTATE_UINT32_ARRAY_V(_f, _s, _n, _v)
#define VMSTATE_UINTTL_2DARRAY_V(_f, _s, _n1, _n2, _v)                \
    VMSTATE_UINT32_2DARRAY_V(_f, _s, _n1, _n2, _v)
#define VMSTATE_UINTTL_TEST(_f, _s, _t)                               \
    VMSTATE_UINT32_TEST(_f, _s, _t)
#define vmstate_info_uinttl vmstate_info_uint32
#endif

#define VMSTATE_UINTTL(_f, _s)                                        \
    VMSTATE_UINTTL_V(_f, _s, 0)
#define VMSTATE_UINTTL_EQUAL(_f, _s)                                  \
    VMSTATE_UINTTL_EQUAL_V(_f, _s, 0)
#define VMSTATE_UINTTL_ARRAY(_f, _s, _n)                              \
    VMSTATE_UINTTL_ARRAY_V(_f, _s, _n, 0)
#define VMSTATE_UINTTL_2DARRAY(_f, _s, _n1, _n2)                      \
    VMSTATE_UINTTL_2DARRAY_V(_f, _s, _n1, _n2, 0)


#endif
