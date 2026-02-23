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

/* Architected GICv5 PPIs (as listed in R_XDVCM) */
#define GICV5_PPI_S_DB_PPI 0
#define GICV5_PPI_RL_DB_PPI 1
#define GICV5_PPI_NS_DB_PPI 2
#define GICV5_PPI_SW_PPI 3
#define GICV5_PPI_HACDBSIRQ 15
#define GICV5_PPI_CNTHVS 19
#define GICV5_PPI_CNTHPS 20
#define GICV5_PPI_PMBIRQ 21
#define GICV5_PPI_COMMIRQ 22
#define GICV5_PPI_PMUIRQ 23
#define GICV5_PPI_CTIIRQ 24
#define GICV5_PPI_GICMNT 25
#define GICV5_PPI_CNTHP 26
#define GICV5_PPI_CNTV 27
#define GICV5_PPI_CNTHV 28
#define GICV5_PPI_CNTPS 29
#define GICV5_PPI_CNTP 30
#define GICV5_PPI_TRBIRQ 31

/*
 * Type of the interrupt; these values match the 3-bit format
 * specified in the GICv5 spec R_GYVWB.
 */
typedef enum GICv5IntType {
    GICV5_PPI = 1,
    GICV5_LPI = 2,
    GICV5_SPI = 3,
} GICv5IntType;

#endif
