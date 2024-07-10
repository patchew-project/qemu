/*
 * QEMU KVM PMU Abstraction Header
 *
 * Copyright (C) 2024 Intel Corporation.
 *
 * Authors:
 *  Zhao Liu <zhao1.liu@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#ifndef KVM_PMU_H
#define KVM_PMU_H

#include "qapi/qapi-types-kvm.h"
#include "qom/object.h"

#define TYPE_KVM_PMU_FILTER "kvm-pmu-filter"
OBJECT_DECLARE_SIMPLE_TYPE(KVMPMUFilter, KVM_PMU_FILTER)

struct KVMPMUFilter {
    Object parent_obj;

    uint32_t nevents;
    KVMPMUFilterEventList *events;
};

#endif /* KVM_PMU_H */
