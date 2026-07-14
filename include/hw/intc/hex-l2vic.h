/*
 * QEMU L2VIC Interrupt Controller
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_INTC_HEX_L2VIC_H
#define HW_INTC_HEX_L2VIC_H

#include "qom/object.h"

#define L2VIC_VID_GRP_0 0x0 /* Read */
#define L2VIC_VID_GRP_1 0x4 /* Read */
#define L2VIC_VID_GRP_2 0x8 /* Read */
#define L2VIC_VID_GRP_3 0xC /* Read */
#define L2VIC_INT_ENABLEn 0x100 /* Read/Write */
#define L2VIC_INT_ENABLE_CLEARn 0x180 /* Write */
#define L2VIC_INT_ENABLE_SETn 0x200 /* Write */
#define L2VIC_INT_TYPEn 0x280 /* Read/Write */
#define L2VIC_INT_STATUSn 0x380 /* Read */
#define L2VIC_INT_CLEARn 0x400 /* Write */
#define L2VIC_SOFT_INTn 0x480 /* Write */
#define L2VIC_INT_PENDINGn 0x500 /* Read */
#define L2VIC_INT_GRPn_0 0x600 /* Read/Write */
#define L2VIC_INT_GRPn_1 0x680 /* Read/Write */
#define L2VIC_INT_GRPn_2 0x700 /* Read/Write */
#define L2VIC_INT_GRPn_3 0x780 /* Read/Write */

#define L2VIC_INTERRUPT_MAX 1024
/*
 * Note about l2vic groups:
 * Each interrupt to L2VIC can be configured to associate with one of
 * four groups.
 * Group 0 interrupts go to IRQ2 via VID 0 (SSR: 0xC2, the default)
 * Group 1 interrupts go to IRQ3 via VID 1 (SSR: 0xC3)
 * Group 2 interrupts go to IRQ4 via VID 2 (SSR: 0xC4)
 * Group 3 interrupts go to IRQ5 via VID 3 (SSR: 0xC5)
 */

#define TYPE_HEX_L2VIC "hex-l2vic"
/*
 * L2VIC Interface for CPU/GlobalReg interaction
 */
#define TYPE_HEX_L2VIC_INTERFACE "hex-l2vic-if"

typedef struct HexL2VicInterface HexL2VicInterface;

typedef struct HexL2VicInterfaceClass {
    InterfaceClass parent_class;

    /* Read VID register for given group */
    uint32_t (*read_vid)(HexL2VicInterface *l2vic, uint32_t group);

    /* Update VID register value */
    void (*update_vid)(HexL2VicInterface *l2vic, uint32_t group,
                        uint32_t value);

    /* Clear interrupt using CIAD instruction */
    void (*clear_interrupt)(HexL2VicInterface *l2vic);
} HexL2VicInterfaceClass;

DECLARE_OBJ_CHECKERS(HexL2VicInterface, HexL2VicInterfaceClass,
                     HEX_L2VIC_INTERFACE, TYPE_HEX_L2VIC_INTERFACE);

/* Convenience functions for interface users */
static inline uint32_t l2vic_read_vid(HexL2VicInterface *l2vic,
                                       uint32_t group)
{
    HexL2VicInterfaceClass *k = HEX_L2VIC_INTERFACE_GET_CLASS(l2vic);
    return k->read_vid(l2vic, group);
}

static inline void l2vic_update_vid(HexL2VicInterface *l2vic, uint32_t group,
                                     uint32_t value)
{
    HexL2VicInterfaceClass *k = HEX_L2VIC_INTERFACE_GET_CLASS(l2vic);
    k->update_vid(l2vic, group, value);
}

static inline void l2vic_clear_interrupt(HexL2VicInterface *l2vic)
{
    HexL2VicInterfaceClass *k = HEX_L2VIC_INTERFACE_GET_CLASS(l2vic);
    k->clear_interrupt(l2vic);
}

#endif /* HW_INTC_HEX_L2VIC_H */
