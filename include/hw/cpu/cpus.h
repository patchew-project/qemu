/*
 * QEMU CPUs type
 *
 * Copyright (c) 2022 GreenSocs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_CPU_CPUS_H
#define HW_CPU_CPUS_H

#include "qemu/typedefs.h"
#include "hw/qdev-core.h"
#include "qom/object.h"

/*
 * This object represent several CPUs which are all identical.
 *
 * If CPUs are not identical (for example, Cortex-A53 and Cortex-A57 CPUs in an
 * Arm big.LITTLE system) they should be in different groups. If the CPUs do
 * not have the same view of memory (for example the main CPU and a management
 * controller processor) they should be in different groups.
 *
 * This is an abstract class, subclasses are supposed to be created on
 * per-architecture basis to handle the specifics of the cpu architecture.
 * Subclasses are meant to be user-creatable (for cold-plug).
 */

#define TYPE_CPUS "cpus"
OBJECT_DECLARE_TYPE(CpusState, CpusClass, CPUS)

/**
 * CpusState:
 * @cpu_type: The type of cpu.
 * @topology.cpus: The number of cpus in this group.
 *      Explicity put this field into a topology structure in
 *      order to eventually update this smoothly with a full
 *      CpuTopology structure in the future.
 * @cpus: Array of pointer to cpu objects.
 */
struct CpusState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    char *cpu_type;
    struct {
        uint16_t cpus;
    } topology;
    CPUState **cpus;
};

typedef void (*CpusConfigureCpu)(CpusState *s, CPUState *cpu, unsigned idx);

/**
 * CpusClass:
 * @base_cpu_type: base cpu type accepted by this cpu group
 *     (the state cpu_type will be tested against it).
 * @configure_cpu: method to configure a cpu (called between
 *     cpu init and realize)
 * @skip_cpus_creation: CPUCLuster do not rely on creating
 *     cpus internally. This flag disables this feature.
 */
struct CpusClass {
    DeviceClass parent_class;
    const char *base_cpu_type;
    CpusConfigureCpu configure_cpu;
    bool skip_cpus_creation;
};

#endif /* HW_CPU_CPUS_H */
