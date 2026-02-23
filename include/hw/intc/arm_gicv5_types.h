/*
 * Type definitions for GICv5
 *
 * This file is for type definitions that we want to share between
 * the GIC proper and the CPU interface.
 *
 * Copyright (c) 2025 Linaro Limited
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_INTC_ARM_GICv5_TYPES_H
#define HW_INTC_ARM_GICv5_TYPES_H

/*
 * The GICv5 has four physical Interrupt Domains. This numbering
 * must match the encoding used in IRS_IDR0.INT_DOM.
 */
typedef enum GICv5Domain {
    GICV5_ID_S = 0,
    GICV5_ID_NS = 1,
    GICV5_ID_EL3 = 2,
    GICV5_ID_REALM = 3,
} GICv5Domain;

#define NUM_GICV5_DOMAINS 4

#endif
