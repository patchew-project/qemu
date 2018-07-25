/*
 * ARM M Profile CPU class
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
#include "target/arm/idau.h"

#define TYPE_BITBAND "ARM,bitband-memory"
#define BITBAND(obj) OBJECT_CHECK(BitBandState, (obj), TYPE_BITBAND)

typedef struct {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    AddressSpace source_as;
    MemoryRegion iomem;
    uint32_t base;
    MemoryRegion *source_memory;
} BitBandState;

#define ARMV7M_NUM_BITBANDS 2

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
 *   that CPU accesses see. (The NVIC, bitbanding and other CPU-internal
 *   devices will be automatically layered on top of this view.)
 * + Property "idau": IDAU interface (forwarded to CPU object)
 * + Property "init-svtor": secure VTOR reset value (forwarded to CPU object)
 * + Property "enable-bitband": expose bitbanded IO
 */
typedef struct {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/
    NVICState nvic;
    BitBandState bitband[ARMV7M_NUM_BITBANDS];
    ARMCPU *cpu;

    /* MemoryRegion we pass to the CPU, with our devices layered on
     * top of the ones the board provides in board_memory.
     */
    MemoryRegion container;

    /* Properties */
    char *cpu_type;
    /* MemoryRegion the board provides to us (with its devices, RAM, etc) */
    MemoryRegion *board_memory;
    Object *idau;
    uint32_t init_svtor;
    bool enable_bitband;
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
