/*
 * ASPEED SDRAM Memory Controller
 *
 * Copyright (C) 2016 IBM Corp.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */
#ifndef ASPEED_SDMC_H
#define ASPEED_SDMC_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_ASPEED_SDMC "aspeed.sdmc"
typedef struct AspeedSDMCClass AspeedSDMCClass;
typedef struct AspeedSDMCState AspeedSDMCState;
DECLARE_OBJ_CHECKERS(AspeedSDMCState, AspeedSDMCClass,
                     ASPEED_SDMC, TYPE_ASPEED_SDMC)
#define TYPE_ASPEED_2400_SDMC TYPE_ASPEED_SDMC "-ast2400"
#define TYPE_ASPEED_2500_SDMC TYPE_ASPEED_SDMC "-ast2500"
#define TYPE_ASPEED_2600_SDMC TYPE_ASPEED_SDMC "-ast2600"

#define ASPEED_SDMC_NR_REGS (0x174 >> 2)

struct AspeedSDMCState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;

    uint32_t regs[ASPEED_SDMC_NR_REGS];
    uint64_t ram_size;
    uint64_t max_ram_size;
};


struct AspeedSDMCClass {
    SysBusDeviceClass parent_class;

    uint64_t max_ram_size;
    const uint64_t *valid_ram_sizes;
    uint32_t (*compute_conf)(AspeedSDMCState *s, uint32_t data);
    void (*write)(AspeedSDMCState *s, uint32_t reg, uint32_t data);
};

#endif /* ASPEED_SDMC_H */
