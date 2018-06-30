/*
 * ARM M Profile CPU base class
 *
 * Copyright (c) 2017 Linaro Ltd
 * Written by Peter Maydell <peter.maydell@linaro.org>
 *
 * Copyright (C) 2018 Red Hat, Inc.
 *
 * This code is licensed under the GPL version 2 or later.
 */

#ifndef HW_ARM_ARM_M_PROFILE_H
#define HW_ARM_ARM_M_PROFILE_H

#include "hw/sysbus.h"
#include "hw/intc/armv7m_nvic.h"

#define TYPE_ARM_M_PROFILE "arm-m-profile"
#define ARM_M_PROFILE(obj) OBJECT_CHECK(ARMMProfileState, (obj), \
                                        TYPE_ARM_M_PROFILE)
#define ARM_M_PROFILE_CLASS(klass) \
     OBJECT_CLASS_CHECK(ARMMProfileClass, (klass), TYPE_ARM_M_PROFILE)
#define ARM_M_PROFILE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(ARMMProfileClass, (obj), TYPE_ARM_M_PROFILE)

/* ARM M Profile container object.
 * + Unnamed GPIO input lines: external IRQ lines for the NVIC
 * + Named GPIO output SYSRESETREQ: signalled for guest AIRCR.SYSRESETREQ
 * + Property "cpu-type": CPU type to instantiate
 * + Property "num-irq": number of external IRQ lines
 * + Property "memory": MemoryRegion defining the physical address space
 *   that CPU accesses see. (The NVIC and other CPU-internal devices will be
 *   automatically layered on top of this view.)
 */
typedef struct {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/
    NVICState nvic;
    ARMCPU *cpu;

    /* MemoryRegion we pass to the CPU, with our devices layered on
     * top of the ones the board provides in board_memory.
     */
    MemoryRegion container;

    /* Properties */
    char *cpu_type;
    /* MemoryRegion the board provides to us (with its devices, RAM, etc) */
    MemoryRegion *board_memory;
} ARMMProfileState;

typedef struct {
    /*< private >*/
    SysBusDeviceClass parent_class;

    /*< public >*/
    /**
     * Initialize the CPU object, for example by setting properties, before it
     * gets realized.  May be NULL.
     */
    void (*cpu_init)(ARMMProfileState *s, Error **errp);
} ARMMProfileClass;

#endif
