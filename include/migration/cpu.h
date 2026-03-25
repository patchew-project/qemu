/* Declarations for use for CPU state serialization.  */

#ifndef MIGRATION_CPU_H
#define MIGRATION_CPU_H

#ifdef TARGET_USING_LEGACY_MIGRATION_VMSTATE_UINTTL_API

#include "exec/cpu-defs.h"
#include "migration/vmstate.h"

#if TARGET_LONG_BITS == 64
#define VMSTATE_UINTTL(_f, _s) \
    VMSTATE_UINT64_V(_f, _s, 0)
#define VMSTATE_UINTTL_ARRAY(_f, _s, _n) \
    VMSTATE_UINT64_ARRAY_V(_f, _s, _n, 0)
#else
#define VMSTATE_UINTTL(_f, _s) \
    VMSTATE_UINT32_V(_f, _s, 0)
#define VMSTATE_UINTTL_ARRAY(_f, _s, _n) \
    VMSTATE_UINT32_ARRAY_V(_f, _s, _n, 0)
#endif

#endif /* TARGET_USING_LEGACY_MIGRATION_VMSTATE_UINTTL_API */

#endif
