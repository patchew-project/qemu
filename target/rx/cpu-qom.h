#ifndef QEMU_SUPERH_CPU_QOM_H
#define QEMU_SUPERH_CPU_QOM_H

#include "qom/cpu.h"
/*
 * RX CPU
 *
 * Copyright (c) 2019 Yoshinori Sato
 * SPDX-License-Identifier: LGPL-2.0+
 */

#define TYPE_RX_CPU "rx-cpu"

#define TYPE_RX62N_CPU RX_CPU_TYPE_NAME("rx62n")

#define RXCPU_CLASS(klass) \
    OBJECT_CLASS_CHECK(RXCPUClass, (klass), TYPE_RX_CPU)
#define RXCPU(obj) \
    OBJECT_CHECK(RXCPU, (obj), TYPE_RX_CPU)
#define RXCPU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(RXCPUClass, (obj), TYPE_RX_CPU)

/*
 * RXCPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_reset: The parent class' reset handler.
 *
 * A RX CPU model.
 */
typedef struct RXCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
    void (*parent_reset)(CPUState *cpu);

} RXCPUClass;

#define CPUArchState struct CPURXState

#endif
