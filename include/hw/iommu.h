/*
 * General vIOMMU flags
 *
 * Copyright (C) 2025 Intel Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_IOMMU_H
#define HW_IOMMU_H

#include "qemu/bitops.h"

enum {
    /* Nesting parent HWPT will be reused by vIOMMU to create nested HWPT */
     VIOMMU_FLAG_WANT_NESTING_PARENT = BIT_ULL(0),
};

#endif /* HW_IOMMU_H */
