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

/*
 * Stolen from Linux kernel (RAW_EVENT at tools/testing/selftests/kvm/include/
 * x86_64/pmu.h).
 *
 * Encode an eventsel+umask pair into event-select MSR format.  Note, this is
 * technically AMD's format, as Intel's format only supports 8 bits for the
 * event selector, i.e. doesn't use bits 24:16 for the selector.  But, OR-ing
 * in '0' is a nop and won't clobber the CMASK.
 */
#define X86_PMU_RAW_EVENT(eventsel, umask) (((eventsel & 0xf00UL) << 24) | \
                                            ((eventsel) & 0xff) | \
                                            ((umask) & 0xff) << 8)

#endif /* KVM_PMU_H */
