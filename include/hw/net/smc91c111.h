/*
 * SMSC 91C111 Ethernet interface emulation
 *
 * Copyright (c) 2005 CodeSourcery, LLC.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL
 */

#ifndef HW_NET_SMC91C111_H
#define HW_NET_SMC91C111_H

#include "hw/irq.h"
#include "net/net.h"

void smc91c111_init(NICInfo *, uint32_t, qemu_irq);

#endif
