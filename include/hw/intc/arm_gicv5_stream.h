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

#endif
