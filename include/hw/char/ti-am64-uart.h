/*
 * TI AM64x UART emulation
 *
 * Copyright (c) 2025 CMBLU Energy AG
 * Author: Wadim Mueller <wafgo01@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_SERIAL_TI_AM64_H
#define HW_SERIAL_TI_AM64_H

#include "hw/char/serial.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_AM64_UART "ti-am64-uart"
OBJECT_DECLARE_SIMPLE_TYPE(AM64Uart, AM64_UART)

struct AM64Uart {
    SysBusDevice parent;

    SerialState serial;

    uint8_t regshift;
};

#endif
