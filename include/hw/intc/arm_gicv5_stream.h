/*
 * Interface between GICv5 CPU interface and GICv5 IRS
 * Loosely modelled on the GICv5 Stream Protocol interface documented
 * in the GICv5 specification.
 *
 * Copyright (c) 2025 Linaro Limited
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_INTC_ARM_GICV5_STREAM_H
#define HW_INTC_ARM_GICV5_STREAM_H

#include "target/arm/cpu-qom.h"
#include "hw/intc/arm_gicv5_types.h"

typedef struct GICv5Common GICv5Common;

/**
 * gicv5_set_gicv5state
 * @cpu: CPU object to tell about its IRS
 * @cs: the GIC IRS it is connected to
 *
 * Set the CPU object's GICv5 pointer to point to this GIC IRS.
 * The IRS must call this when it is realized, for each CPU it is
 * connected to.
 *
 * Returns true on success, false if the CPU doesn't implement
 * the GICv5 CPU interface.
 */
bool gicv5_set_gicv5state(ARMCPU *cpu, GICv5Common *cs);

/*
 * The architected Stream Protocol is asynchronous; commands can be
 * initiated both from the IRS and from the CPU interface, and some
 * require acknowledgement. For QEMU, we simplify this because we know
 * that in the CPU interface code we hold the BQL and so our IRS model
 * is not going to be busy; when we send commands from the CPUIF
 * ("upstream commands") we can model this as a synchronous function
 * call whose return corresponds to the acknowledgement of a completed
 * command.
 */

/**
 * gicv5_set_priority
 * @cs: GIC IRS to send command to
 * @id: interrupt ID
 * @priority: priority to set
 * @domain: interrupt Domain to act on
 * @type: interrupt type (LPI or SPI)
 * @virtual: true if this is a virtual interrupt
 *
 * Set priority of an interrupt; matches stream interface
 * SetPriority command from CPUIF to IRS. There is no report back
 * of success/failure to the CPUIF in the protocol.
 */
void gicv5_set_priority(GICv5Common *cs, uint32_t id,
                        uint8_t priority, GICv5Domain domain,
                        GICv5IntType type, bool virtual);

#endif
