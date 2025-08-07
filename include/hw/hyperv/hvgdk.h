/*
 * Type definitions for the mshv guest interface.
 *
 * Copyright Microsoft, Corp. 2025
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef _HVGDK_H
#define _HVGDK_H

#define HVGDK_H_VERSION         (25125)

enum hv_unimplemented_msr_action {
    HV_UNIMPLEMENTED_MSR_ACTION_FAULT = 0,
    HV_UNIMPLEMENTED_MSR_ACTION_IGNORE_WRITE_READ_ZERO = 1,
    HV_UNIMPLEMENTED_MSR_ACTION_COUNT = 2,
};

#endif /* _HVGDK_H */
