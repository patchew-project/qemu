/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Proxy interrupt controller device.
 *
 * Copyright (c) 2022 Bernhard Beschow <shentey@gmail.com>
 */

#ifndef HW_PROXY_PIC_H
#define HW_PROXY_PIC_H

#include "hw/qdev-core.h"
#include "qom/object.h"
#include "hw/irq.h"

#define TYPE_PROXY_PIC "proxy-pic"
OBJECT_DECLARE_SIMPLE_TYPE(ProxyPICState, PROXY_PIC)

#define MAX_PROXY_PIC_LINES 16

/**
 * This is a simple device which has 16 pairs of GPIO input and output lines.
 * Any change on an input line is forwarded to the respective output.
 *
 * QEMU interface:
 *  + 16 unnamed GPIO inputs: the input lines
 *  + 16 unnamed GPIO outputs: the output lines
 */
struct ProxyPICState {
    /*< private >*/
    struct DeviceState parent_obj;
    /*< public >*/

    qemu_irq in_irqs[MAX_PROXY_PIC_LINES];
    qemu_irq out_irqs[MAX_PROXY_PIC_LINES];
};

#endif /* HW_PROXY_PIC_H */
