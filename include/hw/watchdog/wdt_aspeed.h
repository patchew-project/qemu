/*
 * ASPEED Watchdog Controller
 *
 * Copyright (C) 2016-2017 IBM Corp.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */
#ifndef ASPEED_WDT_H
#define ASPEED_WDT_H

#include "hw/sysbus.h"

#define TYPE_ASPEED_WDT "aspeed.wdt"
#define ASPEED_WDT(obj) \
    OBJECT_CHECK(AspeedWDTState, (obj), TYPE_ASPEED_WDT)
#define ASPEED_WDT_CLASS(klass) \
    OBJECT_CLASS_CHECK(AspeedWDTClass, (klass), TYPE_ASPEED_WDT)
#define ASPEED_WDT_GET_CLASS(obj) \
    OBJECT_GET_CLASS(AspeedWDTClass, (obj), TYPE_ASPEED_WDT)

typedef struct AspeedWDTState {
    /*< private >*/
    SysBusDevice parent_obj;
    QEMUTimer *timer;
    bool enabled;

    /*< public >*/
    MemoryRegion iomem;

    uint32_t reg_status;
    uint32_t reg_reload_value;
    uint32_t reg_restart;
    uint32_t reg_ctrl;
} AspeedWDTState;

#endif  /* ASPEED_WDT_H */
