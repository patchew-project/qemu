/*
 * Cortex-MPCore internal peripheral emulation.
 *
 * Copyright (c) 2009 CodeSourcery.
 * Copyright (c) 2011, 2012, 2023 Linaro Limited.
 * Written by Paul Brook, Peter Maydell.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_CPU_CORTEX_MPCORE_H
#define HW_CPU_CORTEX_MPCORE_H

#include "qom/object.h"
#include "hw/sysbus.h"
#include "hw/intc/arm_gic.h"
#include "hw/misc/a9scu.h"
#include "hw/timer/a9gtimer.h"
#include "hw/timer/arm_mptimer.h"

/*
 * This is a model of the Arm Cortex-A MPCore family of hardware.
 *
 * The A7 and A15 MPCore contain:
 *  up to 4 Cortex-A cores
 *  a GIC
 * The A9 MPCore additionally contains:
 *  a System Control Unit
 *  some timers and watchdogs
 *
 * QEMU interface:
 *  + QOM property "cluster-id" which set the cluster ID and its affinity.
 *  + QOM property "num-cores" which set the number of cores present in
 *    the cluster.
 *  + QOM property "cpu-type" is the CPU model typename.
 *  + QOM properties "cpu-has-el3", "cpu-has-el2" which set whether the CPUs
 *    have the exception level features present.
 *  + QOM properties "cpu-has-vfp-d32", "cpu-has-neon" which set whether the
 *    CPUs have the FPU features present.
 *  + QOM property "cpu-freq-hz" is the frequency of each core
 *  + QOM property "cpu-memory" is a MemoryRegion containing the devices
 *    provided by the board model.
 *  + QOM property "gic-spi-num" sets the number of GIC Shared Peripheral
 *    Interrupts.
 * QEMU interface forwarded from the GIC:
 *  + unnamed GPIO inputs: (where P is number of GIC SPIs, i.e. num-irq - 32)
 *    [0..P-1]  GIC SPIs
 *    [P..P+31] PPIs for CPU 0
 *    [P+32..P+63] PPIs for CPU 1
 *    ...
 *  + sysbus output IRQs: (in order; number will vary depending on number of
 *    cores)
 *    - IRQ for CPU 0
 *    - IRQ for CPU 1
 *      ...
 *    - FIQ for CPU 0
 *    - FIQ for CPU 1
 *      ...
 *    - VIRQ for CPU 0 (exists even if virt extensions not present)
 *    - VIRQ for CPU 1 (exists even if virt extensions not present)
 *      ...
 *    - VFIQ for CPU 0 (exists even if virt extensions not present)
 *    - VFIQ for CPU 1 (exists even if virt extensions not present)
 *      ...
 *    - maintenance IRQ for CPU i/f 0 (only if virt extensions present)
 *    - maintenance IRQ for CPU i/f 1 (only if virt extensions present)
 */
#define TYPE_CORTEX_MPCORE_PRIV "cortex_mpcore_priv"
OBJECT_DECLARE_TYPE(CortexMPPrivState, CortexMPPrivClass, CORTEX_MPCORE_PRIV)

/**
 * CortexMPPrivClass:
 * @container_size - size of the device's MMIO region
 * @gic_class_name - GIC QOM class name
 * @gic_spi_default - default number of GIC SPIs
 * @gic_spi_max - maximum number of GIC SPIs
 * @gic_revision - revision of the GIC
 */
struct CortexMPPrivClass {
    SysBusDeviceClass parent_class;

    DeviceRealize parent_realize;

    uint64_t container_size;

    const char *gic_class_name;
    unsigned gic_spi_default;
    unsigned gic_spi_max;
    unsigned gic_revision;
    uint32_t gic_priority_bits;
};

struct CortexMPPrivState {
    SysBusDevice parent_obj;

    MemoryRegion container;
    GICState gic;

    /* Properties */
    uint32_t num_cores;

    bool cpu_has_el3;
    bool cpu_has_el2;

    uint32_t gic_spi_num;
};

#define TYPE_A9MPCORE_PRIV "a9mpcore_priv"
OBJECT_DECLARE_SIMPLE_TYPE(A9MPPrivState, A9MPCORE_PRIV)

struct A9MPPrivState {
    CortexMPPrivState parent_obj;

    A9SCUState scu;
    A9GTimerState gtimer;
    ARMMPTimerState mptimer;
    ARMMPTimerState wdt;
};

#define TYPE_A15MPCORE_PRIV "a15mpcore_priv"

#define TYPE_A7MPCORE_PRIV "a7mpcore_priv"

#endif
