/*
 * ARM GIC support
 *
 * Copyright (c) 2012 Linaro Limited
 * Written by Peter Maydell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * QEMU interface:
 *  + QOM property "num-cpu": number of CPUs to support
 *  + QOM property "num-irq": number of IRQs (including both SPIs and PPIs)
 *  + QOM property "revision": GIC version (1 or 2), or 0 for the 11MPCore GIC
 *  + QOM property "has-security-extensions": set true if the GIC should
 *    implement the security extensions
 *  + QOM property "has-virtualization-extensions": set true if the GIC should
 *    implement the virtualization extensions
 *  + unnamed GPIO inputs: (where P is number of PPIs, i.e. num-irq - 32)
 *    [0..P-1]  SPIs
 *    [P..P+31] PPIs for CPU 0
 *    [P+32..P+63] PPIs for CPU 1
 *    ...
 *  + sysbus IRQ 0 : IRQ
 *  + sysbus IRQ 1 : FIQ
 *  + sysbus IRQ 2 : VIRQ (exists even if virt extensions not present)
 *  + sysbus IRQ 3 : VFIQ (exists even if virt extensions not present)
 *  + sysbus IRQ 4 : maintenance IRQ for CPU i/f 0 (only if virt extns present)
 *  + sysbus IRQ 5 : maintenance IRQ for CPU i/f 1 (only if virt extns present)
 *    ...
 *  + sysbus MMIO regions: (in order; numbers will vary depending on
 *    whether virtualization extensions are present and on number of cores)
 *    - distributor registers (GICD*)
 *    - CPU interface for the accessing core (GICC*)
 *    - virtual interface control registers (GICH*) (only if virt extns present)
 *    - virtual CPU interface for the accessing core (GICV*) (only if virt)
 *    - CPU 0 CPU interface registers
 *    - CPU 1 CPU interface registers
 *      ...
 *    - CPU 0 VCPU interface registers (only if virt extns present)
 *    - CPU 1 VCPU interface registers (only if virt extns present)
 *      ...
 */

#ifndef HW_ARM_GIC_H
#define HW_ARM_GIC_H

#include "arm_gic_common.h"

/* Number of SGI target-list bits */
#define GIC_TARGETLIST_BITS 8

#define TYPE_ARM_GIC "arm_gic"
#define ARM_GIC(obj) \
     OBJECT_CHECK(GICState, (obj), TYPE_ARM_GIC)
#define ARM_GIC_CLASS(klass) \
     OBJECT_CLASS_CHECK(ARMGICClass, (klass), TYPE_ARM_GIC)
#define ARM_GIC_GET_CLASS(obj) \
     OBJECT_GET_CLASS(ARMGICClass, (obj), TYPE_ARM_GIC)

typedef struct ARMGICClass {
    /*< private >*/
    ARMGICCommonClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
} ARMGICClass;

#endif
