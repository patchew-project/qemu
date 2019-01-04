/*
 * SMSC LAN9118 Ethernet interface emulation
 *
 * Copyright (c) 2009 CodeSourcery, LLC.
 * Written by Paul Brook
 *
 * This code is licensed under the GNU GPL v2
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#ifndef HW_NET_LAN9118_H
#define HW_NET_LAN9118_H

#include "hw/irq.h"
#include "net/net.h"

void lan9118_init(NICInfo *, uint32_t, qemu_irq);

#endif
