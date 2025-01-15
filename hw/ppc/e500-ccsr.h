#ifndef E500_CCSR_H
#define E500_CCSR_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define NR_LAWS 12

struct PPCE500CCSRState {
    /*< private >*/
    SysBusDevice parent;
    /*< public >*/

    MemoryRegion ccsr_space;

    uint32_t law_regs[NR_LAWS * 2];
};

#define TYPE_CCSR "e500-ccsr"
OBJECT_DECLARE_SIMPLE_TYPE(PPCE500CCSRState, CCSR)

#endif /* E500_CCSR_H */
