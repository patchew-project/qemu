/*
 * QEMU KVM PMU Related Abstraction Header
 *
 * Copyright (C) 2025 Intel Corporation.
 *
 * Author: Zhao Liu <zhao1.liu@intel.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KVM_PMU_H
#define KVM_PMU_H

#include "qapi/qapi-types-kvm.h"
#include "qom/object.h"

#define TYPE_KVM_PMU_FILTER "kvm-pmu-filter"
OBJECT_DECLARE_SIMPLE_TYPE(KVMPMUFilter, KVM_PMU_FILTER)

/**
 * KVMPMUFilter:
 * @action: action that KVM PMU filter will take for selected PMU events.
 * @nevents: number of PMU event entries listed in @events
 * @events: list of PMU event entries.  A PMU event entry may represent one
 *    event or multiple events due to its format.
 */
struct KVMPMUFilter {
    Object parent_obj;

    KvmPmuFilterAction action;
    uint32_t nevents;
    KvmPmuFilterEventList *events;
};

#endif /* KVM_PMU_H */
