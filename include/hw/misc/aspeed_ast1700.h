/*
 * ASPEED AST1700 IO Expander
 *
 * Copyright (C) 2025 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef ASPEED_AST1700_H
#define ASPEED_AST1700_H

#include "hw/sysbus.h"
#include "hw/misc/aspeed_scu.h"
#include "hw/adc/aspeed_adc.h"
#include "hw/gpio/aspeed_gpio.h"
#include "hw/i2c/aspeed_i2c.h"
#include "hw/misc/aspeed_ltpi.h"
#include "hw/ssi/aspeed_smc.h"
#include "hw/watchdog/wdt_aspeed.h"
#include "hw/char/serial-mm.h"
#include "hw/misc/unimp.h"

#define TYPE_ASPEED_AST1700 "aspeed.ast1700"

OBJECT_DECLARE_SIMPLE_TYPE(AspeedAST1700SoCState, ASPEED_AST1700)

struct AspeedAST1700SoCState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
};

#endif /* ASPEED_AST1700_H */
