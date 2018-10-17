#ifndef HW_MISC_PVPANIC_H
#define HW_MISC_PVPANIC_H
#include "hw/sysbus.h"
#define TYPE_PVPANIC_MMIO "pvpanic-mmio"
#define PVPANIC_MMIO_DEVICE(obj)    \
    OBJECT_CHECK(PVPanicState, (obj), TYPE_PVPANIC_MMIO)
#endif

typedef struct PVPanicState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
} PVPanicState;
